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

/// Model info for the Synthetic catalog.
struct SyntheticModelInfo {
    std::string id;           // e.g. "hf:deepseek-ai/DeepSeek-R1"
    std::string api_id;       // Resolved API identifier
    int context_length = 131072;
    int max_tokens = 8192;
    bool supports_reasoning = false;
};

/// Synthetic catalog provider.
///
/// Uses the Anthropic-compatible API at https://api.synthetic.new/anthropic
/// Provides access to 22+ models (MiniMax, DeepSeek, Qwen, GLM, Llama, Kimi)
/// through a unified API surface.
class SyntheticProvider final : public Provider {
public:
    SyntheticProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~SyntheticProvider() override;

    SyntheticProvider(const SyntheticProvider&) = delete;
    SyntheticProvider& operator=(const SyntheticProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

public:
    /// Check if a model ID suggests reasoning capability.
    static auto is_reasoning_model(const std::string& model_id) -> bool;

    /// Return the static model catalog.
    static auto static_catalog() -> const std::vector<SyntheticModelInfo>&;

    /// Resolve an hf: prefixed model ID to its API identifier.
    static auto resolve_hf_model(const std::string& model_id) -> std::string;

private:
    /// Build the JSON request body (Anthropic-compatible format).
    auto build_request_body(const CompletionRequest& req) const -> json;

    /// Parse a non-streaming response (Anthropic format).
    auto parse_response(const std::string& body) const -> Result<CompletionResponse>;

    /// Parse a single SSE event (Anthropic format).
    auto parse_sse_event(const json& event, CompletionResponse& response,
                         StreamCallback& cb) const -> void;

    /// Resolve a model ID, handling hf: prefix.
    auto resolve_model(const std::string& model_id) const -> std::string;

    std::string api_key_;
    std::string default_model_;
    std::string api_version_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
