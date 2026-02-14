#include "openclaw/providers/huggingface.hpp"

#include <regex>
#include <sstream>
#include <string>

#include "openclaw/agent/thinking.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::providers {

namespace {

constexpr auto kDefaultBaseUrl = "https://router.huggingface.co";
constexpr auto kDefaultModel = "meta-llama/Llama-3.3-70B-Instruct";
constexpr auto kCompletionsPath = "/v1/chat/completions";

auto role_to_string(Role role) -> std::string {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::System: return "system";
        case Role::Tool: return "tool";
        default: return "user";
    }
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

auto HuggingFaceProvider::static_catalog() -> const std::vector<HFModelInfo>& {
    static const std::vector<HFModelInfo> catalog = {
        {"meta-llama/Llama-3.3-70B-Instruct", 131072, 8192, false},
        {"meta-llama/Llama-4-Scout-17B-16E-Instruct", 131072, 8192, false},
        {"meta-llama/Llama-4-Maverick-17B-128E-Instruct", 131072, 8192, false},
        {"Qwen/Qwen3-235B-A22B", 131072, 8192, false},
        {"Qwen/Qwen3-32B", 131072, 8192, false},
        {"Qwen/QwQ-32B", 131072, 8192, true},
        {"deepseek-ai/DeepSeek-R1-0528", 131072, 8192, true},
        {"deepseek-ai/DeepSeek-R1", 131072, 8192, true},
        {"deepseek-ai/DeepSeek-V3-0324", 131072, 8192, false},
        {"google/gemma-3-27b-it", 131072, 8192, false},
        {"mistralai/Mistral-Small-24B-Instruct-2501", 131072, 8192, false},
        {"mistralai/Mistral-Nemo-Instruct-2407", 131072, 8192, false},
        {"NousResearch/Hermes-3-Llama-3.1-8B", 131072, 8192, false},
        {"microsoft/Phi-3.5-mini-instruct", 131072, 8192, false},
        {"microsoft/phi-4", 131072, 8192, false},
        {"01-ai/Yi-1.5-34B-Chat", 65536, 8192, false},
        {"CohereForAI/c4ai-command-r-plus-08-2024", 131072, 8192, false},
        {"nvidia/Llama-3.1-Nemotron-70B-Instruct-HF", 131072, 8192, false},
        {"HuggingFaceH4/starchat2-15b-v0.1", 16384, 4096, false},
        {"bigcode/starcoder2-15b-instruct-v0.1", 16384, 4096, false},
    };
    return catalog;
}

auto HuggingFaceProvider::is_reasoning_model(const std::string& model_id) -> bool {
    static const std::regex reasoning_pattern(
        "r1|reasoning|thinking|reason|qwq", std::regex::icase);
    return std::regex_search(model_id, reasoning_pattern);
}

auto HuggingFaceProvider::strip_route_policy(const std::string& model)
    -> std::pair<std::string, std::string> {
    for (const auto& suffix : {":cheapest", ":fastest"}) {
        if (model.ends_with(suffix)) {
            return {model.substr(0, model.size() - std::string(suffix).size()),
                    std::string(suffix).substr(1)};
        }
    }
    return {model, ""};
}

HuggingFaceProvider::HuggingFaceProvider(boost::asio::io_context& ioc,
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
    LOG_INFO("HuggingFace provider initialized (model: {}, base: {})",
             default_model_,
             config.base_url.value_or(kDefaultBaseUrl));
}

HuggingFaceProvider::~HuggingFaceProvider() = default;

auto HuggingFaceProvider::convert_message(const Message& msg) const -> json {
    json j;
    j["role"] = role_to_string(msg.role);

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
        std::string text;
        for (const auto& block : msg.content) {
            text += block.text;
        }
        j["content"] = text;
        return j;
    }

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

    if (msg.content.size() == 1 && msg.content[0].type == "text") {
        j["content"] = msg.content[0].text;
    } else {
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

auto HuggingFaceProvider::build_request_body(const CompletionRequest& req,
                                               bool streaming) const -> json {
    json body;

    auto model_name = req.model.empty() ? default_model_ : req.model;
    auto [clean_model, policy] = strip_route_policy(model_name);
    body["model"] = clean_model;

    json messages = json::array();
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
            if (tool.contains("type") && tool["type"] == "function") {
                tools.push_back(tool);
            } else {
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

    if (req.thinking != ThinkingMode::None) {
        auto thinking_config = agent::thinking_config_from_mode(req.thinking);
        agent::apply_thinking_openai(body, thinking_config);
    }

    return body;
}

auto HuggingFaceProvider::parse_response(const std::string& body) const
    -> Result<CompletionResponse> {
    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse HuggingFace response",
            e.what()));
    }

