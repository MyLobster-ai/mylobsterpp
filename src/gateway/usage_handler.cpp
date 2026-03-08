#include "openclaw/gateway/usage_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_usage_handlers(Protocol& protocol,
                             [[maybe_unused]] sessions::SessionManager& sessions) {
    // usage.status — get aggregate usage summary.
    protocol.register_method("usage.status",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"totalInputTokens", 0},
                {"totalOutputTokens", 0},
                {"totalCacheReadTokens", 0},
                {"totalCacheWriteTokens", 0},
                {"totalRequests", 0},
            };
        },
        "Get aggregate usage summary", "usage");

    // usage.cost — get cost breakdown.
    protocol.register_method("usage.cost",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto period = params.value("period", "current");
            co_return json{
                {"ok", true},
                {"period", period},
                {"totalCostUsd", 0.0},
                {"breakdown", json::array()},
            };
        },
        "Get usage cost breakdown", "usage");

    // sessions.usage — get per-session usage.
    protocol.register_method("sessions.usage",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto session_id = params.value("sessionId", "");
            co_return json{
                {"ok", true},
                {"sessionId", session_id},
                {"inputTokens", 0},
                {"outputTokens", 0},
                {"cacheReadTokens", 0},
                {"cacheWriteTokens", 0},
            };
        },
        "Get per-session usage statistics", "usage");

    // sessions.usage.logs — get detailed usage logs.
    protocol.register_method("sessions.usage.logs",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto session_id = params.value("sessionId", "");
            [[maybe_unused]] auto limit = params.value("limit", 50);
            co_return json{
                {"ok", true},
                {"logs", json::array()},
                {"sessionId", session_id},
            };
        },
        "Get detailed usage logs", "usage");

    // sessions.usage.timeseries — get usage over time.
    protocol.register_method("sessions.usage.timeseries",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto granularity = params.value("granularity", "hour");
            co_return json{
                {"ok", true},
                {"granularity", granularity},
                {"data", json::array()},
            };
        },
        "Get usage time series data", "usage");

    // models.list — list available LLM models across all providers.
    protocol.register_method("models.list",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"models", json::array({
                    json{{"id", "claude-sonnet-4-6"}, {"provider", "anthropic"}, {"displayName", "Claude Sonnet 4.6"}},
                    json{{"id", "claude-opus-4-6"}, {"provider", "anthropic"}, {"displayName", "Claude Opus 4.6"}},
                    json{{"id", "claude-haiku-4-5-20251001"}, {"provider", "anthropic"}, {"displayName", "Claude Haiku 4.5"}},
                    json{{"id", "gpt-4o"}, {"provider", "openai"}, {"displayName", "GPT-4o"}},
                    json{{"id", "gemini-2.0-flash"}, {"provider", "gemini"}, {"displayName", "Gemini 2.0 Flash"}},
                })},
            };
        },
        "List available LLM models", "models");

    LOG_INFO("Registered usage handlers");
}

} // namespace openclaw::gateway
