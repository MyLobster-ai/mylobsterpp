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

} // namespace openclaw::providers
