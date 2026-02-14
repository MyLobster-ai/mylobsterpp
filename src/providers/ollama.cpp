#include "openclaw/providers/ollama.hpp"

#include <sstream>
#include <string>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::providers {

namespace {

constexpr auto kDefaultBaseUrl = "http://127.0.0.1:11434";
constexpr auto kDefaultModel = "llama3.3";
constexpr auto kChatPath = "/api/chat";
constexpr auto kTagsPath = "/api/tags";

auto role_to_string(Role role) -> std::string {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::System: return "system";
        case Role::Tool: return "tool";
        default: return "user";
    }
}

} // anonymous namespace

OllamaProvider::OllamaProvider(boost::asio::io_context& ioc,
                                const ProviderConfig& config)
    : base_url_(config.base_url.value_or(kDefaultBaseUrl))
    , default_model_(config.model.value_or(kDefaultModel))
    , http_(ioc, infra::HttpClientConfig{
          .base_url = config.base_url.value_or(kDefaultBaseUrl),
          .timeout_seconds = 300,  // Ollama can be slow for large models
          .verify_ssl = false,     // Local server
          .default_headers = {
              {"Content-Type", "application/json"},
          },
      })
{
    LOG_INFO("Ollama provider initialized (model: {}, base: {})",
             default_model_, base_url_);
}

OllamaProvider::~OllamaProvider() = default;

auto OllamaProvider::convert_message(const Message& msg) const -> json {
    json j;

    // Ollama uses "tool" role for tool results (like OpenAI)
    if (msg.role == Role::Tool) {
        j["role"] = "tool";
        for (const auto& block : msg.content) {
            if (block.type == "tool_result") {
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

    j["role"] = role_to_string(msg.role);

    // Build content text
    std::string text_content;
    json tool_calls = json::array();
    json images = json::array();

    for (const auto& block : msg.content) {
        if (block.type == "text") {
            text_content += block.text;
        } else if (block.type == "tool_use" && msg.role == Role::Assistant) {
            json tc;
            tc["function"]["name"] = block.tool_name.value_or("");
            tc["function"]["arguments"] = block.tool_input.value_or(json::object());
            tool_calls.push_back(tc);
        } else if (block.type == "image") {
            // Ollama expects base64-encoded images in an "images" array.
            // The block.text should contain the base64 data or data URI.
            auto image_data = block.text;
            // Strip data URI prefix if present
            if (auto pos = image_data.find(","); pos != std::string::npos &&
                image_data.starts_with("data:")) {
                image_data = image_data.substr(pos + 1);
            }
            images.push_back(image_data);
        }
    }

    j["content"] = text_content;

    if (!tool_calls.empty()) {
        j["tool_calls"] = tool_calls;
    }
    if (!images.empty()) {
        j["images"] = images;
    }

    return j;
}

auto OllamaProvider::build_request_body(const CompletionRequest& req,
                                          bool streaming) const -> json {
    json body;
    body["model"] = req.model.empty() ? default_model_ : req.model;
    body["stream"] = streaming;

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

    // Options
    json options;
    if (req.max_tokens.has_value()) {
        options["num_predict"] = *req.max_tokens;
    }
    if (req.temperature.has_value()) {
        options["temperature"] = *req.temperature;
    }
    if (!options.empty()) {
        body["options"] = options;
    }

    // Tools (Ollama format)
    if (!req.tools.empty()) {
        json tools = json::array();
        for (const auto& tool : req.tools) {
            json ollama_tool;
            ollama_tool["type"] = "function";
            json func;

            if (tool.contains("type") && tool["type"] == "function") {
                // Already in OpenAI format
                func = tool["function"];
            } else {
                // Anthropic format
                func["name"] = tool.value("name", "");
                func["description"] = tool.value("description", "");
                if (tool.contains("input_schema")) {
                    func["parameters"] = tool["input_schema"];
                } else if (tool.contains("parameters")) {
                    func["parameters"] = tool["parameters"];
                }
            }

            ollama_tool["function"] = func;
            tools.push_back(ollama_tool);
        }
        body["tools"] = tools;
    }

    return body;
}

auto OllamaProvider::parse_response(const std::string& body) const
    -> Result<CompletionResponse> {
    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse Ollama response",
            e.what()));
    }

    if (j.contains("error")) {
        return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Ollama API error",
            j.value("error", "Unknown error")));
    }

    CompletionResponse response;
    response.model = j.value("model", "");
    response.stop_reason = j.value("done_reason", "stop");

    // Ollama usage
    if (j.contains("prompt_eval_count")) {
        response.input_tokens = j.value("prompt_eval_count", 0);
    }
    if (j.contains("eval_count")) {
        response.output_tokens = j.value("eval_count", 0);
    }

    response.message.id = utils::generate_id();
    response.message.role = Role::Assistant;
    response.message.created_at = Clock::now();

    if (j.contains("message")) {
        const auto& msg = j["message"];
        auto content = msg.value("content", "");
        if (!content.empty()) {
            ContentBlock text_block;
            text_block.type = "text";
            text_block.text = content;
            response.message.content.push_back(std::move(text_block));
        }

        // Tool calls
        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
            for (const auto& tc : msg["tool_calls"]) {
                ContentBlock tool_block;
                tool_block.type = "tool_use";
                tool_block.tool_use_id = utils::generate_id(12);

                if (tc.contains("function")) {
                    tool_block.tool_name = tc["function"].value("name", "");
                    if (tc["function"].contains("arguments")) {
                        tool_block.tool_input = tc["function"]["arguments"];
                    }
                }

                response.message.content.push_back(std::move(tool_block));
            }
        }
    }

    return response;
}

