#include "openclaw/providers/volcengine.hpp"

#include <sstream>
#include <string>

#include "openclaw/agent/thinking.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::providers {

namespace {

constexpr auto kDefaultBaseUrl = "https://ark.cn-beijing.volces.com/api/v3";
constexpr auto kDefaultModel = "doubao-pro-32k";
constexpr auto kCompletionsPath = "/chat/completions";

/// Convert a unified Role to the VolcEngine role string (OpenAI-compatible).
auto role_to_string(Role role) -> std::string {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::System: return "system";
        case Role::Tool: return "tool";
        default: return "user";
    }
}

/// Parse SSE lines from response body.
auto parse_sse_lines(const std::string& body) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::istringstream stream(body);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with("data: ")) {
            lines.push_back(line.substr(6));
        }
    }
    return lines;
}

} // anonymous namespace

VolcEngineProvider::VolcEngineProvider(boost::asio::io_context& ioc,
                                         const ProviderConfig& config)
    : api_key_(config.api_key)
    , default_model_(config.model.value_or(kDefaultModel))
    , http_(ioc, infra::HttpClientConfig{
          .base_url = config.base_url.value_or(kDefaultBaseUrl),
          .timeout_seconds = 120,
          .verify_ssl = true,
          .default_headers = {
              {"Authorization", "Bearer " + config.api_key},
              {"Content-Type", "application/json"},
          },
      })
{
    LOG_INFO("VolcEngine provider initialized (model: {}, base: {})",
             default_model_,
             config.base_url.value_or(kDefaultBaseUrl));
}

VolcEngineProvider::~VolcEngineProvider() = default;

auto VolcEngineProvider::convert_message(const Message& msg) const -> json {
    json j;
    j["role"] = role_to_string(msg.role);

    // For tool results, use the OpenAI-compatible tool message format
    if (msg.role == Role::Tool) {
        for (const auto& block : msg.content) {
            if (block.type == "tool_result") {
                j["tool_call_id"] = block.tool_use_id.value_or("");
                if (block.tool_result.has_value()) {
                    j["content"] = block.tool_result->dump();
                } else {
                    j["content"] = block.text;
                }
                return j;
            }
        }
        // Fallback: concatenate all text
        std::string text;
        for (const auto& block : msg.content) {
            text += block.text;
        }
        j["content"] = text;
        return j;
    }

    // For assistant messages with tool calls
    if (msg.role == Role::Assistant) {
        bool has_tool_use = false;
        for (const auto& block : msg.content) {
            if (block.type == "tool_use") {
                has_tool_use = true;
                break;
            }
        }

        if (has_tool_use) {
            std::string text_content;
            json tool_calls = json::array();

            for (const auto& block : msg.content) {
                if (block.type == "text") {
                    text_content += block.text;
                } else if (block.type == "tool_use") {
                    json tc;
                    tc["id"] = block.tool_use_id.value_or("");
                    tc["type"] = "function";
                    tc["function"]["name"] = block.tool_name.value_or("");
                    tc["function"]["arguments"] =
                        block.tool_input.has_value() ? block.tool_input->dump() : "{}";
                    tool_calls.push_back(tc);
                }
            }

            if (!text_content.empty()) {
                j["content"] = text_content;
            } else {
                j["content"] = nullptr;
            }
            j["tool_calls"] = tool_calls;
            return j;
        }
    }

    // Simple text content
    if (msg.content.size() == 1 && msg.content[0].type == "text") {
        j["content"] = msg.content[0].text;
    } else {
        // Multi-part content
        json parts = json::array();
        for (const auto& block : msg.content) {
            if (block.type == "text") {
                json part;
                part["type"] = "text";
                part["text"] = block.text;
                parts.push_back(part);
            } else if (block.type == "image") {
                json part;
                part["type"] = "image_url";
                part["image_url"]["url"] = block.text;
                parts.push_back(part);
            }
        }
        j["content"] = parts;
    }

    return j;
}

