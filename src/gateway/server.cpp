#include "openclaw/gateway/server.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"
#include "openclaw/infra/device.hpp"

#include <cmath>
#include <filesystem>
#include <regex>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fstream>
#endif
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>

namespace openclaw::gateway {

namespace {
auto sanitize_outbound_text(std::string& text) -> void {
    // Strip script tags
    static const std::regex script_re(R"(<script[^>]*>.*?</script>)", std::regex::icase);
    text = std::regex_replace(text, script_re, "");
    // Strip event handlers (onclick, onerror, etc.)
    static const std::regex handler_re(R"(\bon\w+\s*=\s*"[^"]*")", std::regex::icase);
    text = std::regex_replace(text, handler_re, "");
    // Strip javascript: data URIs
    static const std::regex js_uri_re(R"(javascript\s*:)", std::regex::icase);
    text = std::regex_replace(text, js_uri_re, "");
}
} // anonymous namespace

// ===========================================================================
// Connection
// ===========================================================================

Connection::Connection(WsStream ws, std::string id,
                       std::shared_ptr<Protocol> protocol,
                       std::shared_ptr<HookRegistry> hooks)
    : ws_(std::move(ws))
    , id_(std::move(id))
    , protocol_(std::move(protocol))
    , hooks_(std::move(hooks)) {}

auto Connection::run() -> awaitable<void> {
    co_await read_loop();
}

auto Connection::send(const Frame& frame) -> awaitable<Result<void>> {
    if (!open_) {
        co_return std::unexpected(
            make_error(ErrorCode::ConnectionClosed, "Connection is closed"));
    }

    auto text = serialize_frame(frame);
    co_return co_await send_text(std::move(text));
}

auto Connection::send_text(std::string message) -> awaitable<Result<void>> {
    if (!open_) {
        co_return std::unexpected(
            make_error(ErrorCode::ConnectionClosed, "Connection is closed"));
    }

    try {
        sanitize_outbound_text(message);
        ws_.text(true);
        co_await ws_.async_write(
            net::buffer(message), net::use_awaitable);
        co_return Result<void>{};
    } catch (const boost::system::system_error& e) {
        LOG_WARN("Connection {}: write error: {}", id_, e.what());
        open_ = false;
        co_return std::unexpected(
            make_error(ErrorCode::IoError, "WebSocket write failed", e.what()));
    }
}

auto Connection::close() -> awaitable<void> {
    if (!open_) co_return;
    open_ = false;

    try {
        co_await ws_.async_close(
            websocket::close_code::normal, net::use_awaitable);
    } catch (const boost::system::system_error& e) {
        LOG_DEBUG("Connection {}: close error (expected if peer gone): {}",
                  id_, e.what());
    }
}

void Connection::set_auth(AuthInfo info) {
    auth_info_ = std::move(info);
}

auto Connection::auth() const noexcept -> const std::optional<AuthInfo>& {
    return auth_info_;
}

void Connection::set_scopes(std::vector<std::string> scopes) {
    scopes_ = std::move(scopes);
}

auto Connection::scopes() const noexcept -> const std::vector<std::string>& {
    return scopes_;
}

void Connection::set_device_public_key(std::string key) {
    device_public_key_ = std::move(key);
}

auto Connection::device_public_key() const noexcept -> const std::string& {
    return device_public_key_;
}

void Connection::set_nonce(std::string nonce) {
    connect_nonce_ = std::move(nonce);
}

auto Connection::nonce() const noexcept -> const std::string& {
    return connect_nonce_;
}

auto Connection::read_loop() -> awaitable<void> {
    beast::flat_buffer buffer;

    while (open_) {
        try {
            auto bytes = co_await ws_.async_read(buffer, net::use_awaitable);
            (void)bytes;

            auto data = beast::buffers_to_string(buffer.data());
            buffer.consume(buffer.size());

            auto frame_result = parse_frame(data);
            if (!frame_result) {
                LOG_WARN("Connection {}: bad frame: {}", id_,
                         frame_result.error().what());
                // Send an error response with a null id.
                auto err_resp = make_error_response(
                    "", ErrorCode::ProtocolError,
                    frame_result.error().message());
                co_await send(Frame{err_resp});
                continue;
            }

            co_await handle_frame(*frame_result);

        } catch (const boost::system::system_error& e) {
            if (e.code() == websocket::error::closed) {
                LOG_INFO("Connection {}: peer closed", id_);
            } else {
                LOG_WARN("Connection {}: read error: {}", id_, e.what());
            }
            open_ = false;
            break;
        }
    }
}

auto Connection::handle_frame(const Frame& frame) -> awaitable<void> {
    if (auto* req = std::get_if<RequestFrame>(&frame)) {
        co_await handle_request(*req);
    } else if (auto* resp = std::get_if<ResponseFrame>(&frame)) {
        // Clients normally don't send responses to the server.
        LOG_DEBUG("Connection {}: received unexpected response frame id={}",
                  id_, resp->id);
    } else if (auto* evt = std::get_if<EventFrame>(&frame)) {
        LOG_DEBUG("Connection {}: received event '{}' from client",
                  id_, evt->event);
    }
}

auto Connection::handle_request(const RequestFrame& req) -> awaitable<void> {
    LOG_DEBUG("Connection {}: request method={} id={}", id_, req.method, req.id);

    // Run before hooks.
    json hooked_params = co_await hooks_->run_before(req.method, req.params);

    // Build a modified request with hooked params.
    RequestFrame hooked_req{req.id, req.method, std::move(hooked_params)};

    // Dispatch to protocol handler.
    auto result = co_await protocol_->dispatch(hooked_req);

    Frame response_frame;
    if (result) {
        // Run after hooks on the result.
        json hooked_result = co_await hooks_->run_after(req.method, *result);
        response_frame = Frame{make_response(req.id, std::move(hooked_result))};
    } else {
        response_frame = Frame{make_error_response(
            req.id, result.error().code(), result.error().message())};
    }

    co_await send(response_frame);
}

// ===========================================================================
// GatewayServer
// ===========================================================================

GatewayServer::GatewayServer(net::io_context& ioc)
    : ioc_(ioc)
    , protocol_(std::make_shared<Protocol>())
    , hooks_(std::make_shared<HookRegistry>()) {}

auto GatewayServer::start(const GatewayConfig& config) -> awaitable<void> {
    config_ = config;
    max_connections_ = config.max_connections;

    // Configure authentication.
    if (config.auth) {
        authenticator_.configure(*config.auth);
    }

    // Determine bind address.
    auto address = (config.bind == BindMode::All)
        ? net::ip::make_address("0.0.0.0")
        : net::ip::make_address("127.0.0.1");

    auto endpoint = tcp::endpoint{address, config.port};

    // Create the acceptor.
    tcp::acceptor acceptor(ioc_);
    acceptor.open(endpoint.protocol());
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen(net::socket_base::max_listen_connections);

    running_ = true;
    LOG_INFO("Gateway server listening on {}:{}", address.to_string(), config.port);

    // Register SIGUSR1 for graceful state cleanup (restart)
#ifndef _WIN32
    boost::asio::co_spawn(ioc_, [this]() -> awaitable<void> {
        boost::asio::signal_set signals(ioc_, SIGUSR1);
        while (running_) {
            auto [ec, sig] = co_await signals.async_wait(
                boost::asio::as_tuple(net::use_awaitable));
            if (ec) break;
            LOG_INFO("SIGUSR1 received: clearing gateway state for restart");
            for (auto& [id, conn] : connections_) {
                co_await conn->close();
            }
            connections_.clear();
            LOG_INFO("Gateway state cleared ({} connections dropped)", 0);
        }
    }, boost::asio::detached);
#endif

    co_await accept_loop(acceptor);
}

auto GatewayServer::stop() -> awaitable<void> {
    if (!running_) co_return;
    running_ = false;

    LOG_INFO("Gateway server shutting down, closing {} connections",
             connections_.size());

    // Close all active connections.
    for (auto& [id, conn] : connections_) {
        co_await conn->close();
    }
    connections_.clear();

    LOG_INFO("Gateway server stopped");
}

void GatewayServer::on_connection(ConnectionCallback cb) {
    connection_callbacks_.push_back(std::move(cb));
}

auto GatewayServer::protocol() -> std::shared_ptr<Protocol> {
    return protocol_;
}

auto GatewayServer::hooks() -> std::shared_ptr<HookRegistry> {
    return hooks_;
}

auto GatewayServer::authenticator() -> Authenticator& {
    return authenticator_;
}

auto GatewayServer::broadcast(const EventFrame& event) -> awaitable<void> {
    auto frame = Frame{event};
    for (auto& [id, conn] : connections_) {
        if (conn->is_open()) {
            auto result = co_await conn->send(frame);
            if (!result) {
                LOG_DEBUG("Broadcast: failed to send to {}: {}",
                          id, result.error().what());
            }
        }
    }
}

auto GatewayServer::connection_count() const noexcept -> size_t {
    return connections_.size();
}

auto GatewayServer::is_running() const noexcept -> bool {
    return running_;
}

auto GatewayServer::accept_loop(tcp::acceptor& acceptor) -> awaitable<void> {
    while (running_) {
        try {
            auto socket = co_await acceptor.async_accept(net::use_awaitable);

            if (connections_.size() >= max_connections_) {
                LOG_WARN("Max connections ({}) reached, rejecting", max_connections_);
                socket.close();
                continue;
            }

            // Spawn connection handler as a detached coroutine.
            boost::asio::co_spawn(
                ioc_,
                handle_connection(std::move(socket)),
                boost::asio::detached);

        } catch (const boost::system::system_error& e) {
            if (!running_) break;  // Expected during shutdown.
            LOG_ERROR("Accept error: {}", e.what());
        }
    }
}

auto GatewayServer::handle_connection(tcp::socket socket) -> awaitable<void> {
    auto conn_id = utils::generate_id(12);
    auto remote_ep = socket.remote_endpoint();

    // Sanitize: truncate and clean remote endpoint for logging
    auto remote_addr = remote_ep.address().to_string();
    if (remote_addr.size() > 200) {
        remote_addr = remote_addr.substr(0, 200);
    }
    // Strip control characters from address
    std::erase_if(remote_addr, [](char c) {
        return (c < 32 && c != '\t') || c == 127;
    });

    LOG_INFO("New connection {} from {}:{}",
             conn_id, remote_addr, remote_ep.port());

    // Upgrade to WebSocket.
    Connection::WsStream ws(std::move(socket));

    try {
        // Set WebSocket options.
        ws.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::server));
        // v2026.2.24: Include HSTS header when configured
        auto hsts_value = config_.http_security_hsts;
        ws.set_option(websocket::stream_base::decorator(
            [hsts_value](websocket::response_type& res) {
                res.set(boost::beast::http::field::server,
                        "openclaw-gateway/0.1.0");
                if (!hsts_value.empty()) {
                    res.set("Strict-Transport-Security", hsts_value);
                }
            }));

