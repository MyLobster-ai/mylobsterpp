#include "openclaw/context_engine/context_engine.hpp"
#include "openclaw/core/logger.hpp"

namespace openclaw::context_engine {

LegacyContextEngine::LegacyContextEngine() {
    // Default hooks that pass through — legacy compaction behavior
    hooks_.bootstrap = []() -> awaitable<Result<void>> {
        LOG_DEBUG("Legacy context engine: bootstrap (no-op)");
        co_return Result<void>{};
    };

    hooks_.ingest = [](const json& /*content*/) -> awaitable<Result<void>> {
        // Legacy mode: content is appended directly to message history
        co_return Result<void>{};
    };

    hooks_.assemble = []() -> awaitable<Result<json>> {
        // Legacy mode: context assembly handled by agent runtime
        co_return json::object();
    };

    hooks_.compact = [](const json& current_context) -> awaitable<Result<json>> {
        // Legacy mode: standard summarization-based compaction
        LOG_DEBUG("Legacy context engine: compact pass-through");
        co_return current_context;
    };

    hooks_.after_turn = [](const json& /*turn_result*/) -> awaitable<Result<void>> {
        co_return Result<void>{};
    };

    hooks_.prepare_subagent_spawn = [](const json& spawn_config) -> awaitable<Result<json>> {
        // Legacy: pass spawn config through unchanged
        co_return spawn_config;
    };

    hooks_.on_subagent_ended = [](const json& /*subagent_result*/) -> awaitable<Result<void>> {
        co_return Result<void>{};
    };
}

} // namespace openclaw::context_engine
