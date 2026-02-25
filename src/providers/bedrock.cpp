#include "openclaw/providers/bedrock.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::providers {

namespace {

constexpr auto kDefaultRegion = "us-east-1";
constexpr auto kDefaultModel = "anthropic.claude-3-5-sonnet-20241022-v2:0";
constexpr auto kService = "bedrock-runtime";

/// Convert a unified Role to the Bedrock Converse API role string.
auto role_to_string(Role role) -> std::string {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        default: return "user";
    }
}

/// Get the current UTC time as a formatted string for SigV4.
auto get_amz_date() -> std::pair<std::string, std::string> {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &time_t);
#else
    gmtime_r(&time_t, &tm_buf);
#endif

    char amz_date[17];  // YYYYMMDDTHHMMSSZ
    std::strftime(amz_date, sizeof(amz_date), "%Y%m%dT%H%M%SZ", &tm_buf);

    char date_stamp[9];  // YYYYMMDD
    std::strftime(date_stamp, sizeof(date_stamp), "%Y%m%d", &tm_buf);

    return {std::string(amz_date), std::string(date_stamp)};
}

/// Hex-encode a binary string.
auto hex_encode(const std::string& data) -> std::string {
    std::ostringstream oss;
    for (unsigned char c : data) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(c);
    }
    return oss.str();
}

/// Compute SHA-256 hash and return as hex string.
auto sha256_hex(std::string_view data) -> std::string {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()),
           data.size(), hash);
    return hex_encode(std::string(reinterpret_cast<char*>(hash), SHA256_DIGEST_LENGTH));
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
        // Bedrock also uses plain JSON lines in some streaming modes
        else if (!line.empty() && line.front() == '{') {
            lines.push_back(line);
        }
    }
    return lines;
}

} // anonymous namespace

BedrockProvider::BedrockProvider(boost::asio::io_context& ioc,
                                 const ProviderConfig& config)
    : default_model_(config.model.value_or(kDefaultModel))
    , http_(ioc, infra::HttpClientConfig{
          .base_url = "",  // Will be set below
          .timeout_seconds = 120,
          .verify_ssl = true,
          .default_headers = {
              {"Content-Type", "application/json"},
          },
      })
{
    // Parse credentials: api_key can be "ACCESS_KEY_ID:SECRET_ACCESS_KEY"
    // or fall back to environment variables
    if (!config.api_key.empty() && config.api_key.find(':') != std::string::npos) {
        auto pos = config.api_key.find(':');
        access_key_id_ = config.api_key.substr(0, pos);
        secret_access_key_ = config.api_key.substr(pos + 1);
    } else {
        // Try environment variables
        if (auto* key = std::getenv("AWS_ACCESS_KEY_ID")) {
            access_key_id_ = key;
        }
        if (auto* secret = std::getenv("AWS_SECRET_ACCESS_KEY")) {
            secret_access_key_ = secret;
        }
    }

    // Determine region
    if (auto* env_region = std::getenv("AWS_DEFAULT_REGION")) {
        region_ = env_region;
    } else {
        region_ = kDefaultRegion;
    }

    // Set base URL
    if (config.base_url.has_value()) {
        // Use provided base URL (for testing / custom endpoints)
        // Reconstruct HttpClient with proper base URL
        http_ = infra::HttpClient(ioc, infra::HttpClientConfig{
            .base_url = *config.base_url,
            .timeout_seconds = 120,
            .verify_ssl = true,
            .default_headers = {
                {"Content-Type", "application/json"},
            },
        });
    } else {
        auto base = "https://bedrock-runtime." + region_ + ".amazonaws.com";
        http_ = infra::HttpClient(ioc, infra::HttpClientConfig{
            .base_url = base,
            .timeout_seconds = 120,
            .verify_ssl = true,
            .default_headers = {
                {"Content-Type", "application/json"},
            },
        });
    }

    LOG_INFO("Bedrock provider initialized (model: {}, region: {})",
             default_model_, region_);
}

BedrockProvider::~BedrockProvider() = default;

auto BedrockProvider::hmac_sha256(std::string_view key,
                                   std::string_view data) -> std::string {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;

    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(),
         result, &result_len);

    return std::string(reinterpret_cast<char*>(result), result_len);
}

