#include "openclaw/memory/sqlite_vec.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <cstring>
#include <filesystem>

namespace openclaw::memory {

// ---------------------------------------------------------------------------
// SqliteVecStore
// ---------------------------------------------------------------------------

SqliteVecStore::SqliteVecStore(const std::string& db_path, size_t dimensions)
    : dimensions_(dimensions) {
    // Ensure parent directory exists
    auto parent = std::filesystem::path(db_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    try {
        db_ = std::make_unique<SQLite::Database>(
            db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db_->exec("PRAGMA journal_mode=WAL");
        db_->exec("PRAGMA synchronous=NORMAL");
        db_->exec("PRAGMA foreign_keys=ON");
        init_schema();
        LOG_INFO("SqliteVecStore opened: {} ({}D)", db_path, dimensions_);
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to open SqliteVecStore: {}", e.what());
        throw;
    }
}

SqliteVecStore::~SqliteVecStore() = default;
SqliteVecStore::SqliteVecStore(SqliteVecStore&&) noexcept = default;
SqliteVecStore& SqliteVecStore::operator=(SqliteVecStore&&) noexcept = default;

void SqliteVecStore::init_schema() {
    // Metadata table for content and metadata storage
    db_->exec(R"SQL(
        CREATE TABLE IF NOT EXISTS vec_entries (
            id          TEXT PRIMARY KEY,
            content     TEXT NOT NULL,
            metadata    TEXT NOT NULL DEFAULT '{}',
            created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now') * 1000)
        )
    )SQL");

    // Embeddings table storing raw float vectors as BLOBs.
    // We use a plain table since sqlite-vec's vec0 virtual table
    // may not be available at compile time on all platforms.
    // When the sqlite-vec extension is loaded, we create the virtual table instead.
    db_->exec(R"SQL(
        CREATE TABLE IF NOT EXISTS vec_embeddings (
            id        TEXT PRIMARY KEY,
            embedding BLOB NOT NULL,
            FOREIGN KEY (id) REFERENCES vec_entries(id) ON DELETE CASCADE
        )
    )SQL");

    // Try to create the vec0 virtual table for accelerated search.
    // This requires the sqlite-vec extension to be loaded.
    try {
        std::string create_vec0 =
            "CREATE VIRTUAL TABLE IF NOT EXISTS vec_index USING vec0("
            "id TEXT PRIMARY KEY, "
            "embedding float[" + std::to_string(dimensions_) + "]"
            ")";
        db_->exec(create_vec0);
        LOG_DEBUG("vec0 virtual table created/verified");
    } catch (const SQLite::Exception& e) {
        // sqlite-vec extension not available; fall back to brute-force search
        LOG_WARN("sqlite-vec extension not available, using brute-force search: {}",
                 e.what());
    }
}

auto SqliteVecStore::serialize_embedding(const std::vector<float>& vec) const
    -> std::string {
    std::string blob(vec.size() * sizeof(float), '\0');
    std::memcpy(blob.data(), vec.data(), blob.size());
    return blob;
}

auto SqliteVecStore::deserialize_embedding(const void* blob, size_t bytes) const
    -> std::vector<float> {
    size_t count = bytes / sizeof(float);
    std::vector<float> vec(count);
    std::memcpy(vec.data(), blob, bytes);
    return vec;
}

auto SqliteVecStore::insert(const VectorEntry& entry) -> awaitable<Result<void>> {
    try {
        SQLite::Transaction txn(*db_);

        // Insert metadata
        {
            SQLite::Statement stmt(*db_,
                "INSERT OR REPLACE INTO vec_entries (id, content, metadata) "
                "VALUES (?, ?, ?)");
            stmt.bind(1, entry.id);
            stmt.bind(2, entry.content);
            stmt.bind(3, entry.metadata.dump());
            stmt.exec();
        }

        // Insert embedding as blob
        {
            auto blob = serialize_embedding(entry.embedding);
            SQLite::Statement stmt(*db_,
                "INSERT OR REPLACE INTO vec_embeddings (id, embedding) "
                "VALUES (?, ?)");
            stmt.bind(1, entry.id);
            stmt.bind(2, blob.data(), static_cast<int>(blob.size()));
            stmt.exec();
        }

        // Try to insert into vec0 index (if available)
        try {
            auto blob = serialize_embedding(entry.embedding);
            SQLite::Statement stmt(*db_,
                "INSERT OR REPLACE INTO vec_index (id, embedding) "
                "VALUES (?, ?)");
            stmt.bind(1, entry.id);
            stmt.bind(2, blob.data(), static_cast<int>(blob.size()));
            stmt.exec();
        } catch (const SQLite::Exception&) {
            // vec0 not available; that's fine
        }

        txn.commit();
        LOG_DEBUG("Inserted vector entry: {}", entry.id);
        co_return Result<void>{};
    } catch (const SQLite::Exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "Failed to insert vector entry",
                       e.what()));
    }
}

auto SqliteVecStore::search(const std::vector<float>& query, size_t limit)
    -> awaitable<Result<std::vector<VectorEntry>>> {
    try {
        std::vector<VectorEntry> results;

        // First, try vec0 accelerated search
        bool used_vec0 = false;
        try {
            auto blob = serialize_embedding(query);
            SQLite::Statement stmt(*db_,
                "SELECT v.id, v.distance, e.content, e.metadata "
                "FROM vec_index v "
                "JOIN vec_entries e ON v.id = e.id "
                "WHERE v.embedding MATCH ? "
                "ORDER BY v.distance "
                "LIMIT ?");
            stmt.bind(1, blob.data(), static_cast<int>(blob.size()));
            stmt.bind(2, static_cast<int>(limit));

            while (stmt.executeStep()) {
                VectorEntry entry;
                entry.id = stmt.getColumn(0).getString();
                double distance = stmt.getColumn(1).getDouble();
                entry.score = 1.0 - distance;  // convert distance to similarity
                entry.content = stmt.getColumn(2).getString();
                entry.metadata = json::parse(stmt.getColumn(3).getString());
                results.push_back(std::move(entry));
            }
            used_vec0 = true;
        } catch (const SQLite::Exception&) {
            // vec0 not available; fall through to brute-force
        }

        if (!used_vec0) {
            // Brute-force cosine similarity search
            SQLite::Statement stmt(*db_,
                "SELECT e.id, e.content, e.metadata, v.embedding "
                "FROM vec_entries e "
                "JOIN vec_embeddings v ON e.id = v.id");

            // Precompute query magnitude
            double query_mag = 0.0;
            for (float f : query) {
                query_mag += static_cast<double>(f) * f;
            }
            query_mag = std::sqrt(query_mag);

            struct ScoredEntry {
                VectorEntry entry;
                double score;
            };
            std::vector<ScoredEntry> scored;

            while (stmt.executeStep()) {
                auto id = stmt.getColumn(0).getString();
                auto content = stmt.getColumn(1).getString();
                auto metadata_str = stmt.getColumn(2).getString();

                auto blob_col = stmt.getColumn(3);
                auto embedding = deserialize_embedding(
                    blob_col.getBlob(),
                    static_cast<size_t>(blob_col.getBytes()));

                // Compute cosine similarity
                double dot = 0.0;
                double emb_mag = 0.0;
                size_t len = std::min(query.size(), embedding.size());
                for (size_t i = 0; i < len; ++i) {
                    dot += static_cast<double>(query[i]) * embedding[i];
                    emb_mag += static_cast<double>(embedding[i]) * embedding[i];
                }
                emb_mag = std::sqrt(emb_mag);
                double cosine_sim = (query_mag > 0.0 && emb_mag > 0.0)
                                        ? dot / (query_mag * emb_mag)
                                        : 0.0;

                VectorEntry entry;
                entry.id = std::move(id);
                entry.content = std::move(content);
                entry.metadata = json::parse(metadata_str);
                entry.embedding = std::move(embedding);
                entry.score = cosine_sim;

                scored.push_back(ScoredEntry{std::move(entry), cosine_sim});
            }

            // Sort by score descending
            std::sort(scored.begin(), scored.end(),
                      [](const ScoredEntry& a, const ScoredEntry& b) {
                          return a.score > b.score;
                      });

            // Take top-k
            size_t take = std::min(limit, scored.size());
            results.reserve(take);
            for (size_t i = 0; i < take; ++i) {
                results.push_back(std::move(scored[i].entry));
            }
        }

        LOG_DEBUG("Vector search returned {} results", results.size());
        co_return results;
    } catch (const SQLite::Exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "Vector search failed",
                       e.what()));
    }
}

