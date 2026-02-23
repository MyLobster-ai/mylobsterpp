#include "openclaw/providers/gemini.hpp"

#include <sstream>
#include <string>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::providers {

namespace {

constexpr auto kDefaultBaseUrl = "https://generativelanguage.googleapis.com";
constexpr auto kDefaultModel = "gemini-2.0-flash";

/// Convert a unified Role to the Gemini role string.
auto role_to_string(Role role) -> std::string {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "model";
        case Role::Tool: return "function";  // function responses
        default: return "user";
    }
}

/// Parse SSE lines from response body. Gemini uses JSON array streaming
/// or SSE depending on the endpoint.
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

    // If no SSE lines found, the body might be a JSON array (Gemini REST format)
    if (lines.empty() && !body.empty()) {
        // Try parsing as a JSON array of chunks
        try {
            auto j = json::parse(body);
            if (j.is_array()) {
                for (const auto& item : j) {
                    lines.push_back(item.dump());
                }
            } else {
                // Single JSON object
                lines.push_back(body);
            }
        } catch (...) {
            // Not valid JSON; return empty
        }
    }

    return lines;
}

} // anonymous namespace

GeminiProvider::GeminiProvider(boost::asio::io_context& ioc,
                               const ProviderConfig& config)
    : api_key_(config.api_key)
    , default_model_(config.model.value_or(kDefaultModel))
    , http_(ioc, infra::HttpClientConfig{
          .base_url = config.base_url.value_or(kDefaultBaseUrl),
          .timeout_seconds = 120,
          .verify_ssl = true,
          .default_headers = {
              {"Content-Type", "application/json"},
          },
      })
{
    LOG_INFO("Gemini provider initialized (model: {}, base: {})",
             default_model_,
             config.base_url.value_or(kDefaultBaseUrl));
}

GeminiProvider::~GeminiProvider() = default;

auto GeminiProvider::convert_message(const Message& msg) const -> json {
    json j;
    j["role"] = role_to_string(msg.role);

    json parts = json::array();

    for (const auto& block : msg.content) {
        if (block.type == "text") {
            json part;
            part["text"] = block.text;
            parts.push_back(part);
        } else if (block.type == "tool_use") {
            // Gemini represents tool calls as functionCall parts
            json part;
            part["functionCall"]["name"] = block.tool_name.value_or("");
            part["functionCall"]["args"] = block.tool_input.value_or(json::object());
            parts.push_back(part);
        } else if (block.type == "tool_result") {
            // Gemini represents tool results as functionResponse parts
            json part;
            part["functionResponse"]["name"] = block.tool_name.value_or("");
            if (block.tool_result.has_value()) {
                part["functionResponse"]["response"]["result"] = *block.tool_result;
            } else {
                part["functionResponse"]["response"]["result"] = block.text;
            }
            parts.push_back(part);
        } else if (block.type == "image") {
            json part;
            // Inline data for base64-encoded images
            part["inlineData"]["mimeType"] = "image/jpeg";
            part["inlineData"]["data"] = block.text;
            parts.push_back(part);
        }
    }

    j["parts"] = parts;
    return j;
}

auto GeminiProvider::convert_tools(const std::vector<json>& tools) const -> json {
    json function_declarations = json::array();

    for (const auto& tool : tools) {
        json fd;
        fd["name"] = tool.value("name", "");
        fd["description"] = tool.value("description", "");

        // Convert parameter schema
        if (tool.contains("input_schema")) {
            fd["parameters"] = tool["input_schema"];
        } else if (tool.contains("parameters")) {
            fd["parameters"] = tool["parameters"];
        } else {
            fd["parameters"]["type"] = "object";
            fd["parameters"]["properties"] = json::object();
        }

        function_declarations.push_back(fd);
    }

    json tools_json = json::array();
    json tool_entry;
    tool_entry["functionDeclarations"] = function_declarations;
    tools_json.push_back(tool_entry);

    return tools_json;
}

auto GeminiProvider::build_request_body(const CompletionRequest& req) const -> json {
    json body;

    // Build contents (messages)
    json contents = json::array();
    for (const auto& msg : req.messages) {
        if (msg.role == Role::System) continue;  // System prompt handled separately
        contents.push_back(convert_message(msg));
    }
    body["contents"] = contents;

    // System instruction
    if (req.system_prompt.has_value() && !req.system_prompt->empty()) {
        json sys;
        sys["parts"] = json::array({json{{"text", *req.system_prompt}}});
        body["systemInstruction"] = sys;
    }

    // Generation config
    json gen_config;
    if (req.max_tokens.has_value()) {
        gen_config["maxOutputTokens"] = *req.max_tokens;
    }
    if (req.temperature.has_value()) {
        gen_config["temperature"] = *req.temperature;
    }
    if (!gen_config.empty()) {
        body["generationConfig"] = gen_config;
    }

    // Tools (function calling)
    if (!req.tools.empty()) {
        body["tools"] = convert_tools(req.tools);
    }

    return body;
}

