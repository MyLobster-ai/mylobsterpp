#include "openclaw/agent/runtime.hpp"

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::agent {

AgentRuntime::AgentRuntime(boost::asio::io_context& ioc, Config config)
    : ioc_(ioc)
    , config_(std::move(config))
{
    LOG_INFO("Agent runtime initialized");
}

AgentRuntime::~AgentRuntime() = default;

auto AgentRuntime::process(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    if (!provider_) {
        co_return std::unexpected(make_error(
            ErrorCode::InvalidConfig,
            "No provider configured"));
    }

    LOG_DEBUG("Processing completion request (model: {})", req.model);

    // Inject tool definitions if tools are registered and none were provided
    if (req.tools.empty() && tools_.size() > 0) {
        // Determine format based on provider name
        if (provider_->name() == "openai") {
            req.tools = tools_.to_openai_json();
        } else {
            req.tools = tools_.to_json();
        }
    }

    co_return co_await provider_->complete(std::move(req));
}

auto AgentRuntime::process_stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    if (!provider_) {
        co_return std::unexpected(make_error(
            ErrorCode::InvalidConfig,
            "No provider configured"));
    }

    LOG_DEBUG("Processing streaming request (model: {})", req.model);

    if (req.tools.empty() && tools_.size() > 0) {
        if (provider_->name() == "openai") {
            req.tools = tools_.to_openai_json();
        } else {
            req.tools = tools_.to_json();
        }
    }

    co_return co_await provider_->stream(std::move(req), std::move(cb));
}

auto AgentRuntime::extract_tool_calls(const Message& msg) const
    -> std::vector<ContentBlock> {
    std::vector<ContentBlock> tool_calls;

    for (const auto& block : msg.content) {
        if (block.type == "tool_use") {
            tool_calls.push_back(block);
        }
    }

    return tool_calls;
}

auto AgentRuntime::execute_tool_call(const ContentBlock& tool_call)
    -> boost::asio::awaitable<ContentBlock> {
    auto tool_name = tool_call.tool_name.value_or("");
    auto tool_id = tool_call.tool_use_id.value_or("");
    auto params = tool_call.tool_input.value_or(json::object());

    LOG_INFO("Executing tool call: {} (id: {})", tool_name, tool_id);

    ContentBlock result_block;
    result_block.type = "tool_result";
    result_block.tool_use_id = tool_id;
    result_block.tool_name = tool_name;

    auto result = co_await tools_.execute(tool_name, params);

    if (result.has_value()) {
        result_block.tool_result = result.value();
        result_block.text = result.value().dump();
    } else {
        // On error, return the error message as the tool result
        json error_json;
        error_json["error"] = result.error().what();
        result_block.tool_result = error_json;
        result_block.text = error_json.dump();
        LOG_WARN("Tool {} failed: {}", tool_name, result.error().what());
    }

    co_return result_block;
}

auto AgentRuntime::process_with_tools(CompletionRequest req, int max_iterations)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    if (!provider_) {
        co_return std::unexpected(make_error(
            ErrorCode::InvalidConfig,
            "No provider configured"));
    }

    LOG_DEBUG("Processing request with tool loop (max_iterations: {})", max_iterations);

    // Inject tool definitions
    if (req.tools.empty() && tools_.size() > 0) {
        if (provider_->name() == "openai") {
            req.tools = tools_.to_openai_json();
        } else {
            req.tools = tools_.to_json();
        }
    }

    CompletionResponse final_response;
    int total_input_tokens = 0;
    int total_output_tokens = 0;

    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        LOG_DEBUG("Tool loop iteration {}/{}", iteration + 1, max_iterations);

        auto result = co_await provider_->complete(req);

        if (!result.has_value()) {
            co_return std::unexpected(result.error());
        }

        auto& response = result.value();
        total_input_tokens += response.input_tokens;
        total_output_tokens += response.output_tokens;

        // Check for tool calls
        auto tool_calls = extract_tool_calls(response.message);

        if (tool_calls.empty()) {
            // No more tool calls -- we have the final response
            response.input_tokens = total_input_tokens;
            response.output_tokens = total_output_tokens;
            co_return response;
        }

        LOG_INFO("Model requested {} tool call(s)", tool_calls.size());

        // Add the assistant's response to the conversation
        req.messages.push_back(response.message);

        // Execute each tool call and collect results
        Message tool_results_msg;
        tool_results_msg.id = utils::generate_id();
        tool_results_msg.role = Role::User;  // Tool results go as user messages (Anthropic)
        tool_results_msg.created_at = Clock::now();

        for (const auto& tool_call : tool_calls) {
            auto result_block = co_await execute_tool_call(tool_call);
            tool_results_msg.content.push_back(std::move(result_block));
        }

        // Add tool results to the conversation
        req.messages.push_back(std::move(tool_results_msg));

        // Store last response as the potential final response
        final_response = std::move(response);
    }

    // If we hit max iterations, return the last response with a warning
    LOG_WARN("Tool loop reached max iterations ({})", max_iterations);
    final_response.input_tokens = total_input_tokens;
    final_response.output_tokens = total_output_tokens;
    final_response.stop_reason = "max_iterations";
    co_return final_response;
}