auto SqliteVecStore::remove(std::string_view id) -> awaitable<Result<void>> {
    try {
        SQLite::Transaction txn(*db_);

        // Remove from vec0 index (if available)
        try {
            SQLite::Statement stmt(*db_,
                "DELETE FROM vec_index WHERE id = ?");
            stmt.bind(1, std::string(id));
            stmt.exec();
        } catch (const SQLite::Exception&) {
            // vec0 not available
        }

        // Remove embedding
        {
            SQLite::Statement stmt(*db_,
                "DELETE FROM vec_embeddings WHERE id = ?");
            stmt.bind(1, std::string(id));
            stmt.exec();
        }

        // Remove entry (cascade should handle embeddings, but be explicit)
        {
            SQLite::Statement stmt(*db_,
                "DELETE FROM vec_entries WHERE id = ?");
            stmt.bind(1, std::string(id));
            stmt.exec();
        }

        txn.commit();
        LOG_DEBUG("Removed vector entry: {}", std::string(id));
        co_return Result<void>{};
    } catch (const SQLite::Exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "Failed to remove vector entry",
                       e.what()));
    }
}

auto SqliteVecStore::update(const VectorEntry& entry) -> awaitable<Result<void>> {
    // Update is implemented as remove + insert
    auto rm = co_await remove(entry.id);
    if (!rm) {
        co_return std::unexpected(rm.error());
    }
    co_return co_await insert(entry);
}

