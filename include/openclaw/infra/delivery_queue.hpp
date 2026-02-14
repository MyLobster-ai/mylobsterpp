#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "openclaw/channels/message.hpp"
#include "openclaw/core/error.hpp"

namespace openclaw::infra {

using json = nlohmann::json;
using Clock = std::chrono::system_clock;

/// A single payload within a queued delivery.
struct DeliveryPayload {
    std::string text;
    std::vector<channels::Attachment> attachments;
    json extra;
};

void to_json(json& j, const DeliveryPayload& p);
void from_json(const json& j, DeliveryPayload& p);

/// A queued delivery entry persisted to disk.
struct QueuedDelivery {
    std::string id;
    std::chrono::system_clock::time_point enqueued_at;
    std::string channel;
    std::string to;
    std::string account_id;
    std::vector<DeliveryPayload> payloads;
    int retry_count = 0;
    std::optional<std::string> last_error;
};

void to_json(json& j, const QueuedDelivery& d);
void from_json(const json& j, QueuedDelivery& d);

/// File-based write-ahead delivery queue.
///
/// Persists delivery entries as JSON files in ~/.openclaw/delivery-queue/
/// to survive crashes. Uses atomic write (write .tmp then rename) for safety.
class DeliveryQueue {
public:
    /// Maximum number of retry attempts before moving to failed/.
    static constexpr int kMaxRetries = 5;

    /// Backoff schedule in seconds for retries 1-4.
    static constexpr std::array<int, 4> kBackoffSeconds = {5, 25, 120, 600};

    /// Construct with a base directory for queue persistence.
    explicit DeliveryQueue(std::filesystem::path base_dir);

    /// Enqueue a new delivery and persist to disk.
    auto enqueue(QueuedDelivery delivery) -> Result<std::string>;

    /// Acknowledge successful delivery, removing the file.
    auto ack(std::string_view id) -> VoidResult;

    /// Mark a delivery as failed. If max retries exceeded, moves to failed/.
    auto fail(std::string_view id, std::string_view error) -> VoidResult;

    /// Load all pending deliveries, sorted oldest-first by enqueued_at.
    auto load_pending() -> std::vector<QueuedDelivery>;

    /// Compute the backoff delay for a given retry count.
    static auto backoff_delay(int retry_count) -> std::chrono::seconds;

    /// Return the queue base directory.
    [[nodiscard]] auto base_dir() const -> const std::filesystem::path& { return base_dir_; }

private:
    auto delivery_path(std::string_view id) const -> std::filesystem::path;
    auto write_delivery(const QueuedDelivery& delivery) -> VoidResult;

    std::filesystem::path base_dir_;
    std::filesystem::path failed_dir_;
};

} // namespace openclaw::infra
