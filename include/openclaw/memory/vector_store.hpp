#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::memory {

using boost::asio::awaitable;
using json = nlohmann::json;

/// A single entry in the vector store.
struct VectorEntry {
    std::string id;
    std::vector<float> embedding;
    std::string content;
    json metadata;
    double score = 0.0;  // similarity score (populated on search)
};

void to_json(json& j, const VectorEntry& e);
void from_json(const json& j, VectorEntry& e);

/// Abstract interface for vector storage and similarity search.
class VectorStore {
public:
    virtual ~VectorStore() = default;

    /// Insert a vector entry into the store.
    virtual auto insert(const VectorEntry& entry) -> awaitable<Result<void>> = 0;

    /// Search for the most similar vectors to the query.
    virtual auto search(const std::vector<float>& query, size_t limit)
        -> awaitable<Result<std::vector<VectorEntry>>> = 0;

    /// Remove an entry by its ID.
    virtual auto remove(std::string_view id) -> awaitable<Result<void>> = 0;

    /// Update an existing entry's content and embedding.
    virtual auto update(const VectorEntry& entry) -> awaitable<Result<void>> = 0;

    /// Count the total number of entries in the store.
    virtual auto count() -> awaitable<Result<size_t>> = 0;

    /// Remove all entries from the store.
    virtual auto clear() -> awaitable<Result<void>> = 0;
};

} // namespace openclaw::memory
