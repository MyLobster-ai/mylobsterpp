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

} // namespace openclaw::providers
