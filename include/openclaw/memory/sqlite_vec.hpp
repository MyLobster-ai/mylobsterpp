#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <SQLiteCpp/SQLiteCpp.h>

#include "openclaw/memory/vector_store.hpp"

namespace openclaw::memory {

/// Vector store implementation backed by SQLite with the sqlite-vec extension.
/// Uses cosine similarity for vector search via the vec0 virtual table.
class SqliteVecStore : public VectorStore {
public:
    /// Construct with a path to the SQLite database file.
    /// The database and required tables/virtual tables are created if they do not exist.
    /// dimensions specifies the embedding vector dimensionality.
    explicit SqliteVecStore(const std::string& db_path, size_t dimensions);
    ~SqliteVecStore() override;

    SqliteVecStore(const SqliteVecStore&) = delete;
    SqliteVecStore& operator=(const SqliteVecStore&) = delete;
    SqliteVecStore(SqliteVecStore&&) noexcept;
    SqliteVecStore& operator=(SqliteVecStore&&) noexcept;

    auto insert(const VectorEntry& entry) -> awaitable<Result<void>> override;
    auto search(const std::vector<float>& query, size_t limit)
        -> awaitable<Result<std::vector<VectorEntry>>> override;
    auto remove(std::string_view id) -> awaitable<Result<void>> override;
    auto update(const VectorEntry& entry) -> awaitable<Result<void>> override;
    auto count() -> awaitable<Result<size_t>> override;
    auto clear() -> awaitable<Result<void>> override;

private:
    void init_schema();
    auto serialize_embedding(const std::vector<float>& vec) const -> std::string;
    auto deserialize_embedding(const void* blob, size_t bytes) const -> std::vector<float>;

    std::unique_ptr<SQLite::Database> db_;
    size_t dimensions_;
};

} // namespace openclaw::memory
