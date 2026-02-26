#include "openclaw/memory/manager.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"
#include "openclaw/memory/sqlite_vec.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <functional>
#include <unordered_map>

namespace openclaw::memory {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// MemoryEntry JSON serialization
// ---------------------------------------------------------------------------

void to_json(json& j, const MemoryEntry& e) {
    j = json{
        {"id", e.id},
        {"content", e.content},
        {"metadata", e.metadata},
        {"user_id", e.user_id},
        {"created_at", e.created_at},
        {"updated_at", e.updated_at},
    };
}

void from_json(const json& j, MemoryEntry& e) {
    j.at("id").get_to(e.id);
    j.at("content").get_to(e.content);
    if (j.contains("metadata")) {
        e.metadata = j["metadata"];
    }
    if (j.contains("user_id")) {
        j["user_id"].get_to(e.user_id);
    }
    if (j.contains("created_at")) {
        j["created_at"].get_to(e.created_at);
    }
    if (j.contains("updated_at")) {
        j["updated_at"].get_to(e.updated_at);
    }
}

// ---------------------------------------------------------------------------
// MemoryManager::Impl
// ---------------------------------------------------------------------------

struct MemoryManager::Impl {
    std::shared_ptr<EmbeddingProvider> embeddings;
    std::shared_ptr<VectorStore> vector_store;
    std::unique_ptr<HybridSearch> hybrid_search;
    std::unique_ptr<SQLite::Database> meta_db;
    bool ready = false;
    std::unordered_map<std::string, std::string> source_hashes;  // id -> content hash
};

// ---------------------------------------------------------------------------
// MemoryManager
// ---------------------------------------------------------------------------

MemoryManager::MemoryManager(boost::asio::io_context& ioc,
                             const MemoryConfig& config,
                             const std::string& openai_api_key,
                             const std::string& data_dir)
    : impl_(std::make_unique<Impl>()) {
    if (!config.enabled) {
        LOG_INFO("Memory system disabled by configuration");
        return;
    }

    // Determine database paths
    auto mem_dir = fs::path(data_dir) / "memory";
    fs::create_directories(mem_dir);

    auto vec_db_path = config.db_path.value_or((mem_dir / "vectors.db").string());
    auto fts_db_path = (mem_dir / "fts.db").string();
    auto meta_db_path = (mem_dir / "metadata.db").string();

    // Create embedding provider (OpenAI)
    impl_->embeddings = std::make_shared<OpenAIEmbeddings>(
        ioc, openai_api_key);
    auto dims = impl_->embeddings->dimensions();

    // Create vector store (SQLite-vec)
    auto sqlite_vec = std::make_shared<SqliteVecStore>(vec_db_path, dims);
    impl_->vector_store = sqlite_vec;

    // Create hybrid search engine
    impl_->hybrid_search = std::make_unique<HybridSearch>(
        impl_->vector_store, impl_->embeddings, fts_db_path);

    // Initialize metadata database
    init_metadata_db(meta_db_path);

    impl_->ready = true;
    LOG_INFO("MemoryManager initialized (dims={}, store={})", dims, config.store);
}

MemoryManager::~MemoryManager() = default;
MemoryManager::MemoryManager(MemoryManager&&) noexcept = default;
MemoryManager& MemoryManager::operator=(MemoryManager&&) noexcept = default;

void MemoryManager::init_metadata_db(const std::string& db_path) {
    try {
        impl_->meta_db = std::make_unique<SQLite::Database>(
            db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        impl_->meta_db->exec("PRAGMA journal_mode=WAL");
        impl_->meta_db->exec("PRAGMA synchronous=NORMAL");

        impl_->meta_db->exec(R"SQL(
            CREATE TABLE IF NOT EXISTS memories (
                id         TEXT PRIMARY KEY,
                content    TEXT NOT NULL,
                metadata   TEXT NOT NULL DEFAULT '{}',
                user_id    TEXT NOT NULL DEFAULT '',
                category   TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            )
        )SQL");

        impl_->meta_db->exec(R"SQL(
            CREATE INDEX IF NOT EXISTS idx_memories_user_id
            ON memories (user_id)
        )SQL");

        impl_->meta_db->exec(R"SQL(
            CREATE INDEX IF NOT EXISTS idx_memories_category
            ON memories (category)
        )SQL");

        LOG_DEBUG("Memory metadata database initialized: {}", db_path);
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to initialize memory metadata database: {}", e.what());
        throw;
    }
}

auto MemoryManager::store(std::string_view content, const StoreOptions& options)
    -> awaitable<Result<MemoryEntry>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }

