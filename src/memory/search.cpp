#include "openclaw/memory/search.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace openclaw::memory {

// ---------------------------------------------------------------------------
// SearchResult JSON serialization
// ---------------------------------------------------------------------------

void to_json(json& j, const SearchResult& r) {
    j = json{
        {"id", r.id},
        {"content", r.content},
        {"metadata", r.metadata},
        {"vector_score", r.vector_score},
        {"keyword_score", r.keyword_score},
        {"combined_score", r.combined_score},
    };
}

void from_json(const json& j, SearchResult& r) {
    j.at("id").get_to(r.id);
    j.at("content").get_to(r.content);
    if (j.contains("metadata")) {
        r.metadata = j["metadata"];
    }
    if (j.contains("vector_score")) {
        j["vector_score"].get_to(r.vector_score);
    }
    if (j.contains("keyword_score")) {
        j["keyword_score"].get_to(r.keyword_score);
    }
    if (j.contains("combined_score")) {
        j["combined_score"].get_to(r.combined_score);
    }
}

// ---------------------------------------------------------------------------
// HybridSearch
// ---------------------------------------------------------------------------

HybridSearch::HybridSearch(std::shared_ptr<VectorStore> vector_store,
                           std::shared_ptr<EmbeddingProvider> embeddings,
                           const std::string& fts_db_path,
                           BM25Params bm25_params)
    : vector_store_(std::move(vector_store)),
      embeddings_(std::move(embeddings)),
      bm25_params_(bm25_params) {
    auto parent = std::filesystem::path(fts_db_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    try {
        fts_db_ = std::make_unique<SQLite::Database>(
            fts_db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        fts_db_->exec("PRAGMA journal_mode=WAL");
        fts_db_->exec("PRAGMA synchronous=NORMAL");
        init_fts_schema();
        LOG_INFO("HybridSearch FTS database opened: {}", fts_db_path);
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to open FTS database: {}", e.what());
        throw;
    }
}

HybridSearch::~HybridSearch() = default;
HybridSearch::HybridSearch(HybridSearch&&) noexcept = default;
HybridSearch& HybridSearch::operator=(HybridSearch&&) noexcept = default;

void HybridSearch::init_fts_schema() {
    // FTS5 virtual table for keyword search with BM25 ranking
    fts_db_->exec(R"SQL(
        CREATE VIRTUAL TABLE IF NOT EXISTS fts_content USING fts5(
            id UNINDEXED,
            content,
            metadata UNINDEXED,
            tokenize='porter unicode61'
        )
    )SQL");

    // Track document count and average length for BM25
    fts_db_->exec(R"SQL(
        CREATE TABLE IF NOT EXISTS fts_stats (
            key   TEXT PRIMARY KEY,
            value REAL NOT NULL
        )
    )SQL");
}

auto HybridSearch::index(std::string_view id, std::string_view content,
                         json metadata) -> awaitable<Result<void>> {
    // Generate embedding and store in vector store
    auto embed_result = co_await embeddings_->embed(content);
    if (!embed_result) {
        co_return std::unexpected(embed_result.error());
    }

    VectorEntry entry;
    entry.id = std::string(id);
    entry.content = std::string(content);
    entry.metadata = metadata;
    entry.embedding = std::move(*embed_result);

    auto insert_result = co_await vector_store_->insert(entry);
    if (!insert_result) {
        co_return std::unexpected(insert_result.error());
    }

    // Index in FTS5 for keyword search
    try {
        // Remove existing entry if present (FTS5 doesn't support UPSERT)
        {
            SQLite::Statement stmt(*fts_db_,
                "DELETE FROM fts_content WHERE id = ?");
            stmt.bind(1, std::string(id));
            stmt.exec();
        }

        {
            SQLite::Statement stmt(*fts_db_,
                "INSERT INTO fts_content (id, content, metadata) VALUES (?, ?, ?)");
            stmt.bind(1, std::string(id));
            stmt.bind(2, std::string(content));
            stmt.bind(3, metadata.dump());
            stmt.exec();
        }

        LOG_DEBUG("Indexed document for hybrid search: {}", std::string(id));
        co_return Result<void>{};
    } catch (const SQLite::Exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "Failed to index document in FTS",
                       e.what()));
    }
}

auto HybridSearch::search(std::string_view query, const SearchOptions& options)
    -> awaitable<Result<std::vector<SearchResult>>> {
    // Perform vector search
    auto vec_results = co_await vector_search(query, options.limit * 2);
    if (!vec_results) {
        co_return std::unexpected(vec_results.error());
    }

    // Perform keyword search
    auto kw_results = co_await keyword_search(query, options.limit * 2);
    if (!kw_results) {
        co_return std::unexpected(kw_results.error());
    }

    // Merge and rank results
    auto merged = merge_results(*vec_results, *kw_results, options);

    // Filter by similarity threshold
    std::vector<SearchResult> filtered;
    filtered.reserve(merged.size());
    for (auto& result : merged) {
        if (result.combined_score >= options.similarity_threshold) {
            filtered.push_back(std::move(result));
        }
    }

    // Apply limit
    if (filtered.size() > options.limit) {
        filtered.resize(options.limit);
    }

    // Apply metadata filter if provided
    if (options.metadata_filter) {
        std::erase_if(filtered, [&](const SearchResult& r) {
            for (auto& [key, val] : options.metadata_filter->items()) {
                if (!r.metadata.contains(key) || r.metadata[key] != val) {
                    return true;
                }
            }
            return false;
        });
    }

    LOG_DEBUG("Hybrid search for '{}' returned {} results",
              std::string(query), filtered.size());
    co_return filtered;
}