        // Accept the WebSocket handshake.
        co_await ws.async_accept(net::use_awaitable);
    } catch (const boost::system::system_error& e) {
        LOG_WARN("Connection {}: WebSocket handshake failed: {}",
                 conn_id, e.what());
        co_return;
    }

    // --- v2026.2.22 challenge-response connect handshake ---

    // Step 1: Send connect.challenge event with a random nonce
    auto challenge_nonce = utils::generate_uuid();
    auto challenge_ts = utils::timestamp_ms();
    {
        auto challenge_event = make_event("connect.challenge", json{
            {"nonce", challenge_nonce},
            {"ts", challenge_ts},
        });
        ws.text(true);
        co_await ws.async_write(
            net::buffer(serialize_frame(Frame{challenge_event})),
            net::use_awaitable);
        LOG_DEBUG("Connection {}: sent connect.challenge nonce={}", conn_id, challenge_nonce);
    }

    // Step 2: Read the connect request
    beast::flat_buffer buf;
    try {
        co_await ws.async_read(buf, net::use_awaitable);
    } catch (const boost::system::system_error& e) {
        LOG_WARN("Connection {}: failed to read connect request: {}", conn_id, e.what());
        co_return;
    }

    auto connect_msg = beast::buffers_to_string(buf.data());
    buf.consume(buf.size());

