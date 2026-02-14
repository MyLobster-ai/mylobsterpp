#include "openclaw/channels/delivered_channel.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::channels {

DeliveredChannel::DeliveredChannel(std::unique_ptr<Channel> inner,
                                    std::shared_ptr<infra::DeliveryQueue> queue,
                                    std::shared_ptr<gateway::HookRegistry> hooks)
    : inner_(std::move(inner))
    , queue_(std::move(queue))
    , hooks_(std::move(hooks))
{
    // Forward message callback from inner channel
    inner_->set_on_message([this](IncomingMessage msg) {
        dispatch(std::move(msg));
    });
}

auto DeliveredChannel::start() -> boost::asio::awaitable<void> {
    co_await inner_->start();
}

auto DeliveredChannel::stop() -> boost::asio::awaitable<void> {
    co_await inner_->stop();
}

auto DeliveredChannel::send(OutgoingMessage msg)
    -> boost::asio::awaitable<openclaw::Result<void>> {

    // 1. Run message_sending hook (can cancel or modify)
    if (hooks_) {
        json hook_ctx;
        hook_ctx["channel"] = std::string(inner_->name());
        hook_ctx["to"] = msg.recipient_id;
        hook_ctx["content"] = msg.text;

        auto hooked = co_await hooks_->run_before("message_sending", hook_ctx);

        // Check for cancel
        if (hooked.value("cancel", false)) {
            LOG_DEBUG("Message to {} cancelled by message_sending hook", msg.recipient_id);
            co_return Result<void>{};
        }

        // Check for content modification
        if (hooked.contains("content") && hooked["content"].is_string()) {
            msg.text = hooked["content"].get<std::string>();
        }
    }

    // 2. Enqueue for persistence
    std::string delivery_id;
    if (queue_) {
        infra::QueuedDelivery delivery;
        delivery.id = utils::generate_uuid();
        delivery.enqueued_at = std::chrono::system_clock::now();
        delivery.channel = std::string(inner_->name());
        delivery.to = msg.recipient_id;

        infra::DeliveryPayload payload;
        payload.text = msg.text;
        payload.attachments = msg.attachments;
        payload.extra = msg.extra;
        delivery.payloads.push_back(std::move(payload));

        auto enqueue_result = queue_->enqueue(std::move(delivery));
        if (enqueue_result) {
            delivery_id = *enqueue_result;
        } else {
            LOG_WARN("Failed to enqueue delivery: {}", enqueue_result.error().what());
        }
    }

    // 3. Call inner channel send
    auto send_result = co_await inner_->send(std::move(msg));

    // 4. Ack or fail the delivery
    if (queue_ && !delivery_id.empty()) {
        if (send_result) {
            queue_->ack(delivery_id);
        } else {
            queue_->fail(delivery_id, send_result.error().what());
        }
    }

    // 5. Run message_sent hook
    if (hooks_) {
        json hook_ctx;
        hook_ctx["channel"] = std::string(inner_->name());
        hook_ctx["success"] = send_result.has_value();
        if (!send_result) {
            hook_ctx["error"] = send_result.error().what();
        }
        co_await hooks_->run_after("message_sent", hook_ctx);
    }

    co_return send_result;
}

auto DeliveredChannel::name() const -> std::string_view {
    return inner_->name();
}

auto DeliveredChannel::type() const -> std::string_view {
    return inner_->type();
}

auto DeliveredChannel::is_running() const noexcept -> bool {
    return inner_->is_running();
}

} // namespace openclaw::channels
