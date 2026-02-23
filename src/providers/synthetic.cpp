#include "openclaw/providers/synthetic.hpp"

#include <regex>
#include <sstream>
#include <string>

#include "openclaw/agent/thinking.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::providers {

namespace {

constexpr auto kDefaultBaseUrl = "https://api.synthetic.new/anthropic";
constexpr auto kDefaultModel = "deepseek-ai/DeepSeek-V3-0324";
constexpr auto kApiVersion = "2023-06-01";
constexpr auto kMessagesPath = "/v1/messages";

auto role_to_string(Role role) -> std::string {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "user";
        default: return "user";
    }
}

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

auto SyntheticProvider::static_catalog() -> const std::vector<SyntheticModelInfo>& {
    static const std::vector<SyntheticModelInfo> catalog = {
        {"hf:MiniMaxAI/MiniMax-M2.1", "MiniMax-M2.1", 131072, 8192, false},
        {"hf:deepseek-ai/DeepSeek-R1", "deepseek-r1", 131072, 8192, true},
        {"hf:deepseek-ai/DeepSeek-R1-0528", "deepseek-r1-0528", 131072, 8192, true},
        {"hf:deepseek-ai/DeepSeek-V3-0324", "deepseek-v3-0324", 131072, 8192, false},
        {"hf:Qwen/Qwen3-235B-A22B", "qwen3-235b", 131072, 8192, false},
        {"hf:Qwen/Qwen3-32B", "qwen3-32b", 131072, 8192, false},
        {"hf:Qwen/QwQ-32B", "qwq-32b", 131072, 8192, true},
        {"hf:THUDM/GLM-4.5-9B-0414", "glm-4.5-9b", 131072, 8192, false},
        {"hf:THUDM/GLM-4.6-9B-0414", "glm-4.6-9b", 131072, 8192, false},
        {"hf:THUDM/GLM-5-32B-0414", "glm-5-32b", 131072, 8192, false},
        {"hf:meta-llama/Llama-3.3-70B-Instruct", "llama-3.3-70b", 131072, 8192, false},
        {"hf:meta-llama/Llama-4-Scout-17B-16E-Instruct", "llama-4-scout-17b", 131072, 8192, false},
        {"hf:meta-llama/Llama-4-Maverick-17B-128E-Instruct", "llama-4-maverick-17b", 131072, 8192, false},
        {"hf:moonshotai/Kimi-K2", "kimi-k2", 131072, 8192, false},
        {"hf:mistralai/Mistral-Small-24B-Instruct-2501", "mistral-small-24b", 131072, 8192, false},
        {"hf:google/gemma-3-27b-it", "gemma-3-27b", 131072, 8192, false},
        {"hf:NousResearch/Hermes-3-Llama-3.1-8B", "hermes-3-8b", 131072, 8192, false},
        {"hf:microsoft/phi-4", "phi-4", 131072, 8192, false},
        {"hf:CohereForAI/c4ai-command-r-plus-08-2024", "command-r-plus", 131072, 8192, false},
        {"hf:01-ai/Yi-1.5-34B-Chat", "yi-1.5-34b", 65536, 8192, false},
        {"hf:nvidia/Llama-3.1-Nemotron-70B-Instruct-HF", "nemotron-70b", 131072, 8192, false},
        {"hf:bigcode/starcoder2-15b-instruct-v0.1", "starcoder2-15b", 16384, 4096, false},
    };
    return catalog;
}

auto SyntheticProvider::is_reasoning_model(const std::string& model_id) -> bool {
    static const std::regex reasoning_pattern(
        "r1|reasoning|think|reason|qwq", std::regex::icase);
    return std::regex_search(model_id, reasoning_pattern);
}

auto SyntheticProvider::resolve_model(const std::string& model_id) const -> std::string {
    return resolve_hf_model(model_id);
}

auto SyntheticProvider::resolve_hf_model(const std::string& model_id) -> std::string {
    // If it starts with hf:, look up in catalog
    if (model_id.starts_with("hf:")) {
        for (const auto& m : static_catalog()) {
            if (m.id == model_id) {
                return m.api_id;
            }
        }
        // Fallback: return as-is if not found in catalog
        return model_id;
    }
    return model_id;
}

SyntheticProvider::SyntheticProvider(boost::asio::io_context& ioc,
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
    LOG_INFO("Synthetic provider initialized (model: {}, base: {})",
             default_model_,
             config.base_url.value_or(kDefaultBaseUrl));
}

SyntheticProvider::~SyntheticProvider() = default;

auto SyntheticProvider::build_request_body(const CompletionRequest& req) const -> json {
    json body;

    auto model_name = req.model.empty() ? default_model_ : req.model;
    body["model"] = resolve_model(model_name);

    if (req.system_prompt.has_value() && !req.system_prompt->empty()) {
        body["system"] = *req.system_prompt;
    }

    json messages = json::array();
    for (const auto& msg : req.messages) {
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

    if (req.thinking != ThinkingMode::None) {
        auto thinking_config = agent::thinking_config_from_mode(req.thinking);
        agent::apply_thinking_anthropic(body, thinking_config);
    }

    return body;
}

auto SyntheticProvider::parse_response(const std::string& body) const
    -> Result<CompletionResponse> {
    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse Synthetic response",
            e.what()));
    }

    if (j.contains("error")) {
        auto err_msg = j["error"].value("message", "Unknown error");
        auto err_type = j["error"].value("type", "api_error");
        return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Synthetic API error: " + err_type,
            err_msg));
    }

    CompletionResponse response;
    response.model = j.value("model", "");
    response.stop_reason = j.value("stop_reason", "");

    if (j.contains("usage")) {
        response.input_tokens = j["usage"].value("input_tokens", 0);
        response.output_tokens = j["usage"].value("output_tokens", 0);
    }

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

auto SyntheticProvider::parse_sse_event(const json& event,
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
                if (!response.message.content.empty()) {
                    auto& last = response.message.content.back();
                    if (last.type == "tool_use") {
                        last.text += partial;
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
        if (!response.message.content.empty()) {
            auto& last = response.message.content.back();
            if (last.type == "tool_use" && !last.text.empty()) {
                try {
                    last.tool_input = json::parse(last.text);
                } catch (...) {
                    last.tool_input = json::object();
                }
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

auto SyntheticProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req);

    LOG_DEBUG("Synthetic complete request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kMessagesPath, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Synthetic API request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Synthetic API error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Synthetic API error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    co_return parse_response(http_resp.body);
}

auto SyntheticProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req);
    body["stream"] = true;

    LOG_DEBUG("Synthetic stream request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kMessagesPath, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Synthetic API streaming request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Synthetic API stream error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Synthetic API stream error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    CompletionResponse response;
    auto sse_lines = parse_sse_lines(http_resp.body);

    for (const auto& line : sse_lines) {
        if (line == "[DONE]") break;

        try {
            auto event = json::parse(line);
            parse_sse_event(event, response, cb);
        } catch (const json::parse_error& e) {
            LOG_WARN("Failed to parse Synthetic SSE event: {}", e.what());
            continue;
        }
    }

    co_return response;
}

auto SyntheticProvider::name() const -> std::string_view {
    return "synthetic";
}

auto SyntheticProvider::models() const -> std::vector<std::string> {
    const auto& catalog = static_catalog();
    std::vector<std::string> result;
    result.reserve(catalog.size());
    for (const auto& m : catalog) {
        result.push_back(m.id);
    }
    return result;
}

} // namespace openclaw::providers
