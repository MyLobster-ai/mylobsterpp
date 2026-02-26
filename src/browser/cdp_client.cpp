#include "openclaw/browser/cdp_client.hpp"
#include "openclaw/core/logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace openclaw::browser {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// ---------------------------------------------------------------------------
// Pending command: a promise/future pair for awaiting CDP responses
// ---------------------------------------------------------------------------

struct PendingCommand {
    net::any_io_executor executor;
    std::function<void(Result<json>)> callback;
};

// ---------------------------------------------------------------------------
// CdpClient::Impl
// ---------------------------------------------------------------------------

struct CdpClient::Impl {
    net::io_context& ioc;
    std::unique_ptr<websocket::stream<beast::tcp_stream>> ws;
    std::string url;
    std::atomic<bool> connected{false};
    std::atomic<int> next_id{1};

    std::mutex pending_mutex;
    std::unordered_map<int, PendingCommand> pending_commands;

    std::mutex handler_mutex;
    std::unordered_map<std::string, EventHandler> event_handlers;

    explicit Impl(net::io_context& ctx) : ioc(ctx) {}

    auto allocate_id() -> int {
        return next_id.fetch_add(1, std::memory_order_relaxed);
    }

    void dispatch_message(const std::string& msg) {
        try {
            auto j = json::parse(msg);

            // Check if this is a response to a command (has "id" field)
            if (j.contains("id")) {
                int id = j["id"].get<int>();

                std::function<void(Result<json>)> callback;
                {
                    std::lock_guard lock(pending_mutex);
                    auto it = pending_commands.find(id);
                    if (it != pending_commands.end()) {
                        callback = std::move(it->second.callback);
                        pending_commands.erase(it);
                    }
                }

                if (callback) {
                    if (j.contains("error")) {
                        auto& err = j["error"];
                        callback(std::unexpected(
                            make_error(ErrorCode::BrowserError,
                                       err.value("message", "CDP error"),
                                       err.contains("data")
                                           ? err["data"].dump()
                                           : "")));
                    } else {
                        callback(j.value("result", json::object()));
                    }
                }
                return;
            }

            // Otherwise, this is an event notification
            if (j.contains("method")) {
                auto method = j["method"].get<std::string>();
                EventHandler handler;
                {
                    std::lock_guard lock(handler_mutex);
                    auto it = event_handlers.find(method);
                    if (it != event_handlers.end()) {
                        handler = it->second;
                    }
                }
                if (handler) {
                    handler(j.value("params", json::object()));
                }
            }
        } catch (const json::exception& e) {
            LOG_WARN("Failed to parse CDP message: {}", e.what());
        }
    }
};

// ---------------------------------------------------------------------------
// CdpClient
// ---------------------------------------------------------------------------

CdpClient::CdpClient(boost::asio::io_context& ioc)
    : impl_(std::make_unique<Impl>(ioc)) {}

CdpClient::~CdpClient() {
    if (impl_ && impl_->connected) {
        // Best-effort close
        if (impl_->ws) {
            beast::error_code ec;
            impl_->ws->close(websocket::close_code::normal, ec);
        }
        impl_->connected = false;
    }
}

CdpClient::CdpClient(CdpClient&&) noexcept = default;
CdpClient& CdpClient::operator=(CdpClient&&) noexcept = default;