auto VolcEngineProvider::build_request_body(const CompletionRequest& req,
                                              bool streaming) const -> json {
    json body;

    body["model"] = req.model.empty() ? default_model_ : req.model;

    json messages = json::array();

    // Add system prompt as the first system message
    if (req.system_prompt.has_value() && !req.system_prompt->empty()) {
        json sys_msg;
        sys_msg["role"] = "system";
        sys_msg["content"] = *req.system_prompt;
        messages.push_back(sys_msg);
    }

    for (const auto& msg : req.messages) {
        if (msg.role == Role::System) continue;
        messages.push_back(convert_message(msg));
    }
    body["messages"] = messages;

    if (req.max_tokens.has_value()) {
        body["max_tokens"] = *req.max_tokens;
    }

    if (req.temperature.has_value()) {
        body["temperature"] = *req.temperature;
    }

    if (!req.tools.empty()) {
        json tools = json::array();
        for (const auto& tool : req.tools) {
            // If the tool is already in OpenAI format, use it directly
            if (tool.contains("type") && tool["type"] == "function") {
                tools.push_back(tool);
            } else {
                // Convert from Anthropic-style tool to OpenAI function format
                json oai_tool;
                oai_tool["type"] = "function";
                json func;
                func["name"] = tool.value("name", "");
                func["description"] = tool.value("description", "");
                if (tool.contains("input_schema")) {
                    func["parameters"] = tool["input_schema"];
                } else if (tool.contains("parameters")) {
                    func["parameters"] = tool["parameters"];
                }
                oai_tool["function"] = func;
                tools.push_back(oai_tool);
            }
        }
        body["tools"] = tools;
    }

    if (streaming) {
        body["stream"] = true;
        body["stream_options"]["include_usage"] = true;
    }

    // Apply thinking if requested
    if (req.thinking != ThinkingMode::None) {
        auto thinking_config = agent::thinking_config_from_mode(req.thinking);
        agent::apply_thinking_openai(body, thinking_config);
    }

    return body;
}

auto VolcEngineProvider::parse_response(const std::string& body) const
    -> Result<CompletionResponse> {
    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse VolcEngine response",
            e.what()));
    }

    if (j.contains("error")) {
        auto err_msg = j["error"].value("message", "Unknown error");
        auto err_type = j["error"].value("type", "api_error");
        return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "VolcEngine API error: " + err_type,
            err_msg));
    }

    CompletionResponse response;
    response.model = j.value("model", "");

    // Parse usage
    if (j.contains("usage")) {
        response.input_tokens = j["usage"].value("prompt_tokens", 0);
        response.output_tokens = j["usage"].value("completion_tokens", 0);
    }

    // Parse the first choice
    if (j.contains("choices") && !j["choices"].empty()) {
        const auto& choice = j["choices"][0];
        response.stop_reason = choice.value("finish_reason", "");

        if (choice.contains("message")) {
            const auto& msg = choice["message"];

            response.message.id = j.value("id", "");
            response.message.role = Role::Assistant;
            response.message.created_at = Clock::now();

            // Parse text content
            if (msg.contains("content") && !msg["content"].is_null()) {
                ContentBlock text_block;
                text_block.type = "text";
                text_block.text = msg["content"].get<std::string>();
                response.message.content.push_back(std::move(text_block));
            }

            // Parse tool calls
            if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                for (const auto& tc : msg["tool_calls"]) {
                    ContentBlock tool_block;
                    tool_block.type = "tool_use";
                    tool_block.tool_use_id = tc.value("id", "");

                    if (tc.contains("function")) {
                        tool_block.tool_name = tc["function"].value("name", "");
                        auto args_str = tc["function"].value("arguments", "{}");
                        try {
                            tool_block.tool_input = json::parse(args_str);
                        } catch (...) {
                            tool_block.tool_input = json::object();
                        }
                    }

                    response.message.content.push_back(std::move(tool_block));
                }
            }
        }
    }

    return response;
}

