#include "openclaw/gateway/client.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>

namespace openclaw::gateway {

// ===========================================================================
// GatewayClient
// ===========================================================================

GatewayClient::GatewayClient(net::io_context& ioc)
    : ioc_(ioc) {}

GatewayClient::~GatewayClient() {
    connected_ = false;
}

GatewayClient::GatewayClient(GatewayClient&& other) noexcept
    : ioc_(other.ioc_)
    , ws_(std::move(other.ws_))
    , host_(std::move(other.host_))
    , port_(std::move(other.port_))
    , path_(std::move(other.path_))
    , auth_token_(std::move(other.auth_token_))
    , frame_callbacks_(std::move(other.frame_callbacks_))
    , state_callbacks_(std::move(other.state_callbacks_))
    , pending_calls_(std::move(other.pending_calls_))
    , connected_(other.connected_.load(std::memory_order_relaxed))
    , auto_reconnect_(other.auto_reconnect_)
    , reconnect_delay_ms_(other.reconnect_delay_ms_)
    , reconnect_max_attempts_(other.reconnect_max_attempts_)
{
    other.connected_.store(false, std::memory_order_relaxed);
}

GatewayClient& GatewayClient::operator=(GatewayClient&& other) noexcept {
    if (this != &other) {
        ws_ = std::move(other.ws_);
        host_ = std::move(other.host_);
        port_ = std::move(other.port_);
        path_ = std::move(other.path_);
        auth_token_ = std::move(other.auth_token_);
        frame_callbacks_ = std::move(other.frame_callbacks_);
        state_callbacks_ = std::move(other.state_callbacks_);
        pending_calls_ = std::move(other.pending_calls_);
        connected_.store(other.connected_.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
        other.connected_.store(false, std::memory_order_relaxed);
        auto_reconnect_ = other.auto_reconnect_;
        reconnect_delay_ms_ = other.reconnect_delay_ms_;
        reconnect_max_attempts_ = other.reconnect_max_attempts_;
    }
    return *this;
}

auto GatewayClient::connect(std::string_view host, std::string_view port,
                             std::string_view path)
    -> awaitable<Result<void>> {
    host_ = std::string(host);
    port_ = std::string(port);
    path_ = std::string(path);

    try {
        // Resolve the host.
        tcp::resolver resolver(ioc_);
        auto results = co_await resolver.async_resolve(
            host_, port_, net::use_awaitable);

        // Create a new WebSocket stream.
        ws_ = std::make_unique<WsStream>(ioc_);

        // Connect the underlying TCP socket.
        auto ep = co_await beast::get_lowest_layer(*ws_).async_connect(
            results, net::use_awaitable);

        // Set the Host field in the handshake.
        auto host_str = host_ + ":" + std::to_string(ep.port());

        // Set WebSocket options.
        ws_->set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::client));
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(boost::beast::http::field::user_agent,
                        "openclaw-client/0.1.0");
            }));

        // Perform the WebSocket handshake.
        co_await ws_->async_handshake(host_str, path_, net::use_awaitable);

        connected_ = true;
        LOG_INFO("Connected to gateway at {}:{}{}", host_, port_, path_);

        // If we have an auth token, send it as the first message.
        if (!auth_token_.empty()) {
            json auth_msg = json{{"token", auth_token_}};
            ws_->text(true);
            co_await ws_->async_write(
                net::buffer(auth_msg.dump()), net::use_awaitable);

            // Read the auth response.
            beast::flat_buffer buf;
            co_await ws_->async_read(buf, net::use_awaitable);
            auto response_str = beast::buffers_to_string(buf.data());
            buf.consume(buf.size());

            auto frame_result = parse_frame(response_str);
            if (frame_result) {
                // Check for auth error.
                if (auto* resp = std::get_if<ResponseFrame>(&*frame_result)) {
                    if (resp->is_error()) {
                        connected_ = false;
                        auto err_msg = resp->error->value("message", "Auth failed");
                        co_return make_fail(
                            make_error(ErrorCode::Unauthorized, err_msg));
                    }
                }
                // Notify about the auth frame.
                notify_frame(*frame_result);
            }
        }

        notify_state(true);

        // Start the read loop.
        boost::asio::co_spawn(ioc_, read_loop(), boost::asio::detached);

        co_return ok_result();

    } catch (const boost::system::system_error& e) {
        LOG_ERROR("Failed to connect to {}:{}: {}", host_, port_, e.what());
        connected_ = false;
        co_return make_fail(
            make_error(ErrorCode::ConnectionFailed,
                       "WebSocket connection failed", e.what()));
    }
}

