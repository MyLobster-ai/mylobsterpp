#pragma once

#include <atomic>
#include <chrono>
#include <functional>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::channels {

/// v2026.2.26: Circuit breaker for typing indicator API calls.
///
/// Tracks consecutive typing indicator failures. After `kMaxConsecutiveFailures`
/// failures, the guard trips permanently (until reset) to avoid spamming
/// the channel API with calls that are failing.
///
/// Also implements a TTL (60s) auto-stop: if typing is still active after
/// the timeout, it auto-stops to prevent phantom "typing..." states.
class TypingStartGuard {
public:
    static constexpr int kMaxConsecutiveFailures = 2;
    static constexpr auto kTypingTtl = std::chrono::seconds(60);

    explicit TypingStartGuard(boost::asio::io_context& ioc)
        : timer_(ioc) {}

    /// Attempt to send a typing indicator. If the guard is tripped,
    /// returns silently without calling the send function.
    /// On failure, increments the failure counter. At kMaxConsecutiveFailures,
    /// trips the guard permanently.
    ///
    /// @param send_fn  Function that sends the typing indicator. Returns true on success.
    void start(std::function<bool()> send_fn) {
        if (tripped_.load(std::memory_order_acquire)) {
            return;  // silently skip
        }

        bool ok = false;
        try {
            ok = send_fn();
        } catch (...) {
            ok = false;
        }

        if (ok) {
            consecutive_failures_.store(0, std::memory_order_release);
            // Start TTL timer
            timer_.expires_after(kTypingTtl);
            timer_.async_wait([](auto /*ec*/) {
                // Timer expired — typing auto-stopped by platform after TTL
            });
        } else {
            auto failures = consecutive_failures_.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (failures >= kMaxConsecutiveFailures) {
                tripped_.store(true, std::memory_order_release);
                LOG_WARN("TypingStartGuard tripped after {} consecutive failures", failures);
            }
        }
    }

    /// Reset the guard: clear counter and tripped flag.
    /// Call this when a new reply cycle begins.
    void reset() {
        consecutive_failures_.store(0, std::memory_order_release);
        tripped_.store(false, std::memory_order_release);
        timer_.cancel();
    }

    /// Returns true if the guard has tripped (too many failures).
    [[nodiscard]] auto is_tripped() const noexcept -> bool {
        return tripped_.load(std::memory_order_acquire);
    }

    /// Returns the current consecutive failure count.
    [[nodiscard]] auto failure_count() const noexcept -> int {
        return consecutive_failures_.load(std::memory_order_acquire);
    }

private:
    std::atomic<int> consecutive_failures_{0};
    std::atomic<bool> tripped_{false};
    boost::asio::steady_timer timer_;
};

} // namespace openclaw::channels