    if (j.contains("error")) {
        auto err_msg = j["error"].value("message", j.value("error", "Unknown error"));
        return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "HuggingFace API error",
            err_msg));
    }

    CompletionResponse response;
    response.model = j.value("model", "");

    if (j.contains("usage")) {
        response.input_tokens = j["usage"].value("prompt_tokens", 0);
        response.output_tokens = j["usage"].value("completion_tokens", 0);
    }

    if (j.contains("choices") && !j["choices"].empty()) {
        const auto& choice = j["choices"][0];
        response.stop_reason = choice.value("finish_reason", "");

        if (choice.contains("message")) {
            const auto& msg = choice["message"];
            response.message.id = j.value("id", "");
            response.message.role = Role::Assistant;
            response.message.created_at = Clock::now();

            if (msg.contains("content") && !msg["content"].is_null()) {
                ContentBlock text_block;
                text_block.type = "text";
                text_block.text = msg["content"].get<std::string>();
                response.message.content.push_back(std::move(text_block));
            }

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

auto HuggingFaceProvider::parse_sse_chunk(const json& chunk,
                                            CompletionResponse& response,
                                            StreamCallback& cb) const -> void {
    if (chunk.contains("usage") && !chunk["usage"].is_null()) {
        response.input_tokens = chunk["usage"].value("prompt_tokens", 0);
        response.output_tokens = chunk["usage"].value("completion_tokens", 0);
    }

    if (!chunk.contains("choices") || chunk["choices"].empty()) return;

    const auto& choice = chunk["choices"][0];

    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
        response.stop_reason = choice["finish_reason"].get<std::string>();
        CompletionChunk cc;
        cc.type = "stop";
        cb(cc);
        return;
    }

    if (!choice.contains("delta")) return;
    const auto& delta = choice["delta"];

    if (response.message.id.empty() && chunk.contains("id")) {
        response.message.id = chunk["id"].get<std::string>();
        response.message.role = Role::Assistant;
        response.message.created_at = Clock::now();
        response.model = chunk.value("model", "");
    }

    if (delta.contains("content") && !delta["content"].is_null()) {
        auto text = delta["content"].get<std::string>();

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

    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tc_delta : delta["tool_calls"]) {
            auto index = tc_delta.value("index", 0);

            while (response.message.content.size() <= static_cast<size_t>(index) + 1) {
                ContentBlock block;
                block.type = "tool_use";
                block.tool_input = json::object();
                response.message.content.push_back(std::move(block));
            }

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
                    block.text += func["arguments"].get<std::string>();
                }
            }
        }
    }
}

auto HuggingFaceProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req, false);

    // Apply route policy as header hint
    auto model_name = req.model.empty() ? default_model_ : req.model;
    auto [_, policy] = strip_route_policy(model_name);

    std::map<std::string, std::string> extra_headers;
    if (!policy.empty()) {
        extra_headers["X-HF-Route-Policy"] = policy;
    }

    LOG_DEBUG("HuggingFace complete request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kCompletionsPath, body.dump(),
                                       "application/json", extra_headers);

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "HuggingFace API request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "HuggingFace API error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].is_string()
                        ? err_json["error"].get<std::string>()
                        : err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "HuggingFace API error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    co_return parse_response(http_resp.body);
}

auto HuggingFaceProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req, true);

    auto model_name = req.model.empty() ? default_model_ : req.model;
    auto [_, policy] = strip_route_policy(model_name);

    std::map<std::string, std::string> extra_headers;
    if (!policy.empty()) {
        extra_headers["X-HF-Route-Policy"] = policy;
    }

    LOG_DEBUG("HuggingFace stream request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kCompletionsPath, body.dump(),
                                       "application/json", extra_headers);

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "HuggingFace API streaming request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "HuggingFace API stream error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].is_string()
                        ? err_json["error"].get<std::string>()
                        : err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "HuggingFace API stream error",
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
            LOG_WARN("Failed to parse HuggingFace SSE chunk: {}", e.what());
            continue;
        }
    }

    // Finalize tool_use blocks
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

auto HuggingFaceProvider::discover_models()
    -> boost::asio::awaitable<Result<std::vector<HFModelInfo>>> {
    auto result = co_await http_.get("/v1/models");

    if (!result.has_value()) {
        LOG_WARN("HuggingFace model discovery failed: {}", result.error().what());
        co_return std::unexpected(result.error());
    }

    if (!result->is_success()) {
        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "HuggingFace model discovery failed",
            "HTTP " + std::to_string(result->status)));
    }

    try {
        auto body = json::parse(result->body);
        std::vector<HFModelInfo> models;

        if (body.contains("data") && body["data"].is_array()) {
            for (const auto& m : body["data"]) {
                HFModelInfo info;
                info.id = m.value("id", "");
                if (info.id.empty()) continue;

                if (m.contains("context_length")) {
                    info.context_length = m.value("context_length", 131072);
                }
                info.supports_reasoning = is_reasoning_model(info.id);
                models.push_back(std::move(info));
            }
        }

        discovered_models_ = models;
        LOG_INFO("Discovered {} HuggingFace models", models.size());
        co_return models;
    } catch (const json::exception& e) {
        co_return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse model list",
            e.what()));
    }
}

auto HuggingFaceProvider::name() const -> std::string_view {
    return "huggingface";
}

auto HuggingFaceProvider::models() const -> std::vector<std::string> {
    // Return discovered models if available, otherwise static catalog
    if (!discovered_models_.empty()) {
        std::vector<std::string> result;
        result.reserve(discovered_models_.size());
        for (const auto& m : discovered_models_) {
            result.push_back(m.id);
        }
        return result;
    }

    const auto& catalog = static_catalog();
    std::vector<std::string> result;
    result.reserve(catalog.size());
    for (const auto& m : catalog) {
        result.push_back(m.id);
    }
    return result;
}

} // namespace openclaw::providers