auto SqliteVecStore::count() -> awaitable<Result<size_t>> {
    try {
        SQLite::Statement stmt(*db_, "SELECT COUNT(*) FROM vec_entries");
        if (stmt.executeStep()) {
            co_return static_cast<size_t>(stmt.getColumn(0).getInt64());
        }
        co_return size_t{0};
    } catch (const SQLite::Exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "Failed to count vector entries",
                       e.what()));
    }
}

auto SqliteVecStore::clear() -> awaitable<Result<void>> {
    try {
        SQLite::Transaction txn(*db_);

        try {
            db_->exec("DELETE FROM vec_index");
        } catch (const SQLite::Exception&) {
            // vec0 not available
        }

        db_->exec("DELETE FROM vec_embeddings");
        db_->exec("DELETE FROM vec_entries");

        txn.commit();
        LOG_INFO("Cleared all vector entries");
        co_return Result<void>{};
    } catch (const SQLite::Exception& e) {
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError,
                       "Failed to clear vector store",
                       e.what()));
    }
}

// ---------------------------------------------------------------------------
// VectorEntry JSON serialization
// ---------------------------------------------------------------------------

void to_json(json& j, const VectorEntry& e) {
    j = json{
        {"id", e.id},
        {"content", e.content},
        {"metadata", e.metadata},
        {"score", e.score},
    };
}

void from_json(const json& j, VectorEntry& e) {
    j.at("id").get_to(e.id);
    j.at("content").get_to(e.content);
    if (j.contains("metadata")) {
        e.metadata = j["metadata"];
    }
    if (j.contains("score")) {
        j["score"].get_to(e.score);
    }
}

} // namespace openclaw::memory
