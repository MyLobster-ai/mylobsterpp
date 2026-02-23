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

/// Volcano Engine (Doubao) / BytePlus provider.
///
/// OpenAI-compatible API at https://ark.cn-beijing.volces.com/api/v3
/// (BytePlus: https://ark.bytepluscn.com/api/v3).
/// Uses endpoint IDs as model names.
class VolcEngineProvider final : public Provider {
public:
    VolcEngineProvider(boost::asio::io_context& ioc, const ProviderConfig& config);
    ~VolcEngineProvider() override;

    VolcEngineProvider(const VolcEngineProvider&) = delete;
    VolcEngineProvider& operator=(const VolcEngineProvider&) = delete;

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

    std::string api_key_;
    std::string default_model_;
    infra::HttpClient http_;
};

} // namespace openclaw::providers