auto OllamaProvider::parse_ndjson_stream(const std::string& body,
                                           StreamCallback& cb)
    -> CompletionResponse {
    CompletionResponse response;
    response.message.id = utils::generate_id();
    response.message.role = Role::Assistant;
    response.message.created_at = Clock::now();

    // Accumulated tool calls across chunks
    json accumulated_tool_calls = json::array();

    std::istringstream stream(body);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        json chunk;
        try {
            chunk = json::parse(line);
        } catch (const json::parse_error& e) {
            LOG_WARN("Failed to parse Ollama NDJSON line: {}", e.what());
            continue;
        }

        response.model = chunk.value("model", response.model);

        bool done = chunk.value("done", false);

        if (chunk.contains("message")) {
            const auto& msg = chunk["message"];

            // Text content
            auto content = msg.value("content", "");
            if (!content.empty()) {
                bool found_text = false;
                for (auto& block : response.message.content) {
                    if (block.type == "text") {
                        block.text += content;
                        found_text = true;
                        break;
                    }
                }
                if (!found_text) {
                    ContentBlock block;
                    block.type = "text";
                    block.text = content;
                    response.message.content.push_back(std::move(block));
                }

                CompletionChunk cc;
                cc.type = "text";
                cc.text = content;
                cb(cc);
            }

            // Accumulate tool calls (Ollama sends partial tool_calls across chunks)
            if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                for (const auto& tc : msg["tool_calls"]) {
                    accumulated_tool_calls.push_back(tc);
                }
            }
        }

        if (done) {
            // Extract final metrics
            if (chunk.contains("prompt_eval_count")) {
                response.input_tokens = chunk.value("prompt_eval_count", 0);
            }
            if (chunk.contains("eval_count")) {
                response.output_tokens = chunk.value("eval_count", 0);
            }
            response.stop_reason = chunk.value("done_reason", "stop");

            // Finalize accumulated tool calls
            if (!accumulated_tool_calls.empty()) {
                for (const auto& tc : accumulated_tool_calls) {
                    ContentBlock tool_block;
                    tool_block.type = "tool_use";
                    tool_block.tool_use_id = utils::generate_id(12);

                    if (tc.contains("function")) {
                        tool_block.tool_name = tc["function"].value("name", "");
                        if (tc["function"].contains("arguments")) {
                            tool_block.tool_input = tc["function"]["arguments"];
                        }
                    }

                    response.message.content.push_back(std::move(tool_block));

                    CompletionChunk cc;
                    cc.type = "tool_use";
                    cc.tool_name = tool_block.tool_name;
                    cc.tool_input = tool_block.tool_input;
                    cb(cc);
                }
            }

            CompletionChunk cc;
            cc.type = "stop";
            cb(cc);
            break;
        }
    }

    return response;
}

auto OllamaProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req, false);

    LOG_DEBUG("Ollama complete request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kChatPath, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Ollama API request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Ollama API error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json.value("error", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Ollama API error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    co_return parse_response(http_resp.body);
}

auto OllamaProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto body = build_request_body(req, true);

    LOG_DEBUG("Ollama stream request: model={}", body.value("model", ""));

    auto result = co_await http_.post(kChatPath, body.dump());

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Ollama API streaming request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            if (err_json.contains("error")) {
                co_return std::unexpected(make_error(
                    ErrorCode::ProviderError,
                    "Ollama API stream error (HTTP " + std::to_string(http_resp.status) + ")",
                    err_json.value("error", http_resp.body)));
            }
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Ollama API stream error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    co_return parse_ndjson_stream(http_resp.body, cb);
}

auto OllamaProvider::discover_models()
    -> boost::asio::awaitable<Result<std::vector<std::string>>> {
    auto result = co_await http_.get(kTagsPath);

    if (!result.has_value()) {
        co_return std::unexpected(result.error());
    }

    if (!result->is_success()) {
        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Ollama model discovery failed",
            "HTTP " + std::to_string(result->status)));
    }

    try {
        auto body = json::parse(result->body);
        std::vector<std::string> model_names;

        if (body.contains("models") && body["models"].is_array()) {
            for (const auto& m : body["models"]) {
                auto model_name = m.value("name", "");
                if (!model_name.empty()) {
                    model_names.push_back(model_name);
                }
            }
        }

        discovered_models_ = model_names;
        LOG_INFO("Discovered {} Ollama models", model_names.size());
        co_return model_names;
    } catch (const json::exception& e) {
        co_return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse Ollama model list",
            e.what()));
    }
}

auto OllamaProvider::name() const -> std::string_view {
    return "ollama";
}

auto OllamaProvider::models() const -> std::vector<std::string> {
    if (!discovered_models_.empty()) {
        return discovered_models_;
    }
    // Return a set of commonly available Ollama models
    return {
        "llama3.3",
        "llama3.2",
        "llama3.1",
        "mistral",
        "mixtral",
        "codellama",
        "phi3",
        "gemma2",
        "qwen2.5",
        "deepseek-r1",
    };
}

} // namespace openclaw::providers
