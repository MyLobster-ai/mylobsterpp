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

/// Check if a model supports the 1M context beta.
auto is_1m_eligible_model(const std::string& model) -> bool;

/// Anthropic Claude provider.
///
/// Communicates with the Anthropic Messages API at
/// https://api.anthropic.com/v1/messages
///
/// Supports streaming via Server-Sent Events (SSE), tool use, and
/// extended thinking mode.
class AnthropicProvider final : public Provider {
public:
    /// Construct with an io_context reference and provider configuration.
    /// The config must contain at least `api_key`. Optional fields:
    ///   - base_url (default: "https://api.anthropic.com")
    ///   - model (default: "claude-sonnet-4-20250514")
    AnthropicProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~AnthropicProvider() override;

    AnthropicProvider(const AnthropicProvider&) = delete;
    AnthropicProvider& operator=(const AnthropicProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

private:
    /// Build the JSON request body for the Messages API.
    auto build_request_body(const CompletionRequest& req) const -> json;

    /// Parse a non-streaming response body into a CompletionResponse.
    auto parse_response(const std::string& body) const -> Result<CompletionResponse>;

    /// Parse a single SSE data line during streaming.
    auto parse_sse_event(const json& event, CompletionResponse& response,
                         StreamCallback& cb) const -> void;

    std::string api_key_;
    std::string default_model_;
    std::string api_version_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
