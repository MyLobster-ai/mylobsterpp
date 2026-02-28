#include "openclaw/infra/delivery_queue.hpp"

#include <algorithm>
#include <fstream>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::infra {

// JSON serialization for DeliveryPayload
void to_json(json& j, const DeliveryPayload& p) {
    j = json{
        {"text", p.text},
        {"attachments", json::array()},
        {"extra", p.extra},
    };
    for (const auto& a : p.attachments) {
        json aj;
        channels::to_json(aj, a);
        j["attachments"].push_back(aj);
    }
}

void from_json(const json& j, DeliveryPayload& p) {
    p.text = j.value("text", "");
    p.extra = j.value("extra", json::object());
    if (j.contains("attachments") && j["attachments"].is_array()) {
        for (const auto& aj : j["attachments"]) {
            channels::Attachment a;
            channels::from_json(aj, a);
            p.attachments.push_back(std::move(a));
        }
    }
}

// JSON serialization for QueuedDelivery
void to_json(json& j, const QueuedDelivery& d) {
    j = json{
        {"id", d.id},
        {"enqueued_at", std::chrono::duration_cast<std::chrono::milliseconds>(
            d.enqueued_at.time_since_epoch()).count()},
        {"channel", d.channel},
        {"to", d.to},
        {"account_id", d.account_id},
        {"payloads", d.payloads},
        {"retry_count", d.retry_count},
    };
    if (d.last_error.has_value()) {
        j["last_error"] = *d.last_error;
    }
}

void from_json(const json& j, QueuedDelivery& d) {
    d.id = j.value("id", "");
    auto ms = j.value("enqueued_at", int64_t{0});
    d.enqueued_at = Clock::time_point{std::chrono::milliseconds{ms}};
    d.channel = j.value("channel", "");
    d.to = j.value("to", "");
    d.account_id = j.value("account_id", "");
    d.retry_count = j.value("retry_count", 0);
    if (j.contains("last_error") && !j["last_error"].is_null()) {
        d.last_error = j.value("last_error", "");
    }
    if (j.contains("payloads") && j["payloads"].is_array()) {
        d.payloads = j["payloads"].get<std::vector<DeliveryPayload>>();
    }
}

DeliveryQueue::DeliveryQueue(std::filesystem::path base_dir)
    : base_dir_(std::move(base_dir))
    , failed_dir_(base_dir_ / "failed")
{
    std::filesystem::create_directories(base_dir_);
    std::filesystem::create_directories(failed_dir_);
    LOG_INFO("Delivery queue initialized at {}", base_dir_.string());
}

auto DeliveryQueue::delivery_path(std::string_view id) const
    -> std::filesystem::path {
    return base_dir_ / (std::string(id) + ".json");
}

auto DeliveryQueue::write_delivery(const QueuedDelivery& delivery) -> VoidResult {
    auto path = delivery_path(delivery.id);
    auto tmp_path = std::filesystem::path(path.string() + ".tmp");

    try {
        json j = delivery;
        std::ofstream out(tmp_path);
        if (!out.is_open()) {
            return std::unexpected(make_error(
                ErrorCode::IoError,
                "Failed to open temp file for delivery",
                tmp_path.string()));
        }
        out << j.dump(2);
        out.close();

        // Atomic rename
        std::filesystem::rename(tmp_path, path);
        return {};
    } catch (const std::exception& e) {
        // Clean up temp file
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Failed to write delivery file",
            e.what()));
    }
}

auto DeliveryQueue::enqueue(QueuedDelivery delivery) -> Result<std::string> {
    if (delivery.id.empty()) {
        delivery.id = utils::generate_uuid();
    }
    if (delivery.enqueued_at == Clock::time_point{}) {
        delivery.enqueued_at = Clock::now();
    }

    auto result = write_delivery(delivery);
    if (!result) {
        return std::unexpected(result.error());
    }

    LOG_DEBUG("Enqueued delivery {} to {}:{}", delivery.id, delivery.channel, delivery.to);
    return delivery.id;
}

