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

/// AWS Bedrock provider.
///
/// Communicates with the AWS Bedrock Runtime API using SigV4 request signing.
/// Supports Claude, Titan, and other models hosted on Bedrock.
///
/// The provider reads AWS credentials from the ProviderConfig:
///   - api_key format: "ACCESS_KEY_ID:SECRET_ACCESS_KEY" or use env vars
///   - base_url: optional override (default: constructed from region)
///   - model: the Bedrock model ID (e.g. "anthropic.claude-3-5-sonnet-20241022-v2:0")
///
/// Alternatively, AWS credentials can come from environment variables
/// AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, and AWS_DEFAULT_REGION.
class BedrockProvider final : public Provider {
public:
    /// Construct with an io_context reference and provider configuration.
    BedrockProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~BedrockProvider() override;

    BedrockProvider(const BedrockProvider&) = delete;
    BedrockProvider& operator=(const BedrockProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

    /// v2026.2.24: Normalize provider name aliases to canonical "amazon-bedrock".
    /// Maps "bedrock", "aws-bedrock", "aws_bedrock", "amazon bedrock" → "amazon-bedrock".
    [[nodiscard]] static auto normalize_provider_alias(std::string_view alias) -> std::string;

private:
    /// Build the JSON request body for the Bedrock Converse API.
    auto build_request_body(const CompletionRequest& req) const -> json;

    /// Parse a non-streaming Bedrock response.
    auto parse_response(const std::string& body) const -> Result<CompletionResponse>;

    /// Parse a streaming event from Bedrock.
    auto parse_stream_event(const json& event, CompletionResponse& response,
                            StreamCallback& cb) const -> void;

    /// Generate AWS SigV4 authorization headers for a request.
    auto sign_request(std::string_view method, std::string_view path,
                      std::string_view payload,
                      const std::map<std::string, std::string>& headers) const
        -> std::map<std::string, std::string>;

    /// Compute the SigV4 canonical request hash.
    auto canonical_request_hash(std::string_view method, std::string_view path,
                                std::string_view payload,
                                const std::map<std::string, std::string>& headers) const
        -> std::string;

    /// Compute the HMAC-SHA256 of data with the given key.
    static auto hmac_sha256(std::string_view key, std::string_view data)
        -> std::string;

    std::string access_key_id_;
    std::string secret_access_key_;
    std::string region_;
    std::string default_model_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
