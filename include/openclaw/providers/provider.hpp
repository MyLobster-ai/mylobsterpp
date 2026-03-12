#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/core/types.hpp"

namespace openclaw::providers {

using json = nlohmann::json;
using boost::asio::awaitable;

/// Request to send to an AI provider for completion.
struct CompletionRequest {
    std::string model;
    std::vector<Message> messages;
    std::optional<std::string> system_prompt;
    std::optional<double> temperature;
    std::optional<int> max_tokens;
    std::vector<json> tools;
    ThinkingMode thinking = ThinkingMode::None;
    json custom_params = json::object();  // v2026.2.25: Provider-specific custom parameters
};

/// A chunk of a streaming completion response.
struct CompletionChunk {
    std::string type;  // "text", "tool_use", "thinking", "stop"
    std::string text;
    std::optional<std::string> tool_name;
    std::optional<json> tool_input;
};

/// Callback invoked for each chunk during streaming.
using StreamCallback = std::function<void(const CompletionChunk&)>;

/// Full completion response from a provider.
struct CompletionResponse {
    Message message;
    std::string model;
    int input_tokens = 0;
    int output_tokens = 0;
    std::string stop_reason;
};

/// Abstract base class for AI providers.
///
/// Each provider implementation knows how to communicate with a specific
/// AI service (Anthropic, OpenAI, AWS Bedrock, Google Gemini, etc.) and
/// translates between the unified CompletionRequest/CompletionResponse
/// types and the provider's native API format.
class Provider {
public:
    virtual ~Provider() = default;

    /// Perform a non-streaming completion request.
    virtual auto complete(CompletionRequest req)
        -> awaitable<Result<CompletionResponse>> = 0;

    /// Perform a streaming completion request, invoking the callback
    /// for each chunk as it arrives.
    virtual auto stream(CompletionRequest req, StreamCallback cb)
        -> awaitable<Result<CompletionResponse>> = 0;

    /// Return the provider name (e.g. "anthropic", "openai").
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /// Return the list of models supported by this provider.
    [[nodiscard]] virtual auto models() const -> std::vector<std::string> = 0;
};

/// Factory function type for creating providers.
using ProviderFactory = std::function<
    std::unique_ptr<Provider>(boost::asio::io_context&, const json& config)>;

// ---------------------------------------------------------------------------
// v2026.3.2: Failover error classification
// ---------------------------------------------------------------------------

/// Error categories for failover decisions.
enum class FailoverCategory {
    None,           // Not a failover-worthy error
    RateLimit,      // Rate limit hit, try another provider
    ServerError,    // Server-side error (5xx), may recover
    NetworkError,   // Network connectivity issue, try another
    AuthPermanent,  // Auth error (permission_error), try profile fallback
    Timeout,        // Request timeout, may recover
};

/// v2026.3.2: Classify an HTTP status code for failover decisions.
/// Treats HTTP 529 as rate_limit (was previously unclassified).
inline auto classify_http_failover(int status_code, std::string_view error_body = "")
    -> FailoverCategory {
    if (status_code == 429) return FailoverCategory::RateLimit;
    if (status_code == 529) return FailoverCategory::RateLimit;  // v2026.3.2
    if (status_code >= 500 && status_code < 600) return FailoverCategory::ServerError;
    if (status_code == 401 || status_code == 403) {
        // v2026.3.2: Classify permission_error as auth_permanent for profile fallback
        if (error_body.find("permission_error") != std::string_view::npos) {
            return FailoverCategory::AuthPermanent;
        }
        return FailoverCategory::AuthPermanent;
    }
    if (status_code == 408) return FailoverCategory::Timeout;
    return FailoverCategory::None;
}

/// v2026.3.2: Classify network error codes for failover.
/// ECONNREFUSED, ENETUNREACH, EHOSTUNREACH, ENETRESET, EAI_AGAIN are
/// all failover-worthy network errors.
inline auto classify_network_failover(std::string_view error_message)
    -> FailoverCategory {
    // Check for known network error patterns
    if (error_message.find("ECONNREFUSED") != std::string_view::npos ||
        error_message.find("connection refused") != std::string_view::npos) {
        return FailoverCategory::NetworkError;
    }
    if (error_message.find("ENETUNREACH") != std::string_view::npos ||
        error_message.find("network unreachable") != std::string_view::npos) {
        return FailoverCategory::NetworkError;
    }
    if (error_message.find("EHOSTUNREACH") != std::string_view::npos ||
        error_message.find("host unreachable") != std::string_view::npos) {
        return FailoverCategory::NetworkError;
    }
    if (error_message.find("ENETRESET") != std::string_view::npos ||
        error_message.find("network reset") != std::string_view::npos) {
        return FailoverCategory::NetworkError;
    }
    if (error_message.find("EAI_AGAIN") != std::string_view::npos ||
        error_message.find("temporary name resolution") != std::string_view::npos) {
        return FailoverCategory::NetworkError;
    }
    return FailoverCategory::None;
}

/// v2026.3.2: Check if an error message contains a rate limit indicator.
/// Matches "tpm" as a standalone token (word boundary) to avoid false positives
/// from incidental substrings like "attempting".
inline auto is_rate_limit_error(std::string_view error_message) -> bool {
    // Look for standalone "tpm" token
    auto pos = error_message.find("tpm");
    while (pos != std::string_view::npos) {
        // Check word boundaries
        bool start_boundary = (pos == 0) ||
            !std::isalnum(static_cast<unsigned char>(error_message[pos - 1]));
        bool end_boundary = (pos + 3 >= error_message.size()) ||
            !std::isalnum(static_cast<unsigned char>(error_message[pos + 3]));

        if (start_boundary && end_boundary) {
            return true;
        }
        pos = error_message.find("tpm", pos + 1);
    }

    // Also check for explicit rate limit phrases
    return error_message.find("rate limit") != std::string_view::npos ||
           error_message.find("rate_limit") != std::string_view::npos ||
           error_message.find("too many requests") != std::string_view::npos;
}

/// v2026.3.2: Clamp negative prompt/input token values to zero.
inline auto clamp_token_count(int value) -> int {
    return value < 0 ? 0 : value;
}

/// v2026.3.2: Only treat `@` as profile separator after final `/`.
inline auto parse_model_profile(std::string_view model_ref)
    -> std::pair<std::string, std::string> {
    auto last_slash = model_ref.rfind('/');
    auto search_start = (last_slash != std::string_view::npos) ? last_slash : 0;
    auto at_pos = model_ref.find('@', search_start);

    if (at_pos != std::string_view::npos) {
        return {
            std::string(model_ref.substr(0, at_pos)),
            std::string(model_ref.substr(at_pos + 1)),
        };
    }
    return {std::string(model_ref), ""};
}

/// v2026.3.7: HTTP 499 as transient error (Anthropic-style client-closed).
/// Poe 402 as billing error. Venice 402 as billing error for model fallback.
inline auto classify_billing_error(int status_code, std::string_view provider_name)
    -> FailoverCategory {
    if (status_code == 402) {
        // Venice and Poe 402 = billing exhaustion, trigger model fallback
        if (provider_name == "venice" || provider_name == "poe") {
            return FailoverCategory::RateLimit;
        }
    }
    if (status_code == 499) {
        return FailoverCategory::Timeout;  // Transient client-closed
    }
    return FailoverCategory::None;
}

/// v2026.3.11: Strip leaked model control tokens from response text.
/// Removes <|...|> (GPT/DeepSeek) and <｜...｜> (fullwidth, GLM-5) delimiters.
inline auto strip_control_tokens(std::string_view text) -> std::string {
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        // Check for <| or <｜ (fullwidth)
        bool is_control_start = false;
        size_t token_end = std::string_view::npos;

        if (i + 1 < text.size() && text[i] == '<' && text[i + 1] == '|') {
            // Standard <|...|> token
            auto end_pos = text.find("|>", i + 2);
            if (end_pos != std::string_view::npos) {
                is_control_start = true;
                token_end = end_pos + 2;
            }
        }
        // Check for fullwidth <｜...｜>
        // ｜ is U+FF5C (3 bytes in UTF-8: EF BD 9C)
        if (!is_control_start && i + 3 < text.size() && text[i] == '<') {
            if (static_cast<unsigned char>(text[i+1]) == 0xEF &&
                static_cast<unsigned char>(text[i+2]) == 0xBD &&
                static_cast<unsigned char>(text[i+3]) == 0x9C) {
                // Find closing ｜>
                auto pos = i + 4;
                while (pos + 3 < text.size()) {
                    if (static_cast<unsigned char>(text[pos]) == 0xEF &&
                        static_cast<unsigned char>(text[pos+1]) == 0xBD &&
                        static_cast<unsigned char>(text[pos+2]) == 0x9C &&
                        pos + 3 < text.size() && text[pos+3] == '>') {
                        is_control_start = true;
                        token_end = pos + 4;
                        break;
                    }
                    ++pos;
                }
            }
        }

        if (is_control_start && token_end != std::string_view::npos) {
            i = token_end;  // Skip the control token
        } else {
            result += text[i];
            ++i;
        }
    }
    return result;
}