auto GatewayClient::disconnect() -> awaitable<void> {
    if (!connected_ || !ws_) co_return;
    connected_ = false;

    try {
        co_await ws_->async_close(
            websocket::close_code::normal, net::use_awaitable);
    } catch (const boost::system::system_error& e) {
        LOG_DEBUG("Client disconnect error (expected): {}", e.what());
    }

    notify_state(false);
    LOG_INFO("Disconnected from gateway");
}

auto GatewayClient::send(const Frame& frame) -> awaitable<Result<void>> {
    if (!connected_ || !ws_) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionClosed, "Not connected"));
    }

    try {
        auto text = serialize_frame(frame);
        ws_->text(true);
        co_await ws_->async_write(
            net::buffer(text), net::use_awaitable);
        co_return ok_result();
    } catch (const boost::system::system_error& e) {
        LOG_WARN("Client send error: {}", e.what());
        co_return make_fail(
            make_error(ErrorCode::IoError, "WebSocket write failed", e.what()));
    }
}

auto GatewayClient::call(std::string_view method, json params,
                          uint32_t timeout_ms)
    -> awaitable<Result<json>> {
    if (!connected_ || !ws_) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionClosed, "Not connected"));
    }

    auto request_id = utils::generate_id(16);

    // Build the request frame.
    RequestFrame req{
        .id = request_id,
        .method = std::string(method),
        .params = std::move(params),
    };

    // Set up a promise/future mechanism using a timer + callback.
    struct CallState {
        std::optional<Result<json>> result;
        bool completed = false;
    };
    auto state = std::make_shared<CallState>();

    // Register the pending call.
    pending_calls_[request_id] = PendingCall{
        .resolver = [state](Result<json> r) {
            state->result = std::move(r);
            state->completed = true;
        },
    };

    // Send the request.
    auto send_result = co_await send(Frame{req});
    if (!send_result) {
        pending_calls_.erase(request_id);
        co_return make_fail(send_result.error());
    }

    // Wait for the response with timeout.
    net::steady_timer timer(ioc_);
    timer.expires_after(std::chrono::milliseconds(timeout_ms));

    // Poll until completed or timeout.
    while (!state->completed) {
        timer.expires_after(std::chrono::milliseconds(10));
        auto [ec] = co_await timer.async_wait(
            net::as_tuple(net::use_awaitable));

        if (!connected_) {
            pending_calls_.erase(request_id);
            co_return make_fail(
                make_error(ErrorCode::ConnectionClosed,
                           "Connection lost while waiting for response"));
        }

        // Check timeout: if we've been waiting too long.
        // Simple approach: decrement remaining time.
        // A production implementation would use a proper deadline timer.
        timeout_ms -= 10;
        if (timeout_ms == 0) {
            pending_calls_.erase(request_id);
            co_return make_fail(
                make_error(ErrorCode::Timeout,
                           "RPC call timed out: " + std::string(method)));
        }
    }

    pending_calls_.erase(request_id);

    if (state->result) {
        co_return std::move(*state->result);
    }

    co_return make_fail(
        make_error(ErrorCode::InternalError, "No result received"));
}

void GatewayClient::on_frame(FrameCallback cb) {
    frame_callbacks_.push_back(std::move(cb));
}