    auto frame_result = parse_frame(connect_msg);
    if (!frame_result || !std::holds_alternative<RequestFrame>(*frame_result)) {
        LOG_WARN("Connection {}: expected connect request frame", conn_id);
        auto err = make_error_response("", ErrorCode::ProtocolError,
                                       "Expected connect request");
        ws.text(true);
        co_await ws.async_write(
            net::buffer(serialize_frame(Frame{err})), net::use_awaitable);
        co_await ws.async_close(websocket::close_code::policy_error, net::use_awaitable);
        co_return;
    }

    auto& connect_req = std::get<RequestFrame>(*frame_result);
    if (connect_req.method != "connect") {
        LOG_WARN("Connection {}: expected method 'connect', got '{}'",
                 conn_id, connect_req.method);
        auto err = make_error_response(connect_req.id, ErrorCode::ProtocolError,
                                       "First request must be 'connect'");
        ws.text(true);
        co_await ws.async_write(
            net::buffer(serialize_frame(Frame{err})), net::use_awaitable);
        co_await ws.async_close(websocket::close_code::policy_error, net::use_awaitable);
        co_return;
    }

    const auto& params = connect_req.params;

    // Step 3: Protocol version check
    int min_protocol = params.value("minProtocol", 0);
    int max_protocol = params.value("maxProtocol", 0);
    if (max_protocol < PROTOCOL_VERSION || min_protocol > PROTOCOL_VERSION) {
        LOG_WARN("Connection {}: protocol version mismatch (client {}-{}, server {})",
                 conn_id, min_protocol, max_protocol, PROTOCOL_VERSION);
        auto err = make_error_response(connect_req.id, ErrorCode::ProtocolError,
                                       "Unsupported protocol version");
        ws.text(true);
        co_await ws.async_write(
            net::buffer(serialize_frame(Frame{err})), net::use_awaitable);
        co_await ws.async_close(websocket::close_code::policy_error, net::use_awaitable);
        co_return;
    }

