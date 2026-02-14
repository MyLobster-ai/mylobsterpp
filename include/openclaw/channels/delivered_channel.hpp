#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <boost/asio.hpp>

#include "openclaw/channels/channel.hpp"
#include "openclaw/gateway/hooks.hpp"
#include "openclaw/infra/delivery_queue.hpp"

namespace openclaw::channels {

/// Wrapper that adds delivery queue and hook integration to any channel.
///
/// Delegates start/stop/name/type/is_running to the inner channel.
/// Intercepts send() to:
///   1. Run message_sending hook (can cancel or modify)
///   2. Enqueue to delivery queue for persistence
///   3. Call inner channel send()
///   4. Ack or fail the delivery
///   5. Run message_sent hook
class DeliveredChannel : public Channel {
public:
    DeliveredChannel(std::unique_ptr<Channel> inner,
                     std::shared_ptr<infra::DeliveryQueue> queue,
                     std::shared_ptr<gateway::HookRegistry> hooks);

    auto start() -> boost::asio::awaitable<void> override;
    auto stop() -> boost::asio::awaitable<void> override;
    auto send(OutgoingMessage msg) -> boost::asio::awaitable<openclaw::Result<void>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto type() const -> std::string_view override;
    [[nodiscard]] auto is_running() const noexcept -> bool override;

    /// Access the inner channel.
    [[nodiscard]] auto inner() const -> const Channel& { return *inner_; }

private:
    std::unique_ptr<Channel> inner_;
    std::shared_ptr<infra::DeliveryQueue> queue_;
    std::shared_ptr<gateway::HookRegistry> hooks_;
};

} // namespace openclaw::channels
