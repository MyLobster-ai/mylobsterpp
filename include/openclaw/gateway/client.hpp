#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/gateway/frame.hpp"

namespace openclaw::gateway {

using boost::asio::awaitable;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;

/// Callback for frames received from the server.
using FrameCallback = std::function<void(const Frame&)>;

/// Callback for connection state changes.
using StateCallback = std::function<void(bool connected)>;

/// A client for connecting to a GatewayServer over WebSocket.
/// Supports sending requests, receiving responses and events, and
/// an optional auto-reconnect mechanism.
class GatewayClient {
public:
    explicit GatewayClient(net::io_context& ioc);
    ~GatewayClient();

    // Non-copyable, movable.
    GatewayClient(const GatewayClient&) = delete;
    GatewayClient& operator=(const GatewayClient&) = delete;
    GatewayClient(GatewayClient&&) noexcept;
    GatewayClient& operator=(GatewayClient&&) noexcept;

    /// Connect to a gateway server.
    /// @param host  Hostname or IP address.
    /// @param port  Port number (as string, e.g. "18789").
    /// @param path  WebSocket path (default "/").
    auto connect(std::string_view host, std::string_view port,
                 std::string_view path = "/")
        -> awaitable<Result<void>>;

    /// Disconnect from the server.
    auto disconnect() -> awaitable<void>;

    /// Send a Frame to the server.
    auto send(const Frame& frame) -> awaitable<Result<void>>;

    /// Send a request and wait for the matching response.
    /// @param method  RPC method name.
    /// @param params  Method parameters.
    /// @param timeout_ms  Maximum time to wait for a response.
    auto call(std::string_view method, json params,
              uint32_t timeout_ms = 30000)
        -> awaitable<Result<json>>;

    /// Register a callback for all incoming frames.
    void on_frame(FrameCallback cb);

    /// Register a callback for connection state changes.
    void on_state_change(StateCallback cb);

    /// Set a bearer token for authentication.
    void set_auth_token(std::string token);

    /// Enable or disable automatic reconnection.
    void set_auto_reconnect(bool enabled, uint32_t delay_ms = 1000,
                            uint32_t max_attempts = 10);

    /// Return true if the client is connected.
    [[nodiscard]] auto is_connected() const noexcept -> bool;

private:
    using WsStream = websocket::stream<beast::tcp_stream>;

    auto read_loop() -> awaitable<void>;
    auto try_reconnect() -> awaitable<void>;
    void notify_frame(const Frame& frame);
    void notify_state(bool connected);

    net::io_context& ioc_;
    std::unique_ptr<WsStream> ws_;

    std::string host_;
    std::string port_;
    std::string path_;
    std::string auth_token_;

    std::vector<FrameCallback> frame_callbacks_;
    std::vector<StateCallback> state_callbacks_;

    // Pending RPC calls awaiting a response, keyed by request id.
    struct PendingCall {
        std::function<void(Result<json>)> resolver;
    };
    std::unordered_map<std::string, PendingCall> pending_calls_;

    std::atomic<bool> connected_{false};
    bool auto_reconnect_ = false;
    uint32_t reconnect_delay_ms_ = 1000;
    uint32_t reconnect_max_attempts_ = 10;
};

} // namespace openclaw::gateway
