#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/agent/tool_registry.hpp"
#include "openclaw/agent/thinking.hpp"
#include "openclaw/core/config.hpp"
#include "openclaw/core/error.hpp"
#include "openclaw/providers/provider.hpp"

namespace openclaw::agent {

using providers::CompletionRequest;
using providers::CompletionResponse;
using providers::Provider;
using providers::StreamCallback;

/// The agent runtime orchestrates the interaction between the AI provider,
/// tools, and memory. It implements the agentic loop: send a request to
/// the provider, check if the response contains tool calls, execute those
/// tools, append the results, and repeat until the model produces a final
/// answer (or a maximum iteration count is reached).
class AgentRuntime {
public:
    /// Construct the runtime with an io_context and configuration.
    AgentRuntime(boost::asio::io_context& ioc, Config config);
    ~AgentRuntime();

    AgentRuntime(const AgentRuntime&) = delete;
    AgentRuntime& operator=(const AgentRuntime&) = delete;

    /// Perform a single completion request (no tool loop).
    auto process(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>>;

    /// Perform a streaming completion request (no tool loop).
    auto process_stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>>;

    /// Perform a completion with an agentic tool loop.
    /// The runtime will repeatedly call the provider and execute tool calls
    /// until the model stops requesting tools or max_iterations is reached.
    auto process_with_tools(CompletionRequest req, int max_iterations = 25)
        -> boost::asio::awaitable<Result<CompletionResponse>>;

    /// Perform a streaming completion with an agentic tool loop.
    auto process_with_tools_stream(CompletionRequest req, StreamCallback cb,
                                   int max_iterations = 25)
        -> boost::asio::awaitable<Result<CompletionResponse>>;

    /// Set the active AI provider.
    void set_provider(std::shared_ptr<Provider> provider);

    /// Get the active AI provider, or nullptr if none is set.
    [[nodiscard]] auto provider() const -> std::shared_ptr<Provider>;

    /// Get a mutable reference to the tool registry.
    [[nodiscard]] auto tool_registry() -> ToolRegistry&;

    /// Get a const reference to the tool registry.
    [[nodiscard]] auto tool_registry() const -> const ToolRegistry&;

    /// Get the configuration.
    [[nodiscard]] auto config() const -> const Config&;

    /// v2026.2.24: Set fallback providers for model chain traversal.
    void set_fallback_providers(std::vector<std::shared_ptr<Provider>> fallbacks);

    /// v2026.2.24: Check if text matches a multilingual stop phrase.
    [[nodiscard]] static auto is_stop_phrase(std::string_view text) -> bool;

    /// v2026.2.24: Best-effort delivery mode parameter.
    std::optional<bool> best_effort_deliver;

    /// v2026.3.2: Clear pending tool-call state on interruptions.
    /// Prevents orphaned tool-use transcripts from causing follow-up failures.
    void clear_pending_tool_state();

    /// v2026.3.2: Track if there's a pending tool call awaiting result.
    [[nodiscard]] auto has_pending_tool_call() const -> bool;

    /// v2026.3.2: Cancel a running completion by run ID.
    void cancel_run(const std::string& run_id);

    /// v2026.3.2: System prompt management.
    void set_system_prompt(const std::string& prompt);
    [[nodiscard]] auto system_prompt() const -> const std::string&;

    /// v2026.3.2: Thinking mode management ("none", "basic", "extended").
    void set_thinking_mode(const std::string& mode);
    [[nodiscard]] auto thinking_mode() const -> const std::string&;

    /// v2026.3.2: Model override (applies to next completion).
    void set_model(const std::string& model);
    [[nodiscard]] auto model_override() const -> const std::string&;

    /// v2026.3.7: Prepend system context from plugins.
    void prepend_system_context(const std::string& context);

    /// v2026.3.7: Append system context from plugins.
    void append_system_context(const std::string& context);

    /// v2026.3.7: Get accumulated prepended system context.
    [[nodiscard]] auto prepended_system_context() const -> const std::vector<std::string>&;

    /// v2026.3.7: Get accumulated appended system context.
    [[nodiscard]] auto appended_system_context() const -> const std::vector<std::string>&;

    /// v2026.3.7: Clear accumulated system context injections.
    void clear_system_context_injections();

    /// v2026.3.11: Cooldown window classification (defaults to "unknown").
    std::string cooldown_window = "unknown";

private:
    /// Extract tool call content blocks from a message.
    auto extract_tool_calls(const Message& msg) const
        -> std::vector<ContentBlock>;

    /// Execute a single tool call and return a tool_result ContentBlock.
    auto execute_tool_call(const ContentBlock& tool_call)
        -> boost::asio::awaitable<ContentBlock>;

    boost::asio::io_context& ioc_;
    Config config_;
    std::shared_ptr<Provider> provider_;
    std::vector<std::shared_ptr<Provider>> fallback_providers_;
    ToolRegistry tools_;

    // v2026.3.2: Pending tool-call tracking for interruption cleanup
    bool pending_tool_call_ = false;

    // v2026.3.2: Run cancellation tracking
    std::unordered_set<std::string> cancelled_runs_;

    // v2026.3.2: Runtime state
    std::string system_prompt_;
    std::string thinking_mode_{"none"};
    std::string model_override_;

    // v2026.3.7: Plugin system context injections
    std::vector<std::string> prepended_context_;
    std::vector<std::string> appended_context_;
};

} // namespace openclaw::agent
