#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/infra/http_client.hpp"
#include "openclaw/providers/provider.hpp"

namespace openclaw::providers {

/// OpenAI GPT provider.
///
/// Communicates with the OpenAI Chat Completions API at
/// https://api.openai.com/v1/chat/completions
///
/// Supports streaming via Server-Sent Events (SSE), function/tool calling,
/// and is compatible with OpenAI-compatible APIs (e.g. local LLMs) via
/// a configurable base_url.
class OpenAIProvider final : public Provider {
public:
    /// Construct with an io_context reference and provider configuration.
    /// The config must contain at least `api_key`. Optional fields:
    ///   - base_url (default: "https://api.openai.com")
    ///   - model (default: "gpt-4o")
    ///   - organization
    OpenAIProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~OpenAIProvider() override;

    OpenAIProvider(const OpenAIProvider&) = delete;
    OpenAIProvider& operator=(const OpenAIProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

private:
    /// Build the JSON request body for the Chat Completions API.
    auto build_request_body(const CompletionRequest& req, bool streaming) const -> json;

    /// Convert a unified Message to the OpenAI message format.
    auto convert_message(const Message& msg) const -> json;

    /// Parse a non-streaming response body into a CompletionResponse.
    auto parse_response(const std::string& body) const -> Result<CompletionResponse>;

    /// Parse a single SSE data line during streaming.
    auto parse_sse_chunk(const json& chunk, CompletionResponse& response,
                         StreamCallback& cb) const -> void;

    std::string api_key_;
    std::string default_model_;
    std::optional<std::string> organization_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
