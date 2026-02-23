#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/core/error.hpp"
#include "openclaw/memory/embeddings.hpp"
#include "openclaw/memory/search.hpp"
#include "openclaw/memory/vector_store.hpp"

namespace openclaw::memory {

using boost::asio::awaitable;
using json = nlohmann::json;

/// A memory entry stored and recalled by the manager.
struct MemoryEntry {
    std::string id;
    std::string content;
    json metadata;
    std::string user_id;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

void to_json(json& j, const MemoryEntry& e);
void from_json(const json& j, MemoryEntry& e);

/// Options for storing a memory.
struct StoreOptions {
    std::optional<std::string> user_id;
    json metadata;
    std::optional<std::string> category;  // e.g., "fact", "preference", "context"
};

/// Options for recalling memories.
struct RecallOptions {
    size_t limit = 10;
    double threshold = 0.5;
    std::optional<std::string> user_id;
    std::optional<std::string> category;
    bool hybrid = true;  // use hybrid search (vector + keyword)
};

/// High-level memory manager that coordinates embedding generation,
/// vector/keyword storage, and hybrid search for the RAG pipeline.
class MemoryManager {
public:
    /// Construct from configuration. Creates the embedding provider, vector store,
    /// and hybrid search engine based on config settings.
    MemoryManager(boost::asio::io_context& ioc, const MemoryConfig& config,
                  const std::string& openai_api_key,
                  const std::string& data_dir);
    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    MemoryManager(MemoryManager&&) noexcept;
    MemoryManager& operator=(MemoryManager&&) noexcept;

    /// Store a new memory. Generates embeddings and indexes for search.
    auto store(std::string_view content, const StoreOptions& options = {})
        -> awaitable<Result<MemoryEntry>>;

    /// Recall relevant memories for a given query.
    auto recall(std::string_view query, const RecallOptions& options = {})
        -> awaitable<Result<std::vector<SearchResult>>>;

    /// Delete a memory by its ID.
    auto forget(std::string_view id) -> awaitable<Result<void>>;

    /// Get a specific memory by ID.
    auto get(std::string_view id) -> awaitable<Result<MemoryEntry>>;

    /// List all memories for a user, optionally filtered by category.
    auto list(std::string_view user_id, std::optional<std::string> category = std::nullopt,
              size_t limit = 100)
        -> awaitable<Result<std::vector<MemoryEntry>>>;

    /// Get the total count of stored memories.
    auto count() -> awaitable<Result<size_t>>;

    /// Clear all stored memories.
    auto clear() -> awaitable<Result<void>>;

    /// Reindex a memory entry if its content has changed.
    auto reindex(std::string_view id, std::string_view new_content)
        -> awaitable<Result<void>>;

    /// Returns true if the memory system is properly initialized.
    [[nodiscard]] auto is_ready() const -> bool;

private:
    void init_metadata_db(const std::string& db_path);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openclaw::memory