auto BedrockProvider::canonical_request_hash(
    std::string_view method,
    std::string_view path,
    std::string_view payload,
    const std::map<std::string, std::string>& headers) const -> std::string {

    std::ostringstream canonical;

    // HTTP method
    canonical << method << "\n";

    // Canonical URI (path)
    canonical << path << "\n";

    // Canonical query string (empty for POST)
    canonical << "\n";

    // Canonical headers (sorted by lowercase header name)
    std::vector<std::pair<std::string, std::string>> sorted_headers;
    for (const auto& [k, v] : headers) {
        auto lower = utils::to_lower(k);
        sorted_headers.emplace_back(lower, v);
    }
    std::sort(sorted_headers.begin(), sorted_headers.end());

    std::string signed_headers_str;
    for (const auto& [k, v] : sorted_headers) {
        canonical << k << ":" << v << "\n";
        if (!signed_headers_str.empty()) signed_headers_str += ";";
        signed_headers_str += k;
    }
    canonical << "\n";

    // Signed headers
    canonical << signed_headers_str << "\n";

    // Hashed payload
    canonical << sha256_hex(payload);

    return sha256_hex(canonical.str());
}

auto BedrockProvider::sign_request(
    std::string_view method,
    std::string_view path,
    std::string_view payload,
    const std::map<std::string, std::string>& headers) const
    -> std::map<std::string, std::string> {

    auto [amz_date, date_stamp] = get_amz_date();

    // Build the headers map including required AWS headers
    std::map<std::string, std::string> sign_headers = headers;
    sign_headers["x-amz-date"] = amz_date;
    sign_headers["x-amz-content-sha256"] = sha256_hex(payload);

    // Determine the host from the base URL
    auto base = http_.base_url();
    std::string host;
    if (auto pos = base.find("://"); pos != std::string::npos) {
        host = base.substr(pos + 3);
    } else {
        host = base;
    }
    // Remove trailing slash
    if (!host.empty() && host.back() == '/') {
        host.pop_back();
    }
    sign_headers["host"] = host;

    // Credential scope
    auto scope = date_stamp + "/" + region_ + "/" + kService + "/aws4_request";

    // Canonical request hash
    auto req_hash = canonical_request_hash(method, path, payload, sign_headers);

    // String to sign
    std::string string_to_sign = "AWS4-HMAC-SHA256\n" + amz_date + "\n" +
                                  scope + "\n" + req_hash;

    // Signing key derivation
    auto k_date = hmac_sha256("AWS4" + secret_access_key_, date_stamp);
    auto k_region = hmac_sha256(k_date, region_);
    auto k_service = hmac_sha256(k_region, kService);
    auto k_signing = hmac_sha256(k_service, "aws4_request");

    // Signature
    auto signature = hex_encode(hmac_sha256(k_signing, string_to_sign));

    // Build signed headers list
    std::vector<std::string> header_names;
    for (const auto& [k, v] : sign_headers) {
        header_names.push_back(utils::to_lower(k));
    }
    std::sort(header_names.begin(), header_names.end());
    std::string signed_headers_str;
    for (const auto& h : header_names) {
        if (!signed_headers_str.empty()) signed_headers_str += ";";
        signed_headers_str += h;
    }

    // Authorization header
    auto auth = "AWS4-HMAC-SHA256 Credential=" + access_key_id_ + "/" +
                scope + ", SignedHeaders=" + signed_headers_str +
                ", Signature=" + signature;

    // Return the extra headers to add to the request
    std::map<std::string, std::string> result;
    result["Authorization"] = auth;
    result["x-amz-date"] = amz_date;
    result["x-amz-content-sha256"] = sha256_hex(payload);

    return result;
}

auto BedrockProvider::build_request_body(const CompletionRequest& req) const -> json {
    json body;

    // Bedrock Converse API format
    json messages = json::array();
    for (const auto& msg : req.messages) {
        if (msg.role == Role::System) continue;

        json m;
        m["role"] = role_to_string(msg.role);

        json content = json::array();
        for (const auto& block : msg.content) {
            if (block.type == "text") {
                json c;
                c["text"] = block.text;
                content.push_back(c);
            } else if (block.type == "tool_use") {
                json c;
                c["toolUse"]["toolUseId"] = block.tool_use_id.value_or("");
                c["toolUse"]["name"] = block.tool_name.value_or("");
                c["toolUse"]["input"] = block.tool_input.value_or(json::object());
                content.push_back(c);
            } else if (block.type == "tool_result") {
                json c;
                c["toolResult"]["toolUseId"] = block.tool_use_id.value_or("");
                json tool_content = json::array();
                json text_entry;
                if (block.tool_result.has_value()) {
                    text_entry["text"] = block.tool_result->dump();
                } else {
                    text_entry["text"] = block.text;
                }
                tool_content.push_back(text_entry);
                c["toolResult"]["content"] = tool_content;
                content.push_back(c);
            }
        }
        m["content"] = content;
        messages.push_back(m);
    }
    body["messages"] = messages;

    // System prompt
    if (req.system_prompt.has_value() && !req.system_prompt->empty()) {
        json sys = json::array();
        json text_entry;
        text_entry["text"] = *req.system_prompt;
        sys.push_back(text_entry);
        body["system"] = sys;
    }

    // Inference configuration
    json infer_config;
    if (req.max_tokens.has_value()) {
        infer_config["maxTokens"] = *req.max_tokens;
    } else {
        infer_config["maxTokens"] = 4096;
    }
    if (req.temperature.has_value()) {
        infer_config["temperature"] = *req.temperature;
    }
    body["inferenceConfig"] = infer_config;

    // Tool configuration
    if (!req.tools.empty()) {
        json tool_config;
        json tools = json::array();
        for (const auto& tool : req.tools) {
            json t;
            t["toolSpec"]["name"] = tool.value("name", "");
            t["toolSpec"]["description"] = tool.value("description", "");
            if (tool.contains("input_schema")) {
                t["toolSpec"]["inputSchema"]["json"] = tool["input_schema"];
            } else if (tool.contains("parameters")) {
                t["toolSpec"]["inputSchema"]["json"] = tool["parameters"];
            }
            tools.push_back(t);
        }
        tool_config["tools"] = tools;
        body["toolConfig"] = tool_config;
    }

    return body;
}