auto HybridSearch::remove(std::string_view id) -> awaitable<Result<void>> {
    // Remove from vector store
    auto vec_result = co_await vector_store_->remove(id);
    if (!vec_result) {
        LOG_WARN("Failed to remove from vector store: {}",
                 vec_result.error().what());
    }

    // Remove from FTS
    try {
        SQLite::Statement stmt(*fts_db_,
            "DELETE FROM fts_content WHERE id = ?");
        stmt.bind(1, std::string(id));
        stmt.exec();
    } catch (const SQLite::Exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "Failed to remove from FTS index",
                       e.what()));
    }

    co_return Result<void>{};
}

auto HybridSearch::vector_search(std::string_view query, size_t limit)
    -> awaitable<Result<std::vector<SearchResult>>> {
    auto embed_result = co_await embeddings_->embed(query);
    if (!embed_result) {
        co_return std::unexpected(embed_result.error());
    }

    auto vec_results = co_await vector_store_->search(*embed_result, limit);
    if (!vec_results) {
        co_return std::unexpected(vec_results.error());
    }

    std::vector<SearchResult> results;
    results.reserve(vec_results->size());
    for (auto& entry : *vec_results) {
        SearchResult r;
        r.id = std::move(entry.id);
        r.content = std::move(entry.content);
        r.metadata = std::move(entry.metadata);
        r.vector_score = entry.score;
        r.combined_score = entry.score;
        results.push_back(std::move(r));
    }

    co_return results;
}

auto HybridSearch::keyword_search(std::string_view query, size_t limit)
    -> awaitable<Result<std::vector<SearchResult>>> {
    auto result = compute_bm25(query, limit);
    if (!result) {
        co_return std::unexpected(result.error());
    }
    co_return std::move(*result);
}

auto HybridSearch::compute_bm25(std::string_view query, size_t limit)
    -> Result<std::vector<SearchResult>> {
    try {
        // FTS5 has built-in BM25 ranking via the bm25() function
        SQLite::Statement stmt(*fts_db_,
            "SELECT id, content, metadata, bm25(fts_content) AS rank "
            "FROM fts_content "
            "WHERE fts_content MATCH ? "
            "ORDER BY rank "
            "LIMIT ?");
        stmt.bind(1, std::string(query));
        stmt.bind(2, static_cast<int>(limit));

        std::vector<SearchResult> results;

        // Collect raw BM25 scores (negative, lower is better)
        double min_rank = 0.0;
        double max_rank = 0.0;
        bool first = true;

        struct RawResult {
            SearchResult result;
            double raw_rank;
        };
        std::vector<RawResult> raw_results;

        while (stmt.executeStep()) {
            SearchResult r;
            r.id = stmt.getColumn(0).getString();
            r.content = stmt.getColumn(1).getString();
            auto metadata_str = stmt.getColumn(2).getString();
            r.metadata = json::parse(metadata_str);
            double rank = stmt.getColumn(3).getDouble();

            if (first) {
                min_rank = max_rank = rank;
                first = false;
            } else {
                min_rank = std::min(min_rank, rank);
                max_rank = std::max(max_rank, rank);
            }

            raw_results.push_back(RawResult{std::move(r), rank});
        }

        // Normalize BM25 scores to [0, 1] range
        // BM25 scores from FTS5 are negative; more negative = better match
        double range = max_rank - min_rank;
        for (auto& rr : raw_results) {
            if (range > 0.0) {
                rr.result.keyword_score = (max_rank - rr.raw_rank) / range;
            } else {
                rr.result.keyword_score = raw_results.empty() ? 0.0 : 1.0;
            }
            rr.result.combined_score = rr.result.keyword_score;
            results.push_back(std::move(rr.result));
        }

        return results;
    } catch (const SQLite::Exception& e) {
        return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "BM25 keyword search failed",
                       e.what()));
    }
}

auto HybridSearch::merge_results(std::vector<SearchResult>& vector_results,
                                 std::vector<SearchResult>& keyword_results,
                                 const SearchOptions& options)
    -> std::vector<SearchResult> {
    // Build a map from ID to merged result
    std::unordered_map<std::string, SearchResult> merged_map;

    for (auto& r : vector_results) {
        auto& m = merged_map[r.id];
        m.id = r.id;
        m.content = std::move(r.content);
        m.metadata = std::move(r.metadata);
        m.vector_score = r.vector_score;
    }

    for (auto& r : keyword_results) {
        auto it = merged_map.find(r.id);
        if (it != merged_map.end()) {
            it->second.keyword_score = r.keyword_score;
        } else {
            auto& m = merged_map[r.id];
            m.id = r.id;
            m.content = std::move(r.content);
            m.metadata = std::move(r.metadata);
            m.keyword_score = r.keyword_score;
        }
    }

    // Compute combined score using weighted sum
    std::vector<SearchResult> results;
    results.reserve(merged_map.size());
    for (auto& [id, r] : merged_map) {
        r.combined_score = options.vector_weight * r.vector_score +
                           options.keyword_weight * r.keyword_score;
        results.push_back(std::move(r));
    }

    // Sort by combined score descending
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.combined_score > b.combined_score;
              });

    return results;
}

} // namespace openclaw::memory
