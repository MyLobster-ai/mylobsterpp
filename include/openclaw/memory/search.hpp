#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

#include "openclaw/core/error.hpp"
#include "openclaw/memory/embeddings.hpp"
#include "openclaw/memory/vector_store.hpp"

namespace openclaw::memory {

using boost::asio::awaitable;
using json = nlohmann::json;

/// A search result combining vector similarity and keyword relevance.
struct SearchResult {
    std::string id;
    std::string content;
    json metadata;
    double vector_score = 0.0;   // cosine similarity score
    double keyword_score = 0.0;  // BM25 keyword relevance score
    double combined_score = 0.0; // weighted combination
};

void to_json(json& j, const SearchResult& r);
void from_json(const json& j, SearchResult& r);

/// Options for controlling hybrid search behavior.
struct SearchOptions {
    size_t limit = 10;
    double similarity_threshold = 0.5;
    double vector_weight = 0.7;    // weight for vector similarity [0,1]
    double keyword_weight = 0.3;   // weight for keyword BM25 score [0,1]
    std::optional<json> metadata_filter;
};

/// BM25 scoring parameters.
struct BM25Params {
    double k1 = 1.2;   // term frequency saturation
    double b = 0.75;    // document length normalization
};

/// Hybrid search engine combining vector similarity search with BM25 keyword search.
/// Vector similarity is delegated to the VectorStore.
/// Keyword search is done via SQLite FTS5 (full-text search).
class HybridSearch {
public:
    /// Construct with a vector store, embedding provider, and path to the SQLite
    /// FTS database (can be the same file as the vector store database).
    HybridSearch(std::shared_ptr<VectorStore> vector_store,
                 std::shared_ptr<EmbeddingProvider> embeddings,
                 const std::string& fts_db_path,
                 BM25Params bm25_params = {});
    ~HybridSearch();

    HybridSearch(const HybridSearch&) = delete;
    HybridSearch& operator=(const HybridSearch&) = delete;
    HybridSearch(HybridSearch&&) noexcept;
    HybridSearch& operator=(HybridSearch&&) noexcept;

    /// Index a document for both vector and keyword search.
    auto index(std::string_view id, std::string_view content, json metadata = {})
        -> awaitable<Result<void>>;

    /// Perform hybrid search combining vector similarity and BM25 keyword relevance.
    auto search(std::string_view query, const SearchOptions& options = {})
        -> awaitable<Result<std::vector<SearchResult>>>;

    /// Remove a document from both vector and keyword indices.
    auto remove(std::string_view id) -> awaitable<Result<void>>;

    /// Perform vector-only search.
    auto vector_search(std::string_view query, size_t limit = 10)
        -> awaitable<Result<std::vector<SearchResult>>>;

    /// Perform keyword-only BM25 search.
    auto keyword_search(std::string_view query, size_t limit = 10)
        -> awaitable<Result<std::vector<SearchResult>>>;

private:
    void init_fts_schema();
    auto compute_bm25(std::string_view query, size_t limit)
        -> Result<std::vector<SearchResult>>;
    auto merge_results(std::vector<SearchResult>& vector_results,
                       std::vector<SearchResult>& keyword_results,
                       const SearchOptions& options)
        -> std::vector<SearchResult>;

    std::shared_ptr<VectorStore> vector_store_;
    std::shared_ptr<EmbeddingProvider> embeddings_;
    std::unique_ptr<SQLite::Database> fts_db_;
    BM25Params bm25_params_;
};

} // namespace openclaw::memory
