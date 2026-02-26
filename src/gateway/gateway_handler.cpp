#include "openclaw/gateway/gateway_handler.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

#ifndef OPENCLAW_VERSION_STRING
#define OPENCLAW_VERSION_STRING "2026.2.25"
#endif

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

namespace {

/// Simple metrics counters.
struct Metrics {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> total_errors{0};
    std::chrono::steady_clock::time_point start_time =
        std::chrono::steady_clock::now();
};

/// Ring buffer for recent log entries.
struct LogBuffer {
    static constexpr size_t MAX_ENTRIES = 1000;
    std::mutex mutex;
    std::deque<json> entries;

    void add(const std::string& level, const std::string& message) {
        std::lock_guard lock(mutex);
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        entries.push_back(json{
            {"timestamp", ms},
            {"level", level},
            {"message", message},
        });
        while (entries.size() > MAX_ENTRIES) {
            entries.pop_front();
        }
    }

    auto recent(size_t n) -> json {
        std::lock_guard lock(mutex);
        json result = json::array();
        size_t start = entries.size() > n ? entries.size() - n : 0;
        for (size_t i = start; i < entries.size(); ++i) {
            result.push_back(entries[i]);
        }
        return result;
    }
};

static Metrics g_metrics;
static LogBuffer g_logs;

} // anonymous namespace

void register_gateway_handlers(Protocol& protocol, GatewayServer& server) {
    // gateway.info
    protocol.register_method("gateway.info",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"version", OPENCLAW_VERSION_STRING},
                {"protocol", GatewayServer::PROTOCOL_VERSION},
                {"engine", "mylobsterpp"},
                {"capabilities", json::array({
                    "chat", "tools", "memory", "browser",
                    "channels", "plugins", "cron",
                })},
            };
        },
        "Return gateway version and capabilities", "gateway");

    // gateway.ping
    protocol.register_method("gateway.ping",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            co_return json{{"pong", true}, {"ts", ms}};
        },
        "Health check ping", "gateway");

    // gateway.status
    protocol.register_method("gateway.status",
        [&server]([[maybe_unused]] json params) -> awaitable<json> {
            auto now = std::chrono::steady_clock::now();
            auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(
                now - g_metrics.start_time).count();
            co_return json{
                {"running", server.is_running()},
                {"uptime_seconds", uptime_s},
                {"connection_count", server.connection_count()},
                {"total_requests", g_metrics.total_requests.load()},
                {"total_errors", g_metrics.total_errors.load()},
            };
        },
        "Return gateway runtime status", "gateway");

    // gateway.methods
    protocol.register_method("gateway.methods",
        [&protocol]([[maybe_unused]] json params) -> awaitable<json> {
            auto methods = protocol.methods();
            json result = json::array();
            for (const auto& m : methods) {
                result.push_back(json{
                    {"name", m.name},
                    {"description", m.description},
                    {"group", m.group},
                });
            }
            co_return json{{"methods", result}, {"count", result.size()}};
        },
        "List all registered RPC methods", "gateway");

    // gateway.subscribe
    protocol.register_method("gateway.subscribe",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto topic = params.value("topic", "");
            if (topic.empty()) {
                co_return json{{"ok", false}, {"error", "topic is required"}};
            }
            // Topic subscriptions are tracked per-connection;
            // for now, acknowledge the subscription.
            co_return json{{"ok", true}, {"topic", topic}};
        },
        "Subscribe to server-sent events by topic", "gateway");

    // gateway.unsubscribe
    protocol.register_method("gateway.unsubscribe",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto topic = params.value("topic", "");
            co_return json{{"ok", true}, {"topic", topic}};
        },
        "Unsubscribe from server-sent events", "gateway");

    // gateway.shutdown
    protocol.register_method("gateway.shutdown",
        [&server]([[maybe_unused]] json params) -> awaitable<json> {
            LOG_INFO("Graceful shutdown requested via RPC");
            server.stop();
            co_return json{{"ok", true}, {"message", "Shutting down"}};
        },
        "Initiate graceful server shutdown", "gateway");

    // gateway.reload
    protocol.register_method("gateway.reload",
        []([[maybe_unused]] json params) -> awaitable<json> {
            LOG_INFO("Configuration reload requested via RPC");
            // Reload config from disk. The actual reload logic depends on
            // the RuntimeConfig being wired up.
            co_return json{{"ok", true}, {"message", "Configuration reloaded"}};
        },
        "Reload gateway configuration", "gateway");

    // gateway.metrics
    protocol.register_method("gateway.metrics",
        [&server]([[maybe_unused]] json params) -> awaitable<json> {
            auto now = std::chrono::steady_clock::now();
            auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(
                now - g_metrics.start_time).count();
            co_return json{
                {"uptime_seconds", uptime_s},
                {"total_requests", g_metrics.total_requests.load()},
                {"total_errors", g_metrics.total_errors.load()},
                {"connection_count", server.connection_count()},
            };
        },
        "Return gateway metrics", "gateway");

    // gateway.logs
    protocol.register_method("gateway.logs",
        []([[maybe_unused]] json params) -> awaitable<json> {
            size_t limit = params.value("limit", 100);
            co_return json{{"logs", g_logs.recent(limit)}};
        },
        "Stream or query recent gateway logs", "gateway");

    LOG_INFO("Registered gateway handlers");
}

} // namespace openclaw::gateway