auto GeminiProvider::parse_response(const std::string& body,
                                     const std::string& model) const
    -> Result<CompletionResponse> {
    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse Gemini response",
            e.what()));
    }

    // Check for errors
    if (j.contains("error")) {
        auto err_msg = j["error"].value("message", "Unknown error");
        auto err_code = j["error"].value("code", 0);
        return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Gemini API error (code " + std::to_string(err_code) + ")",
            err_msg));
    }

    CompletionResponse response;
    response.model = model;

    // Parse usage metadata
    if (j.contains("usageMetadata")) {
        response.input_tokens = j["usageMetadata"].value("promptTokenCount", 0);
        response.output_tokens = j["usageMetadata"].value("candidatesTokenCount", 0);
    }

    // Parse candidates
    if (j.contains("candidates") && !j["candidates"].empty()) {
        const auto& candidate = j["candidates"][0];

        // Finish reason
        response.stop_reason = candidate.value("finishReason", "");

        if (candidate.contains("content")) {
            const auto& content = candidate["content"];

            response.message.id = utils::generate_id();
            response.message.role = Role::Assistant;
            response.message.created_at = Clock::now();

            if (content.contains("parts") && content["parts"].is_array()) {
                for (const auto& part : content["parts"]) {
                    if (part.contains("text")) {
                        ContentBlock cb;
                        cb.type = "text";
                        cb.text = part["text"].get<std::string>();
                        response.message.content.push_back(std::move(cb));
                    } else if (part.contains("functionCall")) {
                        ContentBlock cb;
                        cb.type = "tool_use";
                        cb.tool_use_id = utils::generate_id();
                        cb.tool_name = part["functionCall"].value("name", "");
                        if (part["functionCall"].contains("args")) {
                            cb.tool_input = part["functionCall"]["args"];
                        }
                        response.message.content.push_back(std::move(cb));
                    }
                }
            }
        }
    }

    return response;
}

auto GeminiProvider::parse_stream_chunk(const json& chunk,
                                         CompletionResponse& response,
                                         StreamCallback& cb) const -> void {
    // Parse usage metadata from any chunk
    if (chunk.contains("usageMetadata")) {
        response.input_tokens = chunk["usageMetadata"].value("promptTokenCount", 0);
        response.output_tokens = chunk["usageMetadata"].value("candidatesTokenCount", 0);
    }

    if (!chunk.contains("candidates") || chunk["candidates"].empty()) return;

    const auto& candidate = chunk["candidates"][0];

    // Check finish reason
    if (candidate.contains("finishReason") && !candidate["finishReason"].is_null()) {
        response.stop_reason = candidate["finishReason"].get<std::string>();

        if (response.stop_reason == "STOP" || response.stop_reason == "MAX_TOKENS") {
            CompletionChunk cc;
            cc.type = "stop";
            cb(cc);
        }
    }

    if (!candidate.contains("content")) return;
    const auto& content = candidate["content"];

    // Initialize message if needed
    if (response.message.id.empty()) {
        response.message.id = utils::generate_id();
        response.message.role = Role::Assistant;
        response.message.created_at = Clock::now();
    }

    if (!content.contains("parts") || !content["parts"].is_array()) return;

    for (const auto& part : content["parts"]) {
        if (part.contains("text")) {
            auto text = part["text"].get<std::string>();

            // Append to existing text block or create one
            bool found = false;
            for (auto& block : response.message.content) {
                if (block.type == "text") {
                    block.text += text;
                    found = true;
                    break;
                }
            }
            if (!found) {
                ContentBlock block;
                block.type = "text";
                block.text = text;
                response.message.content.push_back(std::move(block));
            }

            CompletionChunk cc;
            cc.type = "text";
            cc.text = text;
            cb(cc);
        } else if (part.contains("functionCall")) {
            ContentBlock block;
            block.type = "tool_use";
            block.tool_use_id = utils::generate_id();
            block.tool_name = part["functionCall"].value("name", "");
            if (part["functionCall"].contains("args")) {
                block.tool_input = part["functionCall"]["args"];
            }
            response.message.content.push_back(std::move(block));

            CompletionChunk cc;
            cc.type = "tool_use";
            cc.tool_name = part["functionCall"].value("name", "");
            cc.tool_input = block.tool_input;
            cb(cc);
        }
    }
}

auto GeminiProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto model = req.model.empty() ? default_model_ : req.model;
    auto body = build_request_body(req);

    // Gemini REST endpoint: /v1beta/models/{model}:generateContent?key={api_key}
    auto path = "/v1beta/models/" + model + ":generateContent?key=" + api_key_;

    LOG_DEBUG("Gemini complete request: model={}", model);

    auto result = co_await http_.post(path, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Gemini API request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Gemini API error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Gemini API error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    co_return parse_response(http_resp.body, model);
}

auto GeminiProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto model = req.model.empty() ? default_model_ : req.model;
    auto body = build_request_body(req);

    // Gemini streaming endpoint: /v1beta/models/{model}:streamGenerateContent?key={api_key}&alt=sse
    auto path = "/v1beta/models/" + model +
                ":streamGenerateContent?key=" + api_key_ + "&alt=sse";

    LOG_DEBUG("Gemini stream request: model={}", model);

    auto result = co_await http_.post(path, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Gemini API streaming request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Gemini API stream error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json["error"].value("message", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Gemini API stream error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    CompletionResponse response;
    response.model = model;

    auto sse_lines = parse_sse_lines(http_resp.body);
    for (const auto& line : sse_lines) {
        if (line == "[DONE]") break;

        try {
            auto chunk_json = json::parse(line);
            parse_stream_chunk(chunk_json, response, cb);
        } catch (const json::parse_error& e) {
            LOG_WARN("Failed to parse Gemini SSE chunk: {}", e.what());
            continue;
        }
    }

    co_return response;
}

auto GeminiProvider::name() const -> std::string_view {
    return "gemini";
}

auto GeminiProvider::models() const -> std::vector<std::string> {
    return {
        "gemini-3.1-pro-preview",
        "gemini-3.1-pro-preview-antigravity-high",
        "gemini-3.1-pro-preview-antigravity-low",
        "gemini-2.0-flash",
        "gemini-2.0-flash-lite",
        "gemini-1.5-pro",
        "gemini-1.5-flash",
        "gemini-1.5-flash-8b",
        "gemini-1.0-pro",
    };
}

} // namespace openclaw::providers