void GatewayClient::on_state_change(StateCallback cb) {
    state_callbacks_.push_back(std::move(cb));
}

void GatewayClient::set_auth_token(std::string token) {
    auth_token_ = std::move(token);
}

void GatewayClient::set_auto_reconnect(bool enabled, uint32_t delay_ms,
                                        uint32_t max_attempts) {
    auto_reconnect_ = enabled;
    reconnect_delay_ms_ = delay_ms;
    reconnect_max_attempts_ = max_attempts;
}

auto GatewayClient::is_connected() const noexcept -> bool {
    return connected_;
}

auto GatewayClient::read_loop() -> awaitable<void> {
    beast::flat_buffer buffer;

    while (connected_ && ws_) {
        try {
            auto bytes = co_await ws_->async_read(buffer, net::use_awaitable);
            (void)bytes;

            auto data = beast::buffers_to_string(buffer.data());
            buffer.consume(buffer.size());

            auto frame_result = parse_frame(data);
            if (!frame_result) {
                LOG_WARN("Client: bad frame from server: {}",
                         frame_result.error().what());
                continue;
            }

            // If this is a response, check for pending calls.
            if (auto* resp = std::get_if<ResponseFrame>(&*frame_result)) {
                auto it = pending_calls_.find(resp->id);
                if (it != pending_calls_.end()) {
                    if (resp->is_error()) {
                        auto msg = resp->error->value("message", "RPC error");
                        auto code_val = resp->error->value("code",
                            static_cast<int>(ErrorCode::InternalError));
                        it->second.resolver(std::unexpected(
                            make_error(static_cast<ErrorCode>(code_val), msg)));
                    } else {
                        it->second.resolver(resp->result.value_or(json::object()));
                    }
                    // Don't erase here; the caller will erase.
                }
            }

            notify_frame(*frame_result);

        } catch (const boost::system::system_error& e) {
            if (e.code() == websocket::error::closed) {
                LOG_INFO("Client: server closed connection");
            } else {
                LOG_WARN("Client: read error: {}", e.what());
            }
            connected_ = false;
            break;
        }
    }

    notify_state(false);

    // Resolve any pending calls with connection error.
    for (auto& [id, pending] : pending_calls_) {
        pending.resolver(std::unexpected(
            make_error(ErrorCode::ConnectionClosed,
                       "Connection closed while waiting for response")));
    }
    pending_calls_.clear();

    // Auto-reconnect if enabled.
    if (auto_reconnect_) {
        co_await try_reconnect();
    }
}

auto GatewayClient::try_reconnect() -> awaitable<void> {
    for (uint32_t attempt = 1; attempt <= reconnect_max_attempts_; ++attempt) {
        LOG_INFO("Client: reconnect attempt {}/{}", attempt,
                 reconnect_max_attempts_);

        // Wait before reconnecting.
        net::steady_timer timer(ioc_);
        timer.expires_after(std::chrono::milliseconds(
            reconnect_delay_ms_ * attempt));  // Exponential-ish backoff.
        co_await timer.async_wait(net::use_awaitable);

        auto result = co_await connect(host_, port_, path_);
        if (result) {
            LOG_INFO("Client: reconnected successfully");
            co_return;
        }

        LOG_WARN("Client: reconnect attempt {} failed: {}",
                 attempt, result.error().what());
    }

    LOG_ERROR("Client: all reconnect attempts exhausted");
}

void GatewayClient::notify_frame(const Frame& frame) {
    for (auto& cb : frame_callbacks_) {
        try {
            cb(frame);
        } catch (const std::exception& e) {
            LOG_WARN("Frame callback threw: {}", e.what());
        }
    }
}

void GatewayClient::notify_state(bool connected) {
    for (auto& cb : state_callbacks_) {
        try {
            cb(connected);
        } catch (const std::exception& e) {
            LOG_WARN("State callback threw: {}", e.what());
        }
    }
}

} // namespace openclaw::gateway