    // Step 4: Extract role and scopes
    std::string role = params.value("role", "operator");
    std::vector<std::string> requested_scopes;
    if (params.contains("scopes") && params["scopes"].is_array()) {
        for (const auto& s : params["scopes"]) {
            if (s.is_string()) {
                requested_scopes.push_back(s.get<std::string>());
            }
        }
    }

    // Step 5: Token auth (if authentication is required)
    AuthInfo auth_info;
    if (!authenticator_.is_open()) {
        std::string token;
        if (params.contains("auth") && params["auth"].is_object()) {
            token = params["auth"].value("token", "");
        }

        if (authenticator_.active_method() == AuthMethod::Tailscale) {
            auto result = co_await authenticator_.verify(
                remote_ep.address().to_string());
            if (!result) {
                LOG_WARN("Connection {}: tailscale auth failed: {}", conn_id,
                         result.error().what());
                auto err = make_error_response(connect_req.id, ErrorCode::Unauthorized,
                                               result.error().message());
                ws.text(true);
                co_await ws.async_write(
                    net::buffer(serialize_frame(Frame{err})), net::use_awaitable);
                co_await ws.async_close(websocket::close_code::policy_error,
                                        net::use_awaitable);
                co_return;
            }
            auth_info = *result;
        } else {
            if (token.empty()) {
                LOG_WARN("Connection {}: no auth token in connect request", conn_id);
                auto err = make_error_response(connect_req.id, ErrorCode::Unauthorized,
                                               "Authentication required");
                ws.text(true);
                co_await ws.async_write(
                    net::buffer(serialize_frame(Frame{err})), net::use_awaitable);
                co_await ws.async_close(websocket::close_code::policy_error,
                                        net::use_awaitable);
                co_return;
            }

            auto result = co_await authenticator_.verify(token);
            if (!result) {
                LOG_WARN("Connection {}: token auth failed", conn_id);
                auto err = make_error_response(connect_req.id, ErrorCode::Unauthorized,
                                               "Authentication failed");
                ws.text(true);
                co_await ws.async_write(
                    net::buffer(serialize_frame(Frame{err})), net::use_awaitable);
                co_await ws.async_close(websocket::close_code::policy_error,
                                        net::use_awaitable);
                co_return;
            }
            auth_info = *result;
        }
    }

