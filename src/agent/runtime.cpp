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
        co_return make_fail(make_error(
            ErrorCode::InvalidConfig,
            "No provider configured"));
    }

    // Pre-prompt context diagnostics
    size_t total_chars = 0;
    size_t system_chars = 0;
    for (const auto& msg : req.messages) {
        for (const auto& block : msg.content) {
            total_chars += block.text.size();
            if (msg.role == Role::System) {
                system_chars += block.text.size();
            }
        }
    }
    LOG_INFO("Completion request: messages={}, system_chars={}, prompt_chars={}, "
             "provider={}, model={}",
             req.messages.size(), system_chars, total_chars - system_chars,
             provider_->name(), req.model);

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
        co_return make_fail(make_error(
            ErrorCode::InvalidConfig,
            "No provider configured"));
    }

    // Pre-prompt context diagnostics
    size_t stream_total_chars = 0;
    for (const auto& msg : req.messages) {
        for (const auto& block : msg.content) {
            stream_total_chars += block.text.size();
        }
    }
    LOG_INFO("Stream request: messages={}, chars={}, provider={}, model={}",
             req.messages.size(), stream_total_chars,
             provider_->name(), req.model);

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
        co_return make_fail(make_error(
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
            co_return make_fail(result.error());
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
        co_return make_fail(make_error(
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
            co_return make_fail(result.error());
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

void AgentRuntime::set_fallback_providers(std::vector<std::shared_ptr<Provider>> fallbacks) {
    fallback_providers_ = std::move(fallbacks);
    LOG_INFO("Set {} fallback provider(s) for model chain traversal",
             fallback_providers_.size());
}

auto AgentRuntime::is_stop_phrase(std::string_view text) -> bool {
    // v2026.2.24: Multilingual stop phrase matching
    // Trim trailing punctuation for comparison
    std::string trimmed(text);
    while (!trimmed.empty() && (trimmed.back() == '.' || trimmed.back() == '!' ||
           trimmed.back() == '?' || trimmed.back() == ',' || trimmed.back() == ';')) {
        trimmed.pop_back();
    }

    // Normalize to lowercase for ASCII portion
    std::string lower;
    lower.reserve(trimmed.size());
    for (unsigned char c : trimmed) {
        lower += static_cast<char>(std::tolower(c));
    }

    // English stop phrases
    if (lower == "stop openclaw" || lower == "please stop" ||
        lower == "do not do that" || lower == "stop" ||
        lower == "cancel" || lower == "abort" || lower == "quit" ||
        lower == "stop it" || lower == "stop now") {
        return true;
    }

    // Spanish
    if (lower == "para" || lower == "detente" || lower == "basta") {
        return true;
    }

    // French (using raw UTF-8 bytes via regular strings)
    if (lower == "arr\xc3\xaate" || lower == "arr\xc3\xaater" ||
        lower == "arr\xc3\xaate-toi" ||
        lower == "arrete" || lower == "arreter") {
        return true;
    }

    // German
    if (lower == "stopp" || lower == "halt" ||
        lower == "h\xc3\xb6r auf" || lower == "hoer auf") {
        return true;
    }

    // Chinese (simplified) - compare against trimmed (not lowered) for CJK
    if (trimmed == "\xe5\x81\x9c\xe6\xad\xa2" ||   // 停止
        trimmed == "\xe5\x81\x9c" ||                  // 停
        trimmed == "\xe5\x88\xab\xe5\x81\x9a\xe4\xba\x86") {  // 别做了
        return true;
    }

    // Japanese
    if (trimmed == "\xe6\xad\xa2\xe3\x82\x81\xe3\x81\xa6" ||   // 止めて
        trimmed == "\xe3\x82\x84\xe3\x82\x81\xe3\x81\xa6" ||   // やめて
        trimmed == "\xe3\x82\xb9\xe3\x83\x88\xe3\x83\x83\xe3\x83\x97") {  // ストップ
        return true;
    }

    // Russian
    if (trimmed == "\xd1\x81\xd1\x82\xd0\xbe\xd0\xbf" ||                 // стоп
        trimmed == "\xd0\xbe\xd1\x81\xd1\x82\xd0\xb0\xd0\xbd\xd0\xbe\xd0\xb2\xd0\xb8\xd1\x81\xd1\x8c" ||  // остановись
        trimmed == "\xd1\x85\xd0\xb2\xd0\xb0\xd1\x82\xd0\xb8\xd1\x82") {  // хватит
        return true;
    }

    // Arabic
    if (trimmed == "\xd8\xaa\xd9\x88\xd9\x82\xd9\x81" ||  // توقف
        trimmed == "\xd9\x82\xd9\x81") {                     // قف
        return true;
    }

    // Hindi
    if (trimmed == "\xe0\xa4\xb0\xe0\xa5\x81\xe0\xa4\x95\xe0\xa5\x8b" ||  // रुको
        trimmed == "\xe0\xa4\xac\xe0\xa4\x82\xe0\xa4\xa6 \xe0\xa4\x95\xe0\xa4\xb0\xe0\xa5\x8b") {  // बंद करो
        return true;
    }

    return false;
}

} // namespace openclaw::agent
