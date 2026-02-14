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

/// Ollama local LLM provider.
///
/// Communicates with the Ollama API at http://127.0.0.1:11434/api/chat
/// Uses NDJSON streaming (not SSE), with tool call accumulation across chunks.
class OllamaProvider final : public Provider {
public:
    OllamaProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~OllamaProvider() override;

    OllamaProvider(const OllamaProvider&) = delete;
    OllamaProvider& operator=(const OllamaProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

    /// Discover available models from the Ollama instance.
    auto discover_models() -> boost::asio::awaitable<Result<std::vector<std::string>>>;

private:
    /// Build the JSON request body for Ollama /api/chat.
    auto build_request_body(const CompletionRequest& req, bool streaming) const -> json;

    /// Convert a unified Message to Ollama message format.
    auto convert_message(const Message& msg) const -> json;

    /// Parse an NDJSON streaming response body.
    auto parse_ndjson_stream(const std::string& body, StreamCallback& cb)
        -> CompletionResponse;

    /// Parse a non-streaming response body.
    auto parse_response(const std::string& body) const -> Result<CompletionResponse>;

    std::string base_url_;
    std::string default_model_;
    infra::HttpClient http_;
    std::vector<std::string> discovered_models_;
};

} // namespace openclaw::providers
