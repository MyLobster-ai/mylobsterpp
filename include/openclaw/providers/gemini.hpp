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

/// Google Gemini provider.
///
/// Communicates with the Google Generative Language API at
/// https://generativelanguage.googleapis.com/v1beta/models
///
/// Supports streaming via Server-Sent Events, function calling,
/// and multi-modal content.
class GeminiProvider final : public Provider {
public:
    /// Construct with an io_context reference and provider configuration.
    /// The config must contain at least `api_key`. Optional fields:
    ///   - base_url (default: "https://generativelanguage.googleapis.com")
    ///   - model (default: "gemini-2.0-flash")
    GeminiProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~GeminiProvider() override;

    GeminiProvider(const GeminiProvider&) = delete;
    GeminiProvider& operator=(const GeminiProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

private:
    /// Build the JSON request body for the Gemini generateContent API.
    auto build_request_body(const CompletionRequest& req) const -> json;

    /// Convert a unified Message to Gemini Content format.
    auto convert_message(const Message& msg) const -> json;

    /// Convert unified tool definitions to Gemini function declarations.
    auto convert_tools(const std::vector<json>& tools) const -> json;

    /// Parse a non-streaming Gemini response.
    auto parse_response(const std::string& body, const std::string& model) const
        -> Result<CompletionResponse>;

    /// Parse a streaming chunk from Gemini.
    auto parse_stream_chunk(const json& chunk, CompletionResponse& response,
                            StreamCallback& cb) const -> void;

    std::string api_key_;
    std::string default_model_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
