#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/core/error.hpp"
#include "openclaw/gateway/auth.hpp"
#include "openclaw/gateway/frame.hpp"
#include "openclaw/gateway/hooks.hpp"
#include "openclaw/gateway/protocol.hpp"
#include "openclaw/infra/device.hpp"

namespace openclaw::gateway {

using boost::asio::awaitable;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;

/// Represents a single connected WebSocket client session.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    using WsStream = websocket::stream<beast::tcp_stream>;

    Connection(WsStream ws, std::string id, std::shared_ptr<Protocol> protocol,
               std::shared_ptr<HookRegistry> hooks);

    /// Start reading frames from the client.
    auto run() -> awaitable<void>;

    /// Send a frame to this client.
    auto send(const Frame& frame) -> awaitable<Result<void>>;

    /// Send a raw string message.
    auto send_text(std::string message) -> awaitable<Result<void>>;

    /// Close the connection.
    auto close() -> awaitable<void>;

    [[nodiscard]] auto id() const noexcept -> const std::string& { return id_; }
    [[nodiscard]] auto is_open() const noexcept -> bool { return open_; }

    /// Authentication info set after successful auth.
    void set_auth(AuthInfo info);
    [[nodiscard]] auto auth() const noexcept -> const std::optional<AuthInfo>&;

    /// Scopes granted to this connection after device identity validation.
    void set_scopes(std::vector<std::string> scopes);
    [[nodiscard]] auto scopes() const noexcept -> const std::vector<std::string>&;

    /// Device public key (base64url-encoded raw Ed25519 key).
    void set_device_public_key(std::string key);
    [[nodiscard]] auto device_public_key() const noexcept -> const std::string&;

    /// Per-connection challenge nonce used in the connect handshake.
    void set_nonce(std::string nonce);
    [[nodiscard]] auto nonce() const noexcept -> const std::string&;

private:
    auto read_loop() -> awaitable<void>;
    auto handle_frame(const Frame& frame) -> awaitable<void>;
    auto handle_request(const RequestFrame& req) -> awaitable<void>;

    WsStream ws_;
    std::string id_;
    std::shared_ptr<Protocol> protocol_;
    std::shared_ptr<HookRegistry> hooks_;
    std::optional<AuthInfo> auth_info_;
    std::vector<std::string> scopes_;
    std::string device_public_key_;
    std::string connect_nonce_;
    std::atomic<bool> open_{true};
};

/// Callback type for new connection events.
using ConnectionCallback = std::function<void(std::shared_ptr<Connection>)>;

/// The GatewayServer listens on a TCP port, accepts WebSocket upgrade
/// requests, authenticates connections, and manages their lifecycle.
class GatewayServer {
public:
    /// Protocol version for the connect handshake.
    static constexpr int PROTOCOL_VERSION = 3;

    /// Maximum clock skew allowed for device signature timestamps (2 minutes).
    static constexpr int64_t DEVICE_SIGNATURE_SKEW_MS = 2 * 60 * 1000;

    explicit GatewayServer(net::io_context& ioc);

    /// Start the server with the given configuration.
    auto start(const GatewayConfig& config) -> awaitable<void>;

    /// Gracefully shut down the server, closing all connections.
    auto stop() -> awaitable<void>;

    /// Register a callback invoked for each new connection after auth.
    void on_connection(ConnectionCallback cb);

    /// Get the protocol registry (for registering methods externally).
    [[nodiscard]] auto protocol() -> std::shared_ptr<Protocol>;

    /// Get the hook registry.
    [[nodiscard]] auto hooks() -> std::shared_ptr<HookRegistry>;

    /// Get the authenticator.
    [[nodiscard]] auto authenticator() -> Authenticator&;

    /// Broadcast an event to all connected clients.
    auto broadcast(const EventFrame& event) -> awaitable<void>;

    /// Return current number of active connections.
    [[nodiscard]] auto connection_count() const noexcept -> size_t;

    /// Return true if the server is running.
    [[nodiscard]] auto is_running() const noexcept -> bool;

    /// Validates an avatar file path: checks canonical containment, symlink
    /// rejection, and 2MB size limit.
    [[nodiscard]] static auto validate_avatar_path(
        const std::filesystem::path& path,
        const std::filesystem::path& root) -> Result<void>;

private:
    auto accept_loop(tcp::acceptor& acceptor) -> awaitable<void>;
    auto handle_connection(tcp::socket socket) -> awaitable<void>;

    void add_connection(std::shared_ptr<Connection> conn);
    void remove_connection(const std::string& id);

    net::io_context& ioc_;
    std::shared_ptr<Protocol> protocol_;
    std::shared_ptr<HookRegistry> hooks_;
    Authenticator authenticator_;
    GatewayConfig config_;

    std::unordered_map<std::string, std::shared_ptr<Connection>> connections_;
    std::vector<ConnectionCallback> connection_callbacks_;

    std::atomic<bool> running_{false};
    size_t max_connections_ = 100;
};

} // namespace openclaw::gateway
