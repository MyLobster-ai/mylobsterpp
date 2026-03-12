#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/core/types.hpp"

namespace openclaw::context_engine {

using json = nlohmann::json;
using boost::asio::awaitable;

/// v2026.3.7: Lifecycle hooks for context engine plugins.
/// Context engines manage how conversation context is assembled,
/// compacted, and maintained across turns.
struct ContextEngineHooks {
    /// Called when the engine is first loaded. Initialize resources here.
    std::function<awaitable<Result<void>>()> bootstrap;

    /// Called when new content (user message, tool result) arrives.
    /// The engine should decide how to incorporate it into context.
    std::function<awaitable<Result<void>>(const json& content)> ingest;

    /// Called before an LLM call. Assembles the final context window
    /// from ingested content, memories, and session state.
    std::function<awaitable<Result<json>>()> assemble;

    /// Called when context needs to be reduced (approaching token limit).
    /// Returns compacted context.
    std::function<awaitable<Result<json>>(const json& current_context)> compact;

    /// Called after each assistant turn completes.
    std::function<awaitable<Result<void>>(const json& turn_result)> after_turn;

    /// Called before spawning a subagent to prepare its initial context.
    std::function<awaitable<Result<json>>(const json& spawn_config)> prepare_subagent_spawn;

    /// Called when a spawned subagent session ends.
    std::function<awaitable<Result<void>>(const json& subagent_result)> on_subagent_ended;
};

/// v2026.3.7: Context engine plugin slot.
/// Manages context assembly and compaction lifecycle.
class ContextEngine {
public:
    virtual ~ContextEngine() = default;

    /// Plugin identifier.
    [[nodiscard]] virtual auto id() const -> std::string_view = 0;

    /// Human-readable name.
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /// Get the lifecycle hooks for this engine.
    [[nodiscard]] virtual auto hooks() const -> const ContextEngineHooks& = 0;

    /// Whether this engine supports compaction.
    [[nodiscard]] virtual auto supports_compaction() const -> bool { return true; }

    /// Whether this engine supports subagent context preparation.
    [[nodiscard]] virtual auto supports_subagents() const -> bool { return true; }
};

/// v2026.3.7: Legacy context engine wrapping existing compaction behavior.
/// Used when no plugin is configured — preserves backward compatibility.
class LegacyContextEngine : public ContextEngine {
public:
    LegacyContextEngine();
    ~LegacyContextEngine() override = default;

    [[nodiscard]] auto id() const -> std::string_view override { return "legacy"; }
    [[nodiscard]] auto name() const -> std::string_view override { return "Legacy Context Engine"; }
    [[nodiscard]] auto hooks() const -> const ContextEngineHooks& override { return hooks_; }

private:
    ContextEngineHooks hooks_;
};

} // namespace openclaw::context_engine
