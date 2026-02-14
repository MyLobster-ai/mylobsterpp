#include "openclaw/gateway/server.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>

namespace openclaw::gateway {

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
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(boost::beast::http::field::server,
                        "openclaw-gateway/0.1.0");
            }));

        // Accept the WebSocket handshake.
        co_await ws.async_accept(net::use_awaitable);
    } catch (const boost::system::system_error& e) {
        LOG_WARN("Connection {}: WebSocket handshake failed: {}",
                 conn_id, e.what());
        co_return;
    }

    // Authenticate if required.
    if (!authenticator_.is_open()) {
        // For token auth, expect the first message to contain the token.
        // For tailscale, use the remote endpoint.
        AuthInfo auth_info;
        if (authenticator_.active_method() == AuthMethod::Tailscale) {
            auto result = co_await authenticator_.verify(
                remote_ep.address().to_string());
            if (!result) {
                LOG_WARN("Connection {}: auth failed: {}", conn_id,
                         result.error().what());
                // Send error and close.
                auto err = make_error_response("", ErrorCode::Unauthorized,
                                               result.error().message());
                ws.text(true);
                co_await ws.async_write(
                    net::buffer(serialize_frame(Frame{err})),
                    net::use_awaitable);
                co_await ws.async_close(websocket::close_code::policy_error,
                                        net::use_awaitable);
                co_return;
            }
            auth_info = *result;
        } else {
            // Token auth: read the first message as auth.
            beast::flat_buffer buf;
            co_await ws.async_read(buf, net::use_awaitable);
            auto auth_msg = beast::buffers_to_string(buf.data());
            buf.consume(buf.size());

            // Try to parse as JSON with a "token" field, or use raw string.
            std::string token;
            try {
                auto j = json::parse(auth_msg);
                if (j.contains("token") && j["token"].is_string()) {
                    token = j["token"].get<std::string>();
                } else {
                    token = auth_msg;
                }
            } catch (...) {
                token = auth_msg;
            }

            auto result = co_await authenticator_.verify(token);
            if (!result) {
                LOG_WARN("Connection {}: token auth failed", conn_id);
                auto err = make_error_response("", ErrorCode::Unauthorized,
                                               "Authentication failed");
                ws.text(true);
                co_await ws.async_write(
                    net::buffer(serialize_frame(Frame{err})),
                    net::use_awaitable);
                co_await ws.async_close(websocket::close_code::policy_error,
                                        net::use_awaitable);
                co_return;
            }
            auth_info = *result;

            // Send auth success event.
            auto event = make_event("auth.success", json{
                {"identity", auth_info.identity},
            });
            ws.text(true);
            co_await ws.async_write(
                net::buffer(serialize_frame(Frame{event})),
                net::use_awaitable);
        }

        auto conn = std::make_shared<Connection>(
            std::move(ws), conn_id, protocol_, hooks_);
        conn->set_auth(std::move(auth_info));
        add_connection(conn);

        // Notify callbacks.
        for (auto& cb : connection_callbacks_) {
            cb(conn);
        }

        co_await conn->run();
        remove_connection(conn_id);
    } else {
        // No auth required.
        auto conn = std::make_shared<Connection>(
            std::move(ws), conn_id, protocol_, hooks_);
        add_connection(conn);

        for (auto& cb : connection_callbacks_) {
            cb(conn);
        }

        co_await conn->run();
        remove_connection(conn_id);
    }

    LOG_INFO("Connection {} closed", conn_id);
}

void GatewayServer::add_connection(std::shared_ptr<Connection> conn) {
    connections_[conn->id()] = std::move(conn);
}

void GatewayServer::remove_connection(const std::string& id) {
    connections_.erase(id);
}

} // namespace openclaw::gateway