    // Step 6: Device identity validation
    std::vector<std::string> granted_scopes = requested_scopes;
    std::string device_pub_key;
    bool has_valid_device = false;

    if (params.contains("device") && params["device"].is_object()) {
        const auto& device = params["device"];
        std::string dev_id = device.value("id", "");
        std::string dev_pub_key = device.value("publicKey", "");
        int64_t signed_at = device.value("signedAt", int64_t{0});
        std::string dev_nonce = device.value("nonce", "");
        std::string dev_signature = device.value("signature", "");
        std::string dev_client_id = device.value("clientId", "");
        std::string dev_client_mode = device.value("clientMode", "");

        // 6a: Derive device ID from public key and check match
        auto derived_id = infra::derive_device_id_from_public_key(dev_pub_key);
        if (derived_id != dev_id) {
            LOG_WARN("Connection {}: device ID mismatch (declared={}, derived={})",
                     conn_id, dev_id, derived_id);
            granted_scopes.clear();
        }
        // 6b: Check timestamp skew
        else if (std::abs(utils::timestamp_ms() - signed_at) > DEVICE_SIGNATURE_SKEW_MS) {
            LOG_WARN("Connection {}: device signature timestamp too stale", conn_id);
            granted_scopes.clear();
        }
        // 6c: Check nonce matches challenge
        else if (dev_nonce != challenge_nonce) {
            LOG_WARN("Connection {}: device nonce mismatch", conn_id);
            granted_scopes.clear();
        }
        // 6d: Verify Ed25519 signature
        else {
            // Reconstruct v2 payload
            std::string auth_token;
            if (params.contains("auth") && params["auth"].is_object()) {
                auth_token = params["auth"].value("token", "");
            }

            infra::DeviceAuthParams auth_params{
                .device_id = dev_id,
                .client_id = dev_client_id,
                .client_mode = dev_client_mode,
                .role = role,
                .scopes = requested_scopes,
                .signed_at_ms = signed_at,
                .token = auth_token,
                .nonce = dev_nonce,
            };
            auto payload = infra::build_device_auth_payload(auth_params);

            if (!infra::verify_device_signature(dev_pub_key, payload, dev_signature)) {
                LOG_WARN("Connection {}: device signature verification failed", conn_id);
                granted_scopes.clear();
            } else {
                has_valid_device = true;
                device_pub_key = dev_pub_key;
                LOG_DEBUG("Connection {}: device identity verified, id={}", conn_id, dev_id);
            }
        }
    } else if (!authenticator_.is_open()) {
        // Step 7: No device identity and auth is required → clear scopes
        LOG_DEBUG("Connection {}: no device identity, clearing scopes", conn_id);
        granted_scopes.clear();
    }

    // v2026.2.25: Trusted proxy control-UI bypass requires operator role.
    // Non-operator connections claiming control-ui through trusted proxy
    // get their scopes cleared to prevent privilege escalation.
    bool is_control_ui = params.value("clientMode", "") == "control-ui";
    if (is_control_ui && auth_info.trusted_proxy_auth_ok && role != "operator") {
        LOG_WARN("Connection {}: control-ui via trusted proxy without operator role, clearing scopes",
                 conn_id);
        granted_scopes.clear();
    }

