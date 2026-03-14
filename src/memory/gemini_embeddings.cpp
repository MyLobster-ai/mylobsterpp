#include "openclaw/memory/embeddings.hpp"

#include <cmath>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/infra/http_client.hpp"

namespace openclaw::memory {

using json = nlohmann::json;

struct GeminiEmbeddings::Impl {
    boost::asio::io_context& ioc;
    std::string api_key;
    std::string model;
    std::string base_url;
    size_t dims;
    infra::HttpClient http;

    Impl(boost::asio::io_context& ioc_, std::string key, std::string mdl,
         std::string url, size_t d)
        : ioc(ioc_), api_key(std::move(key)), model(std::move(mdl)),
          base_url(std::move(url)), dims(d),
          http(ioc_, infra::HttpClientConfig{.base_url = base_url}) {}
};

GeminiEmbeddings::GeminiEmbeddings(boost::asio::io_context& ioc,
                                   std::string api_key,
                                   std::string model,
                                   std::string base_url,
                                   size_t dimensions)
    : impl_(std::make_unique<Impl>(ioc, std::move(api_key), std::move(model),
                                   std::move(base_url), dimensions)) {}

GeminiEmbeddings::~GeminiEmbeddings() = default;
GeminiEmbeddings::GeminiEmbeddings(GeminiEmbeddings&&) noexcept = default;
GeminiEmbeddings& GeminiEmbeddings::operator=(GeminiEmbeddings&&) noexcept = default;

void GeminiEmbeddings::normalize(std::vector<float>& vec) {
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 0.0f) {
        for (float& v : vec) v /= norm;
    }
}

auto GeminiEmbeddings::embed(std::string_view text)
    -> awaitable<Result<std::vector<float>>> {
    // Truncate to 32K chars
    auto truncated = text.substr(0, 32000);

    json body = {
        {"model", "models/" + impl_->model},
        {"content", {{"parts", {{{"text", std::string(truncated)}}}}}},
    };
    if (impl_->dims > 0) {
        body["outputDimensionality"] = impl_->dims;
    }

    auto path = "/v1beta/models/" + impl_->model +
                ":embedContent?key=" + impl_->api_key;

    auto response = co_await impl_->http.post(path, body.dump());
    if (!response || !response->is_success()) {
        co_return make_fail(make_error(ErrorCode::ProviderError, "Gemini embedding request failed"));
    }

    try {
        auto resp_json = json::parse(response->body);
        auto embedding = resp_json.at("embedding").at("values").get<std::vector<float>>();
        normalize(embedding);
        co_return embedding;
    } catch (const std::exception& e) {
        co_return make_fail(make_error(ErrorCode::ProviderError, std::string("Failed to parse Gemini embedding: ") + e.what()));
    }
}

auto GeminiEmbeddings::embed_batch(std::vector<std::string> texts)
    -> awaitable<Result<std::vector<std::vector<float>>>> {
    json requests = json::array();
    for (const auto& text : texts) {
        auto truncated = text.substr(0, 32000);
        json req = {
            {"model", "models/" + impl_->model},
            {"content", {{"parts", {{{"text", truncated}}}}}},
        };
        if (impl_->dims > 0) {
            req["outputDimensionality"] = impl_->dims;
        }
        requests.push_back(std::move(req));
    }

    json body = {{"requests", requests}};
    auto path = "/v1beta/models/" + impl_->model +
                ":batchEmbedContents?key=" + impl_->api_key;

    auto response = co_await impl_->http.post(path, body.dump());
    if (!response || !response->is_success()) {
        co_return make_fail(make_error(ErrorCode::ProviderError, "Gemini batch embedding request failed"));
    }

    try {
        auto resp_json = json::parse(response->body);
        std::vector<std::vector<float>> results;
        for (const auto& emb : resp_json.at("embeddings")) {
            auto vec = emb.at("values").get<std::vector<float>>();
            normalize(vec);
            results.push_back(std::move(vec));
        }
        co_return results;
    } catch (const std::exception& e) {
        co_return make_fail(make_error(ErrorCode::ProviderError, std::string("Failed to parse Gemini batch embeddings: ") + e.what()));
    }
}

auto GeminiEmbeddings::dimensions() const -> size_t {
    return impl_->dims;
}

} // namespace openclaw::memory
