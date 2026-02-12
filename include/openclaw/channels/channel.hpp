#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/channels/message.hpp"

namespace openclaw::channels {

/// Abstract base class for all channel implementations.
/// Each channel (Telegram, Discord, Slack, etc.) derives from this
/// and implements platform-specific start/stop/send logic.
class Channel {
public:
    virtual ~Channel() = default;

    /// Starts the channel (opens connections, begins polling, etc.).
    virtual auto start() -> boost::asio::awaitable<void> = 0;

    /// Stops the channel gracefully (closes connections, cancels polling).
    virtual auto stop() -> boost::asio::awaitable<void> = 0;

    /// Sends a message through this channel.
    virtual auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> = 0;

    /// Returns the instance name of this channel (e.g. "my-telegram-bot").
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /// Returns the channel type identifier (e.g. "telegram", "discord").
    [[nodiscard]] virtual auto type() const -> std::string_view = 0;

    /// Returns whether the channel is currently running.
    [[nodiscard]] virtual auto is_running() const noexcept -> bool = 0;

    /// Callback type invoked when a message is received on this channel.
    using MessageCallback = std::function<void(IncomingMessage)>;

    /// Registers a callback for incoming messages.
    void set_on_message(MessageCallback cb) { on_message_ = std::move(cb); }

protected:
    /// Dispatches an incoming message to the registered callback.
    void dispatch(IncomingMessage msg) {
        if (on_message_) {
            on_message_(std::move(msg));
        }
    }

    MessageCallback on_message_;
};

} // namespace openclaw::channels
