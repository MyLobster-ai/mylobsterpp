#pragma once

#include <memory>
#include <string>
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
    ToolRegistry tools_;
};

} // namespace openclaw::agent