auto AgentRuntime::process_with_tools_stream(CompletionRequest req, StreamCallback cb,
                                              int max_iterations)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    if (!provider_) {
        co_return std::unexpected(make_error(
            ErrorCode::InvalidConfig,
            "No provider configured"));
    }

    LOG_DEBUG("Processing streaming request with tool loop (max_iterations: {})",
              max_iterations);

    // Inject tool definitions
    if (req.tools.empty() && tools_.size() > 0) {
        if (provider_->name() == "openai") {
            req.tools = tools_.to_openai_json();
        } else {
            req.tools = tools_.to_json();
        }
    }

    CompletionResponse final_response;
    int total_input_tokens = 0;
    int total_output_tokens = 0;

    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        LOG_DEBUG("Streaming tool loop iteration {}/{}", iteration + 1, max_iterations);

        // On the final turn (no more tool calls expected), stream to the user callback.
        // On intermediate turns, we need to collect the full response for tool execution.
        // However, we can stream intermediate text too for a better user experience.
        auto result = co_await provider_->stream(req, cb);

        if (!result.has_value()) {
            co_return std::unexpected(result.error());
        }

        auto& response = result.value();
        total_input_tokens += response.input_tokens;
        total_output_tokens += response.output_tokens;

        auto tool_calls = extract_tool_calls(response.message);

        if (tool_calls.empty()) {
            response.input_tokens = total_input_tokens;
            response.output_tokens = total_output_tokens;
            co_return response;
        }

        LOG_INFO("Streaming: Model requested {} tool call(s)", tool_calls.size());

        // Add the assistant's response to the conversation
        req.messages.push_back(response.message);

        // Execute tool calls
        Message tool_results_msg;
        tool_results_msg.id = utils::generate_id();
        tool_results_msg.role = Role::User;
        tool_results_msg.created_at = Clock::now();

        for (const auto& tool_call : tool_calls) {
            auto result_block = co_await execute_tool_call(tool_call);
            tool_results_msg.content.push_back(std::move(result_block));
        }

        req.messages.push_back(std::move(tool_results_msg));
        final_response = std::move(response);
    }

    LOG_WARN("Streaming tool loop reached max iterations ({})", max_iterations);
    final_response.input_tokens = total_input_tokens;
    final_response.output_tokens = total_output_tokens;
    final_response.stop_reason = "max_iterations";
    co_return final_response;
}

void AgentRuntime::set_provider(std::shared_ptr<Provider> provider) {
    provider_ = std::move(provider);
    if (provider_) {
        LOG_INFO("Provider set: {}", provider_->name());
    }
}

auto AgentRuntime::provider() const -> std::shared_ptr<Provider> {
    return provider_;
}

auto AgentRuntime::tool_registry() -> ToolRegistry& {
    return tools_;
}

auto AgentRuntime::tool_registry() const -> const ToolRegistry& {
    return tools_;
}

auto AgentRuntime::config() const -> const Config& {
    return config_;
}

} // namespace openclaw::agent
