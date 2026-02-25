#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

/// Tracks per-connection unauthorized request floods and terminates
/// connections that exceed a configurable threshold. Implements
/// sampled logging to avoid log spam during active flood attacks.
class UnauthorizedFloodGuard {
public:
    /// Maximum consecutive unauthorized requests before closing the connection.
    static constexpr int kDefaultFloodThreshold = 50;

    /// Log sampling interval: log every Nth rejection during a flood.
    static constexpr int kLogSampleInterval = 10;

    explicit UnauthorizedFloodGuard(int threshold = kDefaultFloodThreshold)
        : threshold_(threshold) {}

    /// Record an unauthorized request. Returns true if the connection
    /// should be closed (flood threshold exceeded).
    auto record_rejection() -> bool {
        int count = ++rejection_count_;

        // Sampled logging to avoid log spam
        if (count == 1 || count % kLogSampleInterval == 0) {
            LOG_WARN("FloodGuard: {} consecutive unauthorized requests", count);
        }

        return count >= threshold_;
    }

    /// Reset the rejection counter (e.g., after successful auth).
    void reset() { rejection_count_ = 0; }

    /// Current rejection count.
    [[nodiscard]] auto count() const noexcept -> int { return rejection_count_.load(); }

    /// Whether the flood threshold has been exceeded.
    [[nodiscard]] auto is_flooded() const noexcept -> bool {
        return rejection_count_.load() >= threshold_;
    }

private:
    int threshold_;
    std::atomic<int> rejection_count_{0};
};

} // namespace openclaw::gateway
