#include "openclaw/memory/embeddings.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/infra/http_client.hpp"

#include <nlohmann/json.hpp>

namespace openclaw::memory {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// OpenAIEmbeddings::Impl
// ---------------------------------------------------------------------------

struct OpenAIEmbeddings::Impl {
    infra::HttpClient http;
    std::string api_key;
    std::string model;
    size_t dims = 1536;

    Impl(boost::asio::io_context& ioc, std::string key, std::string mdl,
         std::string base_url)
        : http(ioc, infra::HttpClientConfig{
                         .base_url = std::move(base_url),
                         .timeout_seconds = 60,
                         .verify_ssl = true,
                         .default_headers = {
                             {"Content-Type", "application/json"},
                         },
                     }),
          api_key(std::move(key)),
          model(std::move(mdl)) {
        http.set_default_header("Authorization", "Bearer " + api_key);

        // Determine dimensions from model name
        if (model == "text-embedding-3-small") {
            dims = 1536;
        } else if (model == "text-embedding-3-large") {
            dims = 3072;
        } else if (model == "text-embedding-ada-002") {
            dims = 1536;
        }
    }
};

// ---------------------------------------------------------------------------
// OpenAIEmbeddings
// ---------------------------------------------------------------------------

OpenAIEmbeddings::OpenAIEmbeddings(boost::asio::io_context& ioc,
                                   std::string api_key,
                                   std::string model,
                                   std::string base_url)
    : impl_(std::make_unique<Impl>(ioc, std::move(api_key),
                                   std::move(model), std::move(base_url))) {}

OpenAIEmbeddings::~OpenAIEmbeddings() = default;
OpenAIEmbeddings::OpenAIEmbeddings(OpenAIEmbeddings&&) noexcept = default;
OpenAIEmbeddings& OpenAIEmbeddings::operator=(OpenAIEmbeddings&&) noexcept = default;

auto OpenAIEmbeddings::embed(std::string_view text)
    -> awaitable<Result<std::vector<float>>> {
    // Hard-cap input to ~8000 tokens (32000 chars)
    constexpr size_t kMaxEmbedChars = 32000;
    std::string input_text(text.substr(0, std::min(text.size(), kMaxEmbedChars)));

    json request_body = {
        {"model", impl_->model},
        {"input", input_text},
        {"encoding_format", "float"},
    };

    auto response = co_await impl_->http.post(
        "/v1/embeddings", request_body.dump());

    if (!response) {
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Embedding request failed",
                       response.error().what()));
    }

    if (!response->is_success()) {
        LOG_ERROR("OpenAI embeddings API returned status {}: {}",
                  response->status, response->body);
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Embedding API error",
                       "HTTP " + std::to_string(response->status) + ": " +
                           response->body));
    }

    try {
        auto body = json::parse(response->body);

        if (!body.contains("data") || body["data"].empty()) {
            co_return std::unexpected(
                make_error(ErrorCode::ProviderError,
                           "Embedding response missing data"));
        }

        auto& embedding_data = body["data"][0]["embedding"];
        std::vector<float> embedding;
        embedding.reserve(embedding_data.size());
        for (const auto& val : embedding_data) {
            embedding.push_back(val.get<float>());
        }

        LOG_DEBUG("Generated embedding with {} dimensions", embedding.size());
        co_return embedding;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse embedding response",
                       e.what()));
    }
}