auto VolcEngineProvider::parse_sse_chunk(const json& chunk,
                                           CompletionResponse& response,
                                           StreamCallback& cb) const -> void {
    // Parse usage from stream (when stream_options.include_usage is true)
    if (chunk.contains("usage") && !chunk["usage"].is_null()) {
        response.input_tokens = chunk["usage"].value("prompt_tokens", 0);
        response.output_tokens = chunk["usage"].value("completion_tokens", 0);
    }

    if (!chunk.contains("choices") || chunk["choices"].empty()) return;

    const auto& choice = chunk["choices"][0];

    // Capture the finish reason
    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
        response.stop_reason = choice["finish_reason"].get<std::string>();

        CompletionChunk cc;
        cc.type = "stop";
        cb(cc);
        return;
    }

    if (!choice.contains("delta")) return;
    const auto& delta = choice["delta"];

    // Set message ID from the first chunk
    if (response.message.id.empty() && chunk.contains("id")) {
        response.message.id = chunk["id"].get<std::string>();
        response.message.role = Role::Assistant;
        response.message.created_at = Clock::now();
        response.model = chunk.value("model", "");
    }

    // Text content delta
    if (delta.contains("content") && !delta["content"].is_null()) {
        auto text = delta["content"].get<std::string>();

        // Append to or create a text content block
        bool found_text = false;
        for (auto& block : response.message.content) {
            if (block.type == "text") {
                block.text += text;
                found_text = true;
                break;
            }
        }
        if (!found_text) {
            ContentBlock block;
            block.type = "text";
            block.text = text;
            response.message.content.push_back(std::move(block));
        }

        CompletionChunk cc;
        cc.type = "text";
        cc.text = text;
        cb(cc);
    }

    // Tool calls delta
    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tc_delta : delta["tool_calls"]) {
            auto index = tc_delta.value("index", 0);

            // Ensure we have enough content blocks
            while (response.message.content.size() <= static_cast<size_t>(index) + 1) {
                ContentBlock block;
                block.type = "tool_use";
                block.tool_input = json::object();
                response.message.content.push_back(std::move(block));
            }

            // Find the tool_use block for this index
            size_t tool_idx = 0;
            size_t block_idx = 0;
            for (size_t i = 0; i < response.message.content.size(); ++i) {
                if (response.message.content[i].type == "tool_use") {
                    if (tool_idx == static_cast<size_t>(index)) {
                        block_idx = i;
                        break;
                    }
                    ++tool_idx;
                }
            }

            auto& block = response.message.content[block_idx];

            if (tc_delta.contains("id")) {
                block.tool_use_id = tc_delta["id"].get<std::string>();
            }

            if (tc_delta.contains("function")) {
                const auto& func = tc_delta["function"];
                if (func.contains("name")) {
                    block.tool_name = func["name"].get<std::string>();

                    CompletionChunk cc;
                    cc.type = "tool_use";
                    cc.tool_name = block.tool_name;
                    cb(cc);
                }
                if (func.contains("arguments")) {
                    // Accumulate arguments string
                    block.text += func["arguments"].get<std::string>();
                }
            }
        }
    }
}

auto VolcEngineProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req, false);

    LOG_DEBUG("VolcEngine complete request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kCompletionsPath, body.dump());

    if (!result.has_value()) {
        co_return make_fail(make_error(
            ErrorCode::ConnectionFailed,
            "VolcEngine API request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return make_fail(make_error(
                    ErrorCode::ProviderError,
                    "VolcEngine API error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return make_fail(make_error(
            ErrorCode::ProviderError,
            "VolcEngine API error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    co_return parse_response(http_resp.body);
}

auto VolcEngineProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req, true);

    LOG_DEBUG("VolcEngine stream request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kCompletionsPath, body.dump());

    if (!result.has_value()) {
        co_return make_fail(make_error(
            ErrorCode::ConnectionFailed,
            "VolcEngine API streaming request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return make_fail(make_error(
                    ErrorCode::ProviderError,
                    "VolcEngine API stream error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return make_fail(make_error(
            ErrorCode::ProviderError,
            "VolcEngine API stream error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    CompletionResponse response;
    auto sse_lines = parse_sse_lines(http_resp.body);

    for (const auto& line : sse_lines) {
        if (line == "[DONE]") break;

        try {
            auto chunk = json::parse(line);
            parse_sse_chunk(chunk, response, cb);
        } catch (const json::parse_error& e) {
            LOG_WARN("Failed to parse VolcEngine SSE chunk: {}", e.what());
            continue;
        }
    }

    // Finalize tool_use blocks: parse accumulated argument strings
    for (auto& block : response.message.content) {
        if (block.type == "tool_use" && !block.text.empty()) {
            try {
                block.tool_input = json::parse(block.text);
            } catch (...) {
                block.tool_input = json::object();
            }
            block.text.clear();
        }
    }

    co_return response;
}

auto VolcEngineProvider::name() const -> std::string_view {
    return "volcengine";
}

auto VolcEngineProvider::models() const -> std::vector<std::string> {
    return {
        "doubao-pro-32k",
        "doubao-lite-32k",
        "doubao-pro-128k",
    };
}

} // namespace openclaw::providers
