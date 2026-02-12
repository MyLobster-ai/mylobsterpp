#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::browser {

using boost::asio::awaitable;
using json = nlohmann::json;

/// Callback type for CDP event subscriptions.
using EventHandler = std::function<void(json)>;

/// Chrome DevTools Protocol WebSocket client.
/// Communicates with a Chrome/Chromium instance over the CDP WebSocket interface.
class CdpClient {
public:
    explicit CdpClient(boost::asio::io_context& ioc);
    ~CdpClient();

    CdpClient(const CdpClient&) = delete;
    CdpClient& operator=(const CdpClient&) = delete;
    CdpClient(CdpClient&&) noexcept;
    CdpClient& operator=(CdpClient&&) noexcept;

    /// Connect to the Chrome DevTools WebSocket endpoint.
    auto connect(std::string_view ws_url) -> awaitable<Result<void>>;

    /// Send a CDP command and await its result.
    /// method: CDP method name (e.g. "Page.navigate", "Runtime.evaluate").
    /// params: optional JSON parameters for the command.
    auto send_command(std::string_view method, json params = {})
        -> awaitable<Result<json>>;

    /// Subscribe to a CDP event. The handler is called each time the event fires.
    void subscribe(std::string_view event, EventHandler handler);

    /// Unsubscribe from a CDP event.
    void unsubscribe(std::string_view event);

    /// Disconnect from the Chrome DevTools endpoint.
    auto disconnect() -> awaitable<void>;

    /// Returns true if the client is currently connected.
    [[nodiscard]] auto is_connected() const -> bool;

    /// Get the WebSocket URL this client is connected to.
    [[nodiscard]] auto ws_url() const -> std::string_view;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openclaw::browser