auto OpenAIEmbeddings::embed_batch(std::vector<std::string> texts)
    -> awaitable<Result<std::vector<std::vector<float>>>> {
    if (texts.empty()) {
        co_return std::vector<std::vector<float>>{};
    }

    // Hard-cap each input to ~8000 tokens (32000 chars)
    constexpr size_t kMaxEmbedChars = 32000;
    std::vector<std::string> capped_texts;
    capped_texts.reserve(texts.size());
    for (auto& t : texts) {
        if (t.size() > kMaxEmbedChars) {
            capped_texts.push_back(t.substr(0, kMaxEmbedChars));
        } else {
            capped_texts.push_back(std::move(t));
        }
    }

    // OpenAI API supports batch embedding natively
    json request_body = {
        {"model", impl_->model},
        {"input", capped_texts},
        {"encoding_format", "float"},
    };

    auto response = co_await impl_->http.post(
        "/v1/embeddings", request_body.dump());

    if (!response) {
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Batch embedding request failed",
                       response.error().what()));
    }

    if (!response->is_success()) {
        LOG_ERROR("OpenAI embeddings API returned status {}: {}",
                  response->status, response->body);
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Batch embedding API error",
                       "HTTP " + std::to_string(response->status) + ": " +
                           response->body));
    }

    try {
        auto body = json::parse(response->body);

        if (!body.contains("data")) {
            co_return std::unexpected(
                make_error(ErrorCode::ProviderError,
                           "Batch embedding response missing data"));
        }

        // OpenAI returns embeddings sorted by index
        std::vector<std::vector<float>> results;
        results.resize(texts.size());

        for (const auto& item : body["data"]) {
            auto index = item["index"].get<size_t>();
            if (index >= results.size()) {
                continue;
            }
            auto& embedding_data = item["embedding"];
            std::vector<float> embedding;
            embedding.reserve(embedding_data.size());
            for (const auto& val : embedding_data) {
                embedding.push_back(val.get<float>());
            }
            results[index] = std::move(embedding);
        }

        LOG_DEBUG("Generated {} embeddings in batch", results.size());
        co_return results;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse batch embedding response",
                       e.what()));
    }
}

auto OpenAIEmbeddings::dimensions() const -> size_t {
    return impl_->dims;
}

// ---------------------------------------------------------------------------
// MistralEmbeddings::Impl
// ---------------------------------------------------------------------------

struct MistralEmbeddings::Impl {
    infra::HttpClient http;
    std::string api_key;
    std::string model;
    size_t dims = 1024;

    Impl(boost::asio::io_context& ioc, std::string key, std::string mdl,
         std::string base_url)
        : http(ioc, infra::HttpClientConfig{
                         .base_url = std::move(base_url),
                         .timeout_seconds = 60,
                         .verify_ssl = true,
                         .default_headers = {
                             {"Content-Type", "application/json"},
                         },
                     }),
          api_key(std::move(key)),
          model(std::move(mdl)) {
        http.set_default_header("Authorization", "Bearer " + api_key);

        // Mistral embed model always produces 1024-dimensional vectors
        dims = 1024;
    }
};

// ---------------------------------------------------------------------------
// MistralEmbeddings
// ---------------------------------------------------------------------------

MistralEmbeddings::MistralEmbeddings(boost::asio::io_context& ioc,
                                     std::string api_key,
                                     std::string model,
                                     std::string base_url)
    : impl_(std::make_unique<Impl>(ioc, std::move(api_key),
                                   std::move(model), std::move(base_url))) {}

MistralEmbeddings::~MistralEmbeddings() = default;
MistralEmbeddings::MistralEmbeddings(MistralEmbeddings&&) noexcept = default;
MistralEmbeddings& MistralEmbeddings::operator=(MistralEmbeddings&&) noexcept = default;

auto MistralEmbeddings::embed(std::string_view text)
    -> awaitable<Result<std::vector<float>>> {
    // Hard-cap input to ~8000 tokens (32000 chars)
    constexpr size_t kMaxEmbedChars = 32000;
    std::string input_text(text.substr(0, std::min(text.size(), kMaxEmbedChars)));

    json request_body = {
        {"model", impl_->model},
        {"input", input_text},
        {"encoding_format", "float"},
    };

    auto response = co_await impl_->http.post(
        "/v1/embeddings", request_body.dump());

    if (!response) {
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Mistral embedding request failed",
                       response.error().what()));
    }

    if (!response->is_success()) {
        LOG_ERROR("Mistral embeddings API returned status {}: {}",
                  response->status, response->body);
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Mistral embedding API error",
                       "HTTP " + std::to_string(response->status) + ": " +
                           response->body));
    }

    try {
        auto body = json::parse(response->body);

        if (!body.contains("data") || body["data"].empty()) {
            co_return std::unexpected(
                make_error(ErrorCode::ProviderError,
                           "Mistral embedding response missing data"));
        }

        auto& embedding_data = body["data"][0]["embedding"];
        std::vector<float> embedding;
        embedding.reserve(embedding_data.size());
        for (const auto& val : embedding_data) {
            embedding.push_back(val.get<float>());
        }

        LOG_DEBUG("Generated Mistral embedding with {} dimensions", embedding.size());
        co_return embedding;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Mistral embedding response",
                       e.what()));
    }
}