    // v2026.2.25: Require pairing for unpaired operator device auth.
    // If auth is required, device is not validated, and it's not a trusted proxy,
    // clear scopes to force pairing flow.
    if (!authenticator_.is_open() && !has_valid_device &&
        !auth_info.trusted_proxy_auth_ok && role == "operator") {
        LOG_DEBUG("Connection {}: unpaired operator device, clearing scopes for pairing", conn_id);
        granted_scopes.clear();
    }

    // Step 8: Send hello-ok response
    {
        auto hello_ok = make_response(connect_req.id, json{
            {"type", "hello-ok"},
            {"protocol", PROTOCOL_VERSION},
            {"policy", {
                {"tickIntervalMs", TICK_INTERVAL_MS},
                {"maxPayload", MAX_PAYLOAD_BYTES},
                {"maxBufferedBytes", MAX_BUFFERED_BYTES},
            }},
        });
        ws.text(true);
        co_await ws.async_write(
            net::buffer(serialize_frame(Frame{hello_ok})),
            net::use_awaitable);
        LOG_DEBUG("Connection {}: sent hello-ok, scopes={}, device={}",
                  conn_id, granted_scopes.size(), has_valid_device);
    }

    // Step 9: Create Connection with auth info and scopes
    auto conn = std::make_shared<Connection>(
        std::move(ws), conn_id, protocol_, hooks_);
    conn->set_auth(std::move(auth_info));
    conn->set_scopes(std::move(granted_scopes));
    conn->set_nonce(challenge_nonce);
    if (!device_pub_key.empty()) {
        conn->set_device_public_key(std::move(device_pub_key));
    }
    add_connection(conn);

    // Notify callbacks.
    for (auto& cb : connection_callbacks_) {
        cb(conn);
    }

    co_await conn->run();
    remove_connection(conn_id);

    LOG_INFO("Connection {} closed", conn_id);
}

void GatewayServer::add_connection(std::shared_ptr<Connection> conn) {
    connections_[conn->id()] = std::move(conn);
}

void GatewayServer::remove_connection(const std::string& id) {
    connections_.erase(id);
}

// ===========================================================================
// Avatar path validation
// ===========================================================================

auto GatewayServer::validate_avatar_path(
    const std::filesystem::path& path,
    const std::filesystem::path& root) -> Result<void>
{
    namespace fs = std::filesystem;
    constexpr size_t kMaxAvatarBytes = 2 * 1024 * 1024;  // 2MB

    // Reject if path doesn't exist
    if (!fs::exists(path)) {
        return std::unexpected(make_error(ErrorCode::NotFound,
            "Avatar file not found", path.string()));
    }

    // Reject symlinks outside root
    if (fs::is_symlink(path)) {
        auto target = fs::read_symlink(path);
        auto canonical_target = fs::canonical(target.is_absolute() ? target : path.parent_path() / target);
        auto canonical_root = fs::canonical(root);
        auto target_str = canonical_target.string();
        auto root_str = canonical_root.string();
        if (!target_str.starts_with(root_str)) {
            return std::unexpected(make_error(ErrorCode::Forbidden,
                "Avatar symlink points outside allowed root",
                path.string() + " -> " + target_str));
        }
    }

    // Canonical containment check
    auto canonical_path = fs::canonical(path);
    auto canonical_root = fs::canonical(root);
    if (!canonical_path.string().starts_with(canonical_root.string())) {
        return std::unexpected(make_error(ErrorCode::Forbidden,
            "Avatar path traversal detected",
            canonical_path.string()));
    }

    // Size check
    auto file_size = fs::file_size(path);
    if (file_size > kMaxAvatarBytes) {
        return std::unexpected(make_error(ErrorCode::InvalidArgument,
            "Avatar file exceeds 2MB limit",
            std::to_string(file_size) + " bytes"));
    }

    return {};
}

// ===========================================================================
// Workspace file path resolution (v2026.2.25)
// ===========================================================================

