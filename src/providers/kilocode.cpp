#include "openclaw/providers/kilocode.hpp"
#include "openclaw/core/logger.hpp"

namespace openclaw::providers {

KilocodeProvider::KilocodeProvider(boost::asio::io_context& ioc, const ProviderConfig& config)
    : api_key_(config.api_key)
    , default_model_(config.model.value_or(std::string(kDefaultModel)))
    , http_(ioc, infra::HttpClientConfig{
          .base_url = config.base_url.value_or(std::string(kDefaultBaseUrl)),
          .timeout_seconds = 120,
          .default_headers = {
              {"Authorization", "Bearer " + config.api_key},
              {"Content-Type", "application/json"},
          },
      })
{
    LOG_INFO("[kilocode] Provider initialized (model={})", default_model_);
}

KilocodeProvider::~KilocodeProvider() = default;

auto KilocodeProvider::complete(CompletionRequest req)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    // Stub: Kilo Gateway uses Anthropic-compatible API
    co_return make_fail(
        make_error(ErrorCode::ProviderError, "KilocodeProvider::complete not yet implemented"));
}

auto KilocodeProvider::stream(CompletionRequest req, StreamCallback cb)
    -> boost::asio::awaitable<Result<CompletionResponse>> {
    // Stub: Kilo Gateway uses Anthropic-compatible streaming
    co_return make_fail(
        make_error(ErrorCode::ProviderError, "KilocodeProvider::stream not yet implemented"));
}

auto KilocodeProvider::name() const -> std::string_view {
    return "kilocode";
}

auto KilocodeProvider::models() const -> std::vector<std::string> {
    return {
        "kilocode/anthropic/claude-opus-4.6",
        "kilocode/anthropic/claude-sonnet-4.6",
        "kilocode/anthropic/claude-haiku-4.5",
        "kilocode/openai/gpt-4o",
        "kilocode/google/gemini-pro",
    };
}

auto KilocodeProvider::is_kilocode_model(std::string_view model) -> bool {
    return model.starts_with("kilocode/");
}

} // namespace openclaw::providers