auto DeliveryQueue::ack(std::string_view id) -> VoidResult {
    auto path = delivery_path(id);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Delivery not found",
            std::string(id)));
    }

    if (!std::filesystem::remove(path, ec)) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Failed to remove delivery file",
            ec.message()));
    }

    LOG_DEBUG("Acked delivery {}", id);
    return {};
}

auto DeliveryQueue::fail(std::string_view id, std::string_view error) -> VoidResult {
    auto path = delivery_path(id);

    if (!std::filesystem::exists(path)) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Delivery not found",
            std::string(id)));
    }

    // Read and update the delivery
    try {
        std::ifstream in(path);
        auto j = json::parse(in);
        auto delivery = j.get<QueuedDelivery>();

        delivery.retry_count++;
        delivery.last_error = std::string(error);

        if (delivery.retry_count >= kMaxRetries) {
            // Move to failed/
            auto failed_path = failed_dir_ / (std::string(id) + ".json");
            json failed_j = delivery;
            std::ofstream out(failed_path);
            out << failed_j.dump(2);
            out.close();

            std::filesystem::remove(path);
            LOG_WARN("Delivery {} moved to failed after {} retries: {}",
                     id, delivery.retry_count, error);
        } else {
            // Update in place
            write_delivery(delivery);
            LOG_DEBUG("Delivery {} failed (retry {}/{}): {}",
                      id, delivery.retry_count, kMaxRetries, error);
        }

        return {};
    } catch (const std::exception& e) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Failed to update delivery",
            e.what()));
    }
}

auto DeliveryQueue::load_pending() -> std::vector<QueuedDelivery> {
    std::vector<QueuedDelivery> pending;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(base_dir_, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        if (entry.path().filename().string().ends_with(".tmp")) continue;

        try {
            std::ifstream in(entry.path());
            auto j = json::parse(in);
            pending.push_back(j.get<QueuedDelivery>());
        } catch (const std::exception& e) {
            LOG_WARN("Failed to load delivery {}: {}",
                     entry.path().string(), e.what());
        }
    }

    // Sort oldest-first
    std::sort(pending.begin(), pending.end(),
              [](const QueuedDelivery& a, const QueuedDelivery& b) {
                  return a.enqueued_at < b.enqueued_at;
              });

    LOG_DEBUG("Loaded {} pending deliveries", pending.size());
    return pending;
}

auto DeliveryQueue::drain_pending(SendFunction send_fn) -> size_t {
    auto pending = load_pending();
    size_t sent = 0;
    auto now = Clock::now();

    for (auto& delivery : pending) {
        // Check backoff eligibility
        auto delay = backoff_delay(delivery.retry_count);
        if (now < delivery.enqueued_at + delay) {
            // v2026.2.26: CONTINUE, not break.
            // This is the head-of-line blocking fix: a single entry with
            // remaining backoff should not prevent processing of subsequent
            // entries that may be ready.
            continue;
        }

        // Attempt resend
        bool success = false;
        try {
            success = send_fn(delivery);
        } catch (const std::exception& e) {
            LOG_WARN("drain_pending: send threw for {}: {}", delivery.id, e.what());
        }

        if (success) {
            // Normalize legacy entries: clear stale last_error on success
            (void)ack(delivery.id);
            ++sent;
        } else {
            (void)fail(delivery.id, "drain_pending: send failed");
        }
    }

    if (sent > 0) {
        LOG_INFO("drain_pending: successfully sent {} of {} pending deliveries",
                 sent, pending.size());
    }

    return sent;
}

auto DeliveryQueue::backoff_delay(int retry_count) -> std::chrono::seconds {
    if (retry_count <= 0) return std::chrono::seconds{0};
    int idx = std::min(retry_count - 1, static_cast<int>(kBackoffSeconds.size()) - 1);
    return std::chrono::seconds{kBackoffSeconds[static_cast<size_t>(idx)]};
}

} // namespace openclaw::infra