auto CdpClient::connect(std::string_view ws_url) -> awaitable<Result<void>> {
    impl_->url = std::string(ws_url);

    try {
        // Parse the WebSocket URL: ws://host:port/path
        std::string url_str(ws_url);
        std::string host;
        std::string port;
        std::string target;

        // Strip ws:// prefix
        size_t start = 0;
        if (url_str.starts_with("ws://")) {
            start = 5;
        } else if (url_str.starts_with("wss://")) {
            start = 6;
        }

        auto path_pos = url_str.find('/', start);
        auto host_port = url_str.substr(start, path_pos - start);
        target = (path_pos != std::string::npos) ? url_str.substr(path_pos) : "/";

        auto colon_pos = host_port.find(':');
        if (colon_pos != std::string::npos) {
            host = host_port.substr(0, colon_pos);
            port = host_port.substr(colon_pos + 1);
        } else {
            host = host_port;
            port = "9222";
        }

        // Resolve the host
        tcp::resolver resolver(impl_->ioc);
        auto results = co_await resolver.async_resolve(
            host, port, net::use_awaitable);

        // Create the WebSocket stream
        impl_->ws = std::make_unique<websocket::stream<beast::tcp_stream>>(
            impl_->ioc);

        // Set timeouts
        beast::get_lowest_layer(*impl_->ws).expires_after(
            std::chrono::seconds(30));

        // Connect TCP
        auto ep = co_await beast::get_lowest_layer(*impl_->ws).async_connect(
            results, net::use_awaitable);

        // Update host string for the handshake
        auto host_str = host + ":" + std::to_string(ep.port());

        // Remove timeout for WebSocket (it has its own ping/pong)
        beast::get_lowest_layer(*impl_->ws).expires_never();

        // Set WebSocket options
        impl_->ws->set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::client));

        impl_->ws->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent,
                        "openclaw-cdp/1.0");
            }));

        // Perform WebSocket handshake
        co_await impl_->ws->async_handshake(host_str, target,
                                            net::use_awaitable);

        impl_->connected = true;
        LOG_INFO("CDP connected to {}", url_str);

        // Start the read loop in the background
        net::co_spawn(impl_->ioc,
            [this]() -> awaitable<void> {
                beast::flat_buffer buffer;
                while (impl_->connected) {
                    try {
                        co_await impl_->ws->async_read(buffer,
                                                       net::use_awaitable);
                        auto msg = beast::buffers_to_string(buffer.data());
                        buffer.consume(buffer.size());
                        impl_->dispatch_message(msg);
                    } catch (const beast::system_error& se) {
                        if (se.code() != websocket::error::closed) {
                            LOG_ERROR("CDP read error: {}", se.what());
                        }
                        impl_->connected = false;
                        break;
                    }
                }

                // Fail all pending commands
                {
                    std::lock_guard lock(impl_->pending_mutex);
                    for (auto& [id, cmd] : impl_->pending_commands) {
                        cmd.callback(std::unexpected(
                            make_error(ErrorCode::ConnectionClosed,
                                       "CDP connection closed")));
                    }
                    impl_->pending_commands.clear();
                }
            },
            net::detached);

        co_return ok_result();
    } catch (const beast::system_error& se) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionFailed,
                       "Failed to connect to CDP",
                       se.what()));
    } catch (const std::exception& e) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionFailed,
                       "Failed to connect to CDP",
                       e.what()));
    }
}

auto CdpClient::send_command(std::string_view method, json params)
    -> awaitable<Result<json>> {
    if (!impl_->connected) {
        co_return make_fail(
            make_error(ErrorCode::ConnectionClosed,
                       "CDP client not connected"));
    }

    int id = impl_->allocate_id();

    json message = {
        {"id", id},
        {"method", std::string(method)},
        {"params", std::move(params)},
    };

    // Create a channel to receive the result
    using channel_t = net::experimental::concurrent_channel<void(
        boost::system::error_code, Result<json>)>;
    auto channel = std::make_shared<channel_t>(impl_->ioc, 1);

    // Register the pending command
    {
        std::lock_guard lock(impl_->pending_mutex);
        impl_->pending_commands[id] = PendingCommand{
            co_await net::this_coro::executor,
            [channel](Result<json> result) {
                channel->try_send(boost::system::error_code{},
                                  std::move(result));
            },
        };
    }

    // Send the message
    try {
        auto msg_str = message.dump();
        co_await impl_->ws->async_write(
            net::buffer(msg_str), net::use_awaitable);
    } catch (const beast::system_error& se) {
        // Remove the pending command
        {
            std::lock_guard lock(impl_->pending_mutex);
            impl_->pending_commands.erase(id);
        }
        co_return make_fail(
            make_error(ErrorCode::ConnectionFailed,
                       "Failed to send CDP command",
                       se.what()));
    }

    // Wait for the response
    auto result = co_await channel->async_receive(net::use_awaitable);
    co_return result;
}

void CdpClient::subscribe(std::string_view event, EventHandler handler) {
    std::lock_guard lock(impl_->handler_mutex);
    impl_->event_handlers[std::string(event)] = std::move(handler);
    LOG_DEBUG("Subscribed to CDP event: {}", std::string(event));
}

void CdpClient::unsubscribe(std::string_view event) {
    std::lock_guard lock(impl_->handler_mutex);
    impl_->event_handlers.erase(std::string(event));
    LOG_DEBUG("Unsubscribed from CDP event: {}", std::string(event));
}

auto CdpClient::disconnect() -> awaitable<void> {
    if (!impl_->connected) {
        co_return;
    }

    impl_->connected = false;

    if (impl_->ws) {
        try {
            co_await impl_->ws->async_close(websocket::close_code::normal,
                                            net::use_awaitable);
        } catch (const beast::system_error&) {
            // Ignore close errors
        }
    }

    LOG_INFO("CDP disconnected from {}", impl_->url);
}

auto CdpClient::is_connected() const -> bool {
    return impl_ && impl_->connected;
}

auto CdpClient::ws_url() const -> std::string_view {
    return impl_->url;
}

} // namespace openclaw::browser
