#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/infra/http_client.hpp"
#include "openclaw/providers/provider.hpp"

namespace openclaw::providers {

/// Kilo Gateway provider (v2026.2.23).
///
/// Anthropic-compatible API that proxies through the Kilo Gateway.
/// Default model: kilocode/anthropic/claude-opus-4.6
/// Supports implicit provider detection from model prefixes.
class KilocodeProvider final : public Provider {
public:
    /// Default base URL for the Kilo Gateway API.
    static constexpr std::string_view kDefaultBaseUrl = "https://api.kilocode.ai/v1";

    /// Default model for Kilo Gateway requests.
    static constexpr std::string_view kDefaultModel = "kilocode/anthropic/claude-opus-4.6";

    KilocodeProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~KilocodeProvider() override;

    KilocodeProvider(const KilocodeProvider&) = delete;
    KilocodeProvider& operator=(const KilocodeProvider&) = delete;

    auto complete(CompletionRequest req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(CompletionRequest req, StreamCallback cb)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto models() const -> std::vector<std::string> override;

    /// Returns true if the model string indicates implicit Kilo Gateway provider.
    [[nodiscard]] static auto is_kilocode_model(std::string_view model) -> bool;

private:
    std::string api_key_;
    std::string default_model_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