auto MistralEmbeddings::embed_batch(std::vector<std::string> texts)
    -> awaitable<Result<std::vector<std::vector<float>>>> {
    if (texts.empty()) {
        co_return std::vector<std::vector<float>>{};
    }

    // Hard-cap each input to ~8000 tokens (32000 chars)
    constexpr size_t kMaxEmbedChars = 32000;
    std::vector<std::string> capped_texts;
    capped_texts.reserve(texts.size());
    for (auto& t : texts) {
        if (t.size() > kMaxEmbedChars) {
            capped_texts.push_back(t.substr(0, kMaxEmbedChars));
        } else {
            capped_texts.push_back(std::move(t));
        }
    }

    // Mistral API supports batch embedding natively (same as OpenAI)
    json request_body = {
        {"model", impl_->model},
        {"input", capped_texts},
        {"encoding_format", "float"},
    };

    auto response = co_await impl_->http.post(
        "/v1/embeddings", request_body.dump());

    if (!response) {
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Mistral batch embedding request failed",
                       response.error().what()));
    }

    if (!response->is_success()) {
        LOG_ERROR("Mistral embeddings API returned status {}: {}",
                  response->status, response->body);
        co_return std::unexpected(
            make_error(ErrorCode::ProviderError,
                       "Mistral batch embedding API error",
                       "HTTP " + std::to_string(response->status) + ": " +
                           response->body));
    }

    try {
        auto body = json::parse(response->body);

        if (!body.contains("data")) {
            co_return std::unexpected(
                make_error(ErrorCode::ProviderError,
                           "Mistral batch embedding response missing data"));
        }

        // Mistral returns embeddings sorted by index (same as OpenAI)
        std::vector<std::vector<float>> results;
        results.resize(texts.size());

        for (const auto& item : body["data"]) {
            auto index = item["index"].get<size_t>();
            if (index >= results.size()) {
                continue;
            }
            auto& embedding_data = item["embedding"];
            std::vector<float> embedding;
            embedding.reserve(embedding_data.size());
            for (const auto& val : embedding_data) {
                embedding.push_back(val.get<float>());
            }
            results[index] = std::move(embedding);
        }

        LOG_DEBUG("Generated {} Mistral embeddings in batch", results.size());
        co_return results;
    } catch (const json::exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::SerializationError,
                       "Failed to parse Mistral batch embedding response",
                       e.what()));
    }
}

auto MistralEmbeddings::dimensions() const -> size_t {
    return impl_->dims;
}

// ---------------------------------------------------------------------------
// EmbeddingProviderChain
// ---------------------------------------------------------------------------

void EmbeddingProviderChain::add(std::unique_ptr<EmbeddingProvider> provider) {
    providers_.push_back(std::move(provider));
}

auto EmbeddingProviderChain::embed(std::string_view text)
    -> awaitable<Result<std::vector<float>>> {
    for (auto& provider : providers_) {
        auto result = co_await provider->embed(text);
        if (result) {
            co_return *result;
        }
        LOG_DEBUG("EmbeddingProviderChain: provider failed, trying next");
    }
    co_return std::unexpected(
        make_error(ErrorCode::ProviderError,
                   "All embedding providers failed"));
}

auto EmbeddingProviderChain::embed_batch(std::vector<std::string> texts)
    -> awaitable<Result<std::vector<std::vector<float>>>> {
    for (auto& provider : providers_) {
        auto result = co_await provider->embed_batch(texts);
        if (result) {
            co_return *result;
        }
        LOG_DEBUG("EmbeddingProviderChain: batch provider failed, trying next");
    }
    co_return std::unexpected(
        make_error(ErrorCode::ProviderError,
                   "All embedding providers failed for batch"));
}

auto EmbeddingProviderChain::dimensions() const -> size_t {
    if (!providers_.empty()) {
        return providers_.front()->dimensions();
    }
    return 0;
}

} // namespace openclaw::memory