auto BedrockProvider::parse_response(const std::string& body) const
    -> Result<CompletionResponse> {
    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        return std::unexpected(make_error(
            ErrorCode::SerializationError,
            "Failed to parse Bedrock response",
            e.what()));
    }

    if (j.contains("error") || j.contains("__type")) {
        auto err_msg = j.value("message", j.value("Message", "Unknown error"));
        return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Bedrock API error",
            err_msg));
    }

    CompletionResponse response;
    response.model = default_model_;
    response.stop_reason = j.value("stopReason", "");

    // Parse usage
    if (j.contains("usage")) {
        response.input_tokens = j["usage"].value("inputTokens", 0);
        response.output_tokens = j["usage"].value("outputTokens", 0);
    }

    // Parse the output message
    response.message.id = utils::generate_id();
    response.message.role = Role::Assistant;
    response.message.created_at = Clock::now();

    if (j.contains("output") && j["output"].contains("message")) {
        const auto& msg = j["output"]["message"];

        if (msg.contains("content") && msg["content"].is_array()) {
            for (const auto& block : msg["content"]) {
                if (block.contains("text")) {
                    ContentBlock cb;
                    cb.type = "text";
                    cb.text = block["text"].get<std::string>();
                    response.message.content.push_back(std::move(cb));
                } else if (block.contains("toolUse")) {
                    ContentBlock cb;
                    cb.type = "tool_use";
                    cb.tool_use_id = block["toolUse"].value("toolUseId", "");
                    cb.tool_name = block["toolUse"].value("name", "");
                    if (block["toolUse"].contains("input")) {
                        cb.tool_input = block["toolUse"]["input"];
                    }
                    response.message.content.push_back(std::move(cb));
                }
            }
        }
    }

    return response;
}

auto BedrockProvider::parse_stream_event(const json& event,
                                          CompletionResponse& response,
                                          StreamCallback& cb) const -> void {
    // Bedrock Converse streaming events
    if (event.contains("messageStart")) {
        response.message.id = utils::generate_id();
        response.message.role = Role::Assistant;
        response.message.created_at = Clock::now();
    } else if (event.contains("contentBlockStart")) {
        const auto& start = event["contentBlockStart"];
        if (start.contains("start")) {
            const auto& s = start["start"];
            if (s.contains("toolUse")) {
                ContentBlock block;
                block.type = "tool_use";
                block.tool_use_id = s["toolUse"].value("toolUseId", "");
                block.tool_name = s["toolUse"].value("name", "");
                block.tool_input = json::object();
                response.message.content.push_back(std::move(block));

                CompletionChunk chunk;
                chunk.type = "tool_use";
                chunk.tool_name = s["toolUse"].value("name", "");
                cb(chunk);
            } else {
                ContentBlock block;
                block.type = "text";
                response.message.content.push_back(std::move(block));
            }
        }
    } else if (event.contains("contentBlockDelta")) {
        const auto& delta_wrapper = event["contentBlockDelta"];
        if (delta_wrapper.contains("delta")) {
            const auto& delta = delta_wrapper["delta"];
            if (delta.contains("text")) {
                auto text = delta["text"].get<std::string>();
                if (!response.message.content.empty()) {
                    response.message.content.back().text += text;
                }
                CompletionChunk chunk;
                chunk.type = "text";
                chunk.text = text;
                cb(chunk);
            } else if (delta.contains("toolUse")) {
                auto input_str = delta["toolUse"].value("input", "");
                if (!response.message.content.empty()) {
                    response.message.content.back().text += input_str;
                }
            }
        }
    } else if (event.contains("contentBlockStop")) {
        // Finalize tool_use blocks
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
    } else if (event.contains("messageStop")) {
        response.stop_reason = event["messageStop"].value("stopReason", "");
        CompletionChunk chunk;
        chunk.type = "stop";
        cb(chunk);
    } else if (event.contains("metadata")) {
        if (event["metadata"].contains("usage")) {
            response.input_tokens = event["metadata"]["usage"].value("inputTokens", 0);
            response.output_tokens = event["metadata"]["usage"].value("outputTokens", 0);
        }
    }
}