/// v2026.3.7: Tool-result truncation with head+tail strategy.
/// Keeps first `head_chars` and last `tail_chars` of oversized results.
inline auto truncate_tool_result(std::string_view text, size_t max_chars,
                                  size_t head_ratio_pct = 70) -> std::string {
    if (text.size() <= max_chars) return std::string(text);

    size_t head_chars = max_chars * head_ratio_pct / 100;
    size_t tail_chars = max_chars - head_chars;

    std::string result;
    result.reserve(max_chars + 50);
    result += text.substr(0, head_chars);
    result += "\n\n... [truncated ";
    result += std::to_string(text.size() - max_chars);
    result += " chars] ...\n\n";
    result += text.substr(text.size() - tail_chars);
    return result;
}

/// v2026.3.8: Gemini MALFORMED_RESPONSE as retryable timeout.
inline auto is_gemini_retryable_error(std::string_view error_body) -> bool {
    return error_body.find("MALFORMED_RESPONSE") != std::string_view::npos;
}

/// v2026.3.8: Amazon Bedrock quota error detection.
inline auto is_bedrock_quota_error(std::string_view error_body) -> bool {
    return error_body.find("ThrottlingException") != std::string_view::npos ||
           error_body.find("ServiceQuotaExceededException") != std::string_view::npos;
}

/// v2026.3.7: XAI/Grok HTML entity decoding for tool call responses.
inline auto decode_html_entities(std::string_view text) -> std::string {
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '&') {
            if (text.substr(i).starts_with("&amp;")) { result += '&'; i += 5; }
            else if (text.substr(i).starts_with("&lt;")) { result += '<'; i += 4; }
            else if (text.substr(i).starts_with("&gt;")) { result += '>'; i += 4; }
            else if (text.substr(i).starts_with("&quot;")) { result += '"'; i += 6; }
            else if (text.substr(i).starts_with("&#39;") || text.substr(i).starts_with("&apos;")) {
                result += '\'';
                i += (text.substr(i).starts_with("&#39;") ? 5 : 6);
            }
            else { result += text[i]; ++i; }
        } else {
            result += text[i];
            ++i;
        }
    }
    return result;
}

} // namespace openclaw::providers
