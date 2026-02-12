#include "openclaw/providers/anthropic.hpp"

#include <sstream>
#include <string>

#include "openclaw/agent/thinking.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::providers {

namespace {

constexpr auto kDefaultBaseUrl = "https://api.anthropic.com";
constexpr auto kDefaultModel = "claude-sonnet-4-20250514";
constexpr auto kApiVersion = "2023-06-01";
constexpr auto kMessagesPath = "/v1/messages";

/// Convert a unified Role to the Anthropic role string.
auto role_to_string(Role role) -> std::string {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "user";  // Tool results sent as user messages
        default: return "user";
    }
}

/// Convert a unified ContentBlock to the Anthropic content block format.
auto content_block_to_json(const ContentBlock& block) -> json {
    json j;
    if (block.type == "text") {
        j["type"] = "text";
        j["text"] = block.text;
    } else if (block.type == "tool_use") {
        j["type"] = "tool_use";
        j["id"] = block.tool_use_id.value_or("");
        j["name"] = block.tool_name.value_or("");
        j["input"] = block.tool_input.value_or(json::object());
    } else if (block.type == "tool_result") {
        j["type"] = "tool_result";
        j["tool_use_id"] = block.tool_use_id.value_or("");
        if (block.tool_result.has_value()) {
            j["content"] = block.tool_result->dump();
        } else {
            j["content"] = block.text;
        }
    } else {
        j["type"] = "text";
        j["text"] = block.text;
    }
    return j;
}

/// Convert a unified Message to the Anthropic message format.
auto message_to_json(const Message& msg) -> json {
    json j;
    j["role"] = role_to_string(msg.role);

    json content = json::array();
    for (const auto& block : msg.content) {
        content.push_back(content_block_to_json(block));
    }
    j["content"] = content;
    return j;
}

/// Parse SSE (Server-Sent Events) text into individual lines of event data.
/// Each event starts with "data: " and ends with a double newline.
auto parse_sse_lines(const std::string& body) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::istringstream stream(body);
    std::string line;

    while (std::getline(stream, line)) {
        // Remove trailing \r if present
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

AnthropicProvider::AnthropicProvider(boost::asio::io_context& ioc,
                                     const ProviderConfig& config)
    : api_key_(config.api_key)
    , default_model_(config.model.value_or(kDefaultModel))
    , api_version_(kApiVersion)
    , http_(ioc, infra::HttpClientConfig{
          .base_url = config.base_url.value_or(kDefaultBaseUrl),
          .timeout_seconds = 120,
          .verify_ssl = true,
          .default_headers = {
              {"x-api-key", config.api_key},
              {"anthropic-version", kApiVersion},
              {"content-type", "application/json"},
          },
      })
{
    LOG_INFO("Anthropic provider initialized (model: {}, base: {})",
             default_model_,
             config.base_url.value_or(kDefaultBaseUrl));
}

AnthropicProvider::~AnthropicProvider() = default;

auto AnthropicProvider::build_request_body(const CompletionRequest& req) const -> json {
    json body;

    body["model"] = req.model.empty() ? default_model_ : req.model;

    if (req.system_prompt.has_value() && !req.system_prompt->empty()) {
        body["system"] = *req.system_prompt;
    }

    json messages = json::array();
    for (const auto& msg : req.messages) {
        // Skip system messages; they go into the system field
        if (msg.role == Role::System) continue;
        messages.push_back(message_to_json(msg));
    }
    body["messages"] = messages;

    if (req.max_tokens.has_value()) {
        body["max_tokens"] = *req.max_tokens;
    } else {
        body["max_tokens"] = 4096;
    }

    if (req.temperature.has_value()) {
        body["temperature"] = *req.temperature;
    }

    if (!req.tools.empty()) {
        body["tools"] = req.tools;
    }

    // Apply thinking configuration
    if (req.thinking != ThinkingMode::None) {
        auto thinking_config = agent::thinking_config_from_mode(req.thinking);
        agent::apply_thinking_anthropic(body, thinking_config);
    }

    return body;
}

auto AnthropicProvider::parse_response(const std::string& body) const
    -> Result<CompletionResponse> {
    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse Anthropic response",
            e.what()));
    }

    if (j.contains("error")) {
        auto err_msg = j["error"].value("message", "Unknown error");
        auto err_type = j["error"].value("type", "api_error");
        return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Anthropic API error: " + err_type,
            err_msg));
    }

    CompletionResponse response;
    response.model = j.value("model", "");
    response.stop_reason = j.value("stop_reason", "");

    // Parse usage
    if (j.contains("usage")) {
        response.input_tokens = j["usage"].value("input_tokens", 0);
        response.output_tokens = j["usage"].value("output_tokens", 0);
    }

    // Parse the message
    response.message.id = j.value("id", "");
    response.message.role = Role::Assistant;
    response.message.created_at = Clock::now();

    if (j.contains("content") && j["content"].is_array()) {
        for (const auto& block : j["content"]) {
            ContentBlock cb;
            cb.type = block.value("type", "text");

            if (cb.type == "text") {
                cb.text = block.value("text", "");
            } else if (cb.type == "tool_use") {
                cb.tool_use_id = block.value("id", "");
                cb.tool_name = block.value("name", "");
                if (block.contains("input")) {
                    cb.tool_input = block["input"];
                }
            } else if (cb.type == "thinking") {
                cb.text = block.value("thinking", "");
            }

            response.message.content.push_back(std::move(cb));
        }
    }

    return response;
}