    auto id = utils::generate_uuid();
    auto now = utils::timestamp_ms();

    // Build metadata with category
    json metadata = options.metadata;
    if (options.category) {
        metadata["category"] = *options.category;
    }
    if (options.user_id) {
        metadata["user_id"] = *options.user_id;
    }

    // Index in hybrid search (generates embedding + stores in vector + FTS)
    auto index_result = co_await impl_->hybrid_search->index(id, content, metadata);
    if (!index_result) {
        co_return make_fail(index_result.error());
    }

    // Store metadata entry
    try {
        SQLite::Statement stmt(*impl_->meta_db,
            "INSERT INTO memories (id, content, metadata, user_id, category, "
            "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, id);
        stmt.bind(2, std::string(content));
        stmt.bind(3, metadata.dump());
        stmt.bind(4, options.user_id.value_or(""));
        stmt.bind(5, options.category.value_or(""));
        stmt.bind(6, now);
        stmt.bind(7, now);
        stmt.exec();
    } catch (const SQLite::Exception& e) {
        co_return make_fail(
            make_error(ErrorCode::DatabaseError,
                       "Failed to store memory metadata",
                       e.what()));
    }

    MemoryEntry entry;
    entry.id = id;
    entry.content = std::string(content);
    entry.metadata = metadata;
    entry.user_id = options.user_id.value_or("");
    entry.created_at = now;
    entry.updated_at = now;

    LOG_DEBUG("Stored memory: {} (category={})",
              id, options.category.value_or("none"));
    co_return entry;
}

auto MemoryManager::recall(std::string_view query, const RecallOptions& options)
    -> awaitable<Result<std::vector<SearchResult>>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }

    SearchOptions search_opts;
    search_opts.limit = options.limit;
    search_opts.similarity_threshold = options.threshold;

    // Apply user_id and category as metadata filters
    if (options.user_id || options.category) {
        json filter;
        if (options.user_id) {
            filter["user_id"] = *options.user_id;
        }
        if (options.category) {
            filter["category"] = *options.category;
        }
        search_opts.metadata_filter = filter;
    }

    if (options.hybrid) {
        co_return co_await impl_->hybrid_search->search(query, search_opts);
    } else {
        co_return co_await impl_->hybrid_search->vector_search(query, options.limit);
    }
}

auto MemoryManager::forget(std::string_view id) -> awaitable<Result<void>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }

    // Remove from hybrid search (vector + FTS)
    auto rm_result = co_await impl_->hybrid_search->remove(id);
    if (!rm_result) {
        LOG_WARN("Failed to remove from hybrid search: {}",
                 rm_result.error().what());
    }

    // Remove metadata
    try {
        SQLite::Statement stmt(*impl_->meta_db,
            "DELETE FROM memories WHERE id = ?");
        stmt.bind(1, std::string(id));
        stmt.exec();
    } catch (const SQLite::Exception& e) {
        co_return make_fail(
            make_error(ErrorCode::DatabaseError,
                       "Failed to delete memory metadata",
                       e.what()));
    }

    LOG_DEBUG("Forgot memory: {}", std::string(id));
    co_return ok_result();
}

auto MemoryManager::get(std::string_view id) -> awaitable<Result<MemoryEntry>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }

    try {
        SQLite::Statement stmt(*impl_->meta_db,
            "SELECT id, content, metadata, user_id, created_at, updated_at "
            "FROM memories WHERE id = ?");
        stmt.bind(1, std::string(id));

        if (stmt.executeStep()) {
            MemoryEntry entry;
            entry.id = stmt.getColumn(0).getString();
            entry.content = stmt.getColumn(1).getString();
            entry.metadata = json::parse(stmt.getColumn(2).getString());
            entry.user_id = stmt.getColumn(3).getString();
            entry.created_at = stmt.getColumn(4).getInt64();
            entry.updated_at = stmt.getColumn(5).getInt64();
            co_return entry;
        }

        co_return make_fail(
            make_error(ErrorCode::NotFound,
                       "Memory not found",
                       std::string(id)));
    } catch (const SQLite::Exception& e) {
        co_return make_fail(
            make_error(ErrorCode::DatabaseError,
                       "Failed to get memory",
                       e.what()));
    }
}

