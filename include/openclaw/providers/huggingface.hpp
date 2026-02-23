#pragma once

#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/infra/http_client.hpp"
#include "openclaw/providers/provider.hpp"

namespace openclaw::providers {

/// Model info for the HuggingFace static catalog.
struct HFModelInfo {
    std::string id;
    int context_length = 131072;
    int max_tokens = 8192;
    bool supports_reasoning = false;
};

/// HuggingFace Inference provider.
///
/// Uses the OpenAI-compatible API at https://router.huggingface.co/v1
/// Supports route policy suffixes (:cheapest, :fastest) and reasoning detection.
class HuggingFaceProvider final : public Provider {
public:
    HuggingFaceProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~HuggingFaceProvider() override;

    HuggingFaceProvider(const HuggingFaceProvider&) = delete;
    HuggingFaceProvider& operator=(const HuggingFaceProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

    /// Dynamically discover models from the HuggingFace API.
    auto discover_models() -> boost::asio::awaitable<Result<std::vector<HFModelInfo>>>;

public:
    /// Strip route policy suffix and return {clean_model, policy}.
    static auto strip_route_policy(const std::string& model)
        -> std::pair<std::string, std::string>;

    /// Check if a model ID suggests reasoning capability.
    static auto is_reasoning_model(const std::string& model_id) -> bool;

    /// Return the static model catalog.
    static auto static_catalog() -> const std::vector<HFModelInfo>&;

private:
    /// Build the JSON request body (OpenAI-compatible format).
    auto build_request_body(const CompletionRequest& req, bool streaming) const -> json;

    /// Convert a unified Message to the OpenAI message format.
    auto convert_message(const Message& msg) const -> json;

    /// Parse a non-streaming response.
    auto parse_response(const std::string& body) const -> Result<CompletionResponse>;

    /// Parse a single SSE data line during streaming.
    auto parse_sse_chunk(const json& chunk, CompletionResponse& response,
                         StreamCallback& cb) const -> void;

    std::string api_key_;
    std::string default_model_;
    infra::HttpClient http_;
    std::vector<HFModelInfo> discovered_models_;
};

} // namespace openclaw::providers