auto AnthropicProvider::parse_sse_event(const json& event,
                                         CompletionResponse& response,
                                         StreamCallback& cb) const -> void {
    auto event_type = event.value("type", "");

    if (event_type == "message_start") {
        if (event.contains("message")) {
            const auto& msg = event["message"];
            response.message.id = msg.value("id", "");
            response.model = msg.value("model", "");
            response.message.role = Role::Assistant;
            response.message.created_at = Clock::now();

            if (msg.contains("usage")) {
                response.input_tokens = msg["usage"].value("input_tokens", 0);
            }
        }
    } else if (event_type == "content_block_start") {
        if (event.contains("content_block")) {
            const auto& block = event["content_block"];
            auto block_type = block.value("type", "text");

            if (block_type == "tool_use") {
                ContentBlock cb_block;
                cb_block.type = "tool_use";
                cb_block.tool_use_id = block.value("id", "");
                cb_block.tool_name = block.value("name", "");
                cb_block.tool_input = json::object();
                response.message.content.push_back(std::move(cb_block));

                CompletionChunk chunk;
                chunk.type = "tool_use";
                chunk.tool_name = block.value("name", "");
                cb(chunk);
            } else if (block_type == "thinking") {
                ContentBlock cb_block;
                cb_block.type = "thinking";
                response.message.content.push_back(std::move(cb_block));
            } else {
                ContentBlock cb_block;
                cb_block.type = "text";
                response.message.content.push_back(std::move(cb_block));
            }
        }
    } else if (event_type == "content_block_delta") {
        if (event.contains("delta")) {
            const auto& delta = event["delta"];
            auto delta_type = delta.value("type", "");

            if (delta_type == "text_delta") {
                auto text = delta.value("text", "");
                if (!response.message.content.empty()) {
                    response.message.content.back().text += text;
                }
                CompletionChunk chunk;
                chunk.type = "text";
                chunk.text = text;
                cb(chunk);
            } else if (delta_type == "input_json_delta") {
                auto partial = delta.value("partial_json", "");
                // Accumulate partial JSON for tool input
                if (!response.message.content.empty()) {
                    auto& last = response.message.content.back();
                    if (last.type == "tool_use") {
                        last.text += partial;  // Accumulate raw JSON string
                    }
                }
            } else if (delta_type == "thinking_delta") {
                auto thinking = delta.value("thinking", "");
                if (!response.message.content.empty()) {
                    response.message.content.back().text += thinking;
                }
                CompletionChunk chunk;
                chunk.type = "thinking";
                chunk.text = thinking;
                cb(chunk);
            }
        }
    } else if (event_type == "content_block_stop") {
        // Finalize tool_use blocks: parse accumulated JSON
        if (!response.message.content.empty()) {
            auto& last = response.message.content.back();
            if (last.type == "tool_use" && !last.text.empty()) {
                try {
                    last.tool_input = json::parse(last.text);
                } catch (...) {
                    last.tool_input = json::object();
                }
                // Notify with final tool input
                CompletionChunk chunk;
                chunk.type = "tool_use";
                chunk.tool_name = last.tool_name;
                chunk.tool_input = last.tool_input;
                cb(chunk);
                last.text.clear();
            }
        }
    } else if (event_type == "message_delta") {
        if (event.contains("delta")) {
            response.stop_reason = event["delta"].value("stop_reason", "");
        }
        if (event.contains("usage")) {
            response.output_tokens = event["usage"].value("output_tokens", 0);
        }
    } else if (event_type == "message_stop") {
        CompletionChunk chunk;
        chunk.type = "stop";
        cb(chunk);
    }
}

auto AnthropicProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req);

    LOG_DEBUG("Anthropic complete request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kMessagesPath, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Anthropic API request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        // Try to extract error message from body
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Anthropic API error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Anthropic API error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    co_return parse_response(http_resp.body);
}

auto AnthropicProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req);
    body["stream"] = true;

    LOG_DEBUG("Anthropic stream request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kMessagesPath, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Anthropic API streaming request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Anthropic API stream error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Anthropic API stream error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    // Parse SSE response body
    CompletionResponse response;
    auto sse_lines = parse_sse_lines(http_resp.body);

    for (const auto& line : sse_lines) {
        if (line == "[DONE]") break;

        try {
            auto event = json::parse(line);
            parse_sse_event(event, response, cb);
        } catch (const json::parse_error& e) {
            LOG_WARN("Failed to parse SSE event: {}", e.what());
            continue;
        }
    }

    co_return response;
}

auto AnthropicProvider::name() const -> std::string_view {
    return "anthropic";
}

auto AnthropicProvider::models() const -> std::vector<std::string> {
    return {
        "claude-opus-4-20250514",
        "claude-sonnet-4-20250514",
        "claude-haiku-3-5-20241022",
        "claude-3-5-sonnet-20241022",
        "claude-3-5-haiku-20241022",
        "claude-3-opus-20240229",
    };
}

} // namespace openclaw::providers