auto MemoryManager::list(std::string_view user_id,
                         std::optional<std::string> category,
                         size_t limit)
    -> awaitable<Result<std::vector<MemoryEntry>>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }

    try {
        std::string sql =
            "SELECT id, content, metadata, user_id, created_at, updated_at "
            "FROM memories WHERE user_id = ?";
        if (category) {
            sql += " AND category = ?";
        }
        sql += " ORDER BY created_at DESC LIMIT ?";

        SQLite::Statement stmt(*impl_->meta_db, sql);
        int bind_idx = 1;
        stmt.bind(bind_idx++, std::string(user_id));
        if (category) {
            stmt.bind(bind_idx++, *category);
        }
        stmt.bind(bind_idx, static_cast<int>(limit));

        std::vector<MemoryEntry> entries;
        while (stmt.executeStep()) {
            MemoryEntry entry;
            entry.id = stmt.getColumn(0).getString();
            entry.content = stmt.getColumn(1).getString();
            entry.metadata = json::parse(stmt.getColumn(2).getString());
            entry.user_id = stmt.getColumn(3).getString();
            entry.created_at = stmt.getColumn(4).getInt64();
            entry.updated_at = stmt.getColumn(5).getInt64();
            entries.push_back(std::move(entry));
        }

        co_return entries;
    } catch (const SQLite::Exception& e) {
        co_return make_fail(
            make_error(ErrorCode::DatabaseError,
                       "Failed to list memories",
                       e.what()));
    }
}

auto MemoryManager::count() -> awaitable<Result<size_t>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }
    co_return co_await impl_->vector_store->count();
}

auto MemoryManager::clear() -> awaitable<Result<void>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }

    // Clear vector store
    auto vec_result = co_await impl_->vector_store->clear();
    if (!vec_result) {
        co_return make_fail(vec_result.error());
    }

    // Clear metadata
    try {
        impl_->meta_db->exec("DELETE FROM memories");
    } catch (const SQLite::Exception& e) {
        co_return make_fail(
            make_error(ErrorCode::DatabaseError,
                       "Failed to clear memory metadata",
                       e.what()));
    }

    LOG_INFO("Cleared all memories");
    co_return ok_result();
}

auto MemoryManager::reindex(std::string_view id, std::string_view new_content)
    -> awaitable<Result<void>> {
    if (!impl_->ready) {
        co_return make_fail(
            make_error(ErrorCode::MemoryError, "Memory system not initialized"));
    }

    // Compute simple hash (use std::hash for content)
    std::string content_hash = std::to_string(
        std::hash<std::string_view>{}(new_content));

    auto it = impl_->source_hashes.find(std::string(id));
    if (it != impl_->source_hashes.end() && it->second == content_hash) {
        LOG_DEBUG("Content unchanged for {}, skipping reindex", std::string(id));
        co_return ok_result();
    }

    // Re-embed and store
    auto embed_result = co_await impl_->embeddings->embed(new_content);
    if (!embed_result) {
        co_return make_fail(embed_result.error());
    }

    // Update in hybrid search
    auto remove_result = co_await impl_->hybrid_search->remove(id);
    (void)remove_result;  // OK if not found

    json metadata;
    auto index_result = co_await impl_->hybrid_search->index(id, new_content, metadata);
    if (!index_result) {
        co_return make_fail(index_result.error());
    }

    // Update metadata DB
    try {
        SQLite::Statement stmt(*impl_->meta_db,
            "UPDATE memories SET content = ?, updated_at = ? WHERE id = ?");
        stmt.bind(1, std::string(new_content));
        stmt.bind(2, utils::timestamp_ms());
        stmt.bind(3, std::string(id));
        stmt.exec();
    } catch (const SQLite::Exception& e) {
        co_return make_fail(
            make_error(ErrorCode::DatabaseError, "Failed to update memory", e.what()));
    }

    impl_->source_hashes[std::string(id)] = content_hash;
    LOG_DEBUG("Reindexed memory: {}", std::string(id));
    co_return ok_result();
}

auto MemoryManager::is_ready() const -> bool {
    return impl_ && impl_->ready;
}

} // namespace openclaw::memory
