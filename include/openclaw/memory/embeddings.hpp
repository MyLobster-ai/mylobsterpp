#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::memory {

using boost::asio::awaitable;

/// Abstract interface for generating text embeddings.
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    /// Embed a single text string into a float vector.
    virtual auto embed(std::string_view text)
        -> awaitable<Result<std::vector<float>>> = 0;

    /// Embed a batch of text strings.
    virtual auto embed_batch(std::vector<std::string> texts)
        -> awaitable<Result<std::vector<std::vector<float>>>> = 0;

    /// Returns the dimensionality of the embedding vectors produced.
    [[nodiscard]] virtual auto dimensions() const -> size_t = 0;
};

/// OpenAI text-embedding-3-small provider.
class OpenAIEmbeddings : public EmbeddingProvider {
public:
    /// Construct with the API key and io_context for HTTP requests.
    /// model defaults to "text-embedding-3-small".
    OpenAIEmbeddings(boost::asio::io_context& ioc,
                     std::string api_key,
                     std::string model = "text-embedding-3-small",
                     std::string base_url = "https://api.openai.com");

    ~OpenAIEmbeddings() override;

    OpenAIEmbeddings(const OpenAIEmbeddings&) = delete;
    OpenAIEmbeddings& operator=(const OpenAIEmbeddings&) = delete;
    OpenAIEmbeddings(OpenAIEmbeddings&&) noexcept;
    OpenAIEmbeddings& operator=(OpenAIEmbeddings&&) noexcept;

    auto embed(std::string_view text)
        -> awaitable<Result<std::vector<float>>> override;

    auto embed_batch(std::vector<std::string> texts)
        -> awaitable<Result<std::vector<std::vector<float>>>> override;

    [[nodiscard]] auto dimensions() const -> size_t override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openclaw::memory