auto GatewayServer::resolve_agent_workspace_file_path(
    const std::filesystem::path& requested_path,
    const std::filesystem::path& workspace_root)
    -> ResolvedAgentWorkspaceFilePath
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // Normalize the workspace root
    auto canonical_root = fs::canonical(workspace_root, ec);
    if (ec) {
        return {WorkspaceFileStatus::Invalid, {}, "Cannot canonicalize workspace root: " + ec.message()};
    }

    // Build the resolved path
    fs::path resolved;
    if (requested_path.is_absolute()) {
        resolved = requested_path;
    } else {
        resolved = workspace_root / requested_path;
    }

    // Normalize (resolve .., .)
    resolved = resolved.lexically_normal();

    // Check if file exists
    if (!fs::exists(resolved, ec) || ec) {
        // File doesn't exist — verify the path would be within workspace
        auto parent = resolved.parent_path();
        if (fs::exists(parent, ec) && !ec) {
            auto canonical_parent = fs::canonical(parent, ec);
            if (!ec && canonical_parent.string().starts_with(canonical_root.string())) {
                return {WorkspaceFileStatus::Missing, resolved, ""};
            }
        }
        return {WorkspaceFileStatus::Invalid, resolved,
                "Path parent escapes workspace or doesn't exist"};
    }

    // Resolve canonical path (follows symlinks)
    auto canonical_path = fs::canonical(resolved, ec);
    if (ec) {
        return {WorkspaceFileStatus::Invalid, resolved,
                "Cannot canonicalize path: " + ec.message()};
    }

    // Verify containment
    if (!canonical_path.string().starts_with(canonical_root.string())) {
        return {WorkspaceFileStatus::Invalid, canonical_path,
                "Path escapes workspace after symlink resolution"};
    }

    return {WorkspaceFileStatus::Ready, canonical_path, ""};
}

auto GatewayServer::write_file_safely(
    const std::filesystem::path& path,
    std::string_view data,
    const std::filesystem::path& workspace_root) -> Result<void>
{
    namespace fs = std::filesystem;

    // First resolve the path
    auto resolved = resolve_agent_workspace_file_path(path, workspace_root);
    if (resolved.status == WorkspaceFileStatus::Invalid) {
        return std::unexpected(make_error(ErrorCode::Forbidden,
            "Cannot write to path outside workspace",
            resolved.error_detail));
    }

#ifndef _WIN32
    // Use O_NOFOLLOW to prevent writing through symlinks
    // O_CREAT | O_TRUNC: create or truncate
    int fd = ::open(resolved.resolved_path.c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) {
        if (errno == ELOOP) {
            return std::unexpected(make_error(ErrorCode::Forbidden,
                "Refusing to write through symlink (O_NOFOLLOW)",
                resolved.resolved_path.string()));
        }
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to open file for writing",
            resolved.resolved_path.string() + ": " + std::string(strerror(errno))));
    }

    // Write data
    auto written = ::write(fd, data.data(), data.size());
    if (written < 0 || static_cast<size_t>(written) != data.size()) {
        ::close(fd);
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to write file data",
            resolved.resolved_path.string()));
    }

    // Post-write identity validation: fstat the open fd and lstat the path,
    // verify they reference the same inode
    struct stat fd_stat{};
    struct stat path_stat{};
    if (::fstat(fd, &fd_stat) == 0 && ::lstat(resolved.resolved_path.c_str(), &path_stat) == 0) {
        if (fd_stat.st_ino != path_stat.st_ino || fd_stat.st_dev != path_stat.st_dev) {
            ::close(fd);
            LOG_WARN("write_file_safely: TOCTOU detected on {}",
                     resolved.resolved_path.string());
            return std::unexpected(make_error(ErrorCode::Forbidden,
                "TOCTOU race detected during file write",
                resolved.resolved_path.string()));
        }
    }

    ::close(fd);
#else
    // Windows: basic write without O_NOFOLLOW
    std::ofstream ofs(resolved.resolved_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to open file for writing",
            resolved.resolved_path.string()));
    }
    ofs.write(data.data(), data.size());
    if (!ofs) {
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to write file data",
            resolved.resolved_path.string()));
    }
#endif

    LOG_DEBUG("Safely wrote {} bytes to {}", data.size(),
              resolved.resolved_path.string());
    return {};
}

} // namespace openclaw::gateway