auto BedrockProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto model = req.model.empty() ? default_model_ : req.model;
    auto body = build_request_body(req);
    auto payload = body.dump();

    // Build the Converse API path
    auto path = "/model/" + model + "/converse";

    LOG_DEBUG("Bedrock complete request: model={}", model);

    // Sign the request
    auto sign_headers = sign_request("POST", path, payload,
                                      {{"content-type", "application/json"}});

    auto result = co_await http_.post(path, payload, "application/json", sign_headers);

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Bedrock API request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            auto msg = err_json.value("message",
                       err_json.value("Message", http_resp.body));
            co_return std::unexpected(make_error(
                ErrorCode::ProviderError,
                "Bedrock API error (HTTP " + std::to_string(http_resp.status) + ")",
                msg));
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Bedrock API error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    auto parsed = parse_response(http_resp.body);
    if (parsed.has_value()) {
        parsed->model = model;
    }
    co_return parsed;
}

auto BedrockProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    auto model = req.model.empty() ? default_model_ : req.model;
    auto body = build_request_body(req);
    auto payload = body.dump();

    // Build the ConverseStream API path
    auto path = "/model/" + model + "/converse-stream";

    LOG_DEBUG("Bedrock stream request: model={}", model);

    auto sign_headers = sign_request("POST", path, payload,
                                      {{"content-type", "application/json"}});

    auto result = co_await http_.post(path, payload, "application/json", sign_headers);

    if (!result.has_value()) {
        co_return std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Bedrock API streaming request failed",
            result.error().what()));
    }

    const auto& http_resp = result.value();

    if (!http_resp.is_success()) {
        try {
            auto err_json = json::parse(http_resp.body);
            auto msg = err_json.value("message",
                       err_json.value("Message", http_resp.body));
            co_return std::unexpected(make_error(
                ErrorCode::ProviderError,
                "Bedrock API stream error (HTTP " + std::to_string(http_resp.status) + ")",
                msg));
        } catch (...) {}

        co_return std::unexpected(make_error(
            ErrorCode::ProviderError,
            "Bedrock API stream error",
            "HTTP " + std::to_string(http_resp.status) + ": " + http_resp.body));
    }

    // Parse streaming response
    CompletionResponse response;
    response.model = model;

    auto sse_lines = parse_sse_lines(http_resp.body);
    for (const auto& line : sse_lines) {
        try {
            auto event = json::parse(line);
            parse_stream_event(event, response, cb);
        } catch (const json::parse_error& e) {
            LOG_WARN("Failed to parse Bedrock stream event: {}", e.what());
            continue;
        }
    }

    co_return response;
}

auto BedrockProvider::name() const -> std::string_view {
    return "bedrock";
}

auto BedrockProvider::models() const -> std::vector<std::string> {
    return {
        "anthropic.claude-3-5-sonnet-20241022-v2:0",
        "anthropic.claude-3-5-haiku-20241022-v1:0",
        "anthropic.claude-3-opus-20240229-v1:0",
        "anthropic.claude-3-sonnet-20240229-v1:0",
        "anthropic.claude-3-haiku-20240307-v1:0",
        "amazon.titan-text-premier-v1:0",
        "amazon.titan-text-express-v1",
        "meta.llama3-1-70b-instruct-v1:0",
        "meta.llama3-1-8b-instruct-v1:0",
        "mistral.mixtral-8x7b-instruct-v0:1",
    };
}

auto BedrockProvider::normalize_provider_alias(std::string_view alias) -> std::string {
    // v2026.2.24: Normalize common Bedrock provider name variants
    std::string lower(alias);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Replace underscores and spaces with hyphens for matching
    std::string normalized;
    normalized.reserve(lower.size());
    for (char c : lower) {
        if (c == '_' || c == ' ') {
            normalized += '-';
        } else {
            normalized += c;
        }
    }

    // Map variants to canonical name
    if (normalized == "bedrock" ||
        normalized == "aws-bedrock" ||
        normalized == "amazon-bedrock") {
        return "amazon-bedrock";
    }

    return std::string(alias);
}

} // namespace openclaw::providers
