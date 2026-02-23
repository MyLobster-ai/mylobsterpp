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

/// Mistral AI provider.
///
/// Communicates with the Mistral Chat Completions API at
/// https://api.mistral.ai/v1/chat/completions (OpenAI-compatible).
///
/// Supports streaming via SSE, function/tool calling.
/// Tool call IDs are sanitized to Mistral's strict9 format (alphanumeric, 9 chars).
class MistralProvider final : public Provider {
public:
    MistralProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~MistralProvider() override;

    MistralProvider(const MistralProvider&) = delete;
    MistralProvider& operator=(const MistralProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

private:
    auto build_request_body(const CompletionRequest& req, bool streaming) const -> json;
    auto convert_message(const Message& msg) const -> json;
    auto parse_response(const std::string& body) const -> Result<CompletionResponse>;
    auto parse_sse_chunk(const json& chunk, CompletionResponse& response,
                         StreamCallback& cb) const -> void;

    /// Sanitize a tool call ID to Mistral's strict9 format (alphanumeric only, 9 chars).
    [[nodiscard]] static auto sanitize_tool_call_id(std::string_view id) -> std::string;

    std::string api_key_;
    std::string default_model_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
