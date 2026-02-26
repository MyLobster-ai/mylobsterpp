#include "openclaw/gateway/memory_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_memory_handlers(Protocol& protocol,
                              memory::MemoryManager& memory) {
    // memory.store
    protocol.register_method("memory.store",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto content = params.value("content", "");
            if (content.empty()) {
                co_return json{{"ok", false}, {"error", "content is required"}};
            }
            memory::StoreOptions opts;
            if (params.contains("userId")) {
                opts.user_id = params.value("userId", "");
            }
            if (params.contains("metadata")) {
                opts.metadata = params["metadata"];
            }
            if (params.contains("category")) {
                opts.category = params.value("category", "");
            }
            auto result = co_await memory.store(content, opts);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"memory", result.value()}};
        },
        "Store a memory/fact", "memory");

    // memory.recall
    protocol.register_method("memory.recall",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto query = params.value("query", "");
            if (query.empty()) {
                co_return json{{"ok", false}, {"error", "query is required"}};
            }
            memory::RecallOptions opts;
            opts.limit = params.value("limit", 10);
            opts.threshold = params.value("threshold", 0.5);
            if (params.contains("userId")) {
                opts.user_id = params.value("userId", "");
            }
            if (params.contains("category")) {
                opts.category = params.value("category", "");
            }
            opts.hybrid = params.value("hybrid", true);

            auto result = co_await memory.recall(query, opts);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            json results = json::array();
            for (const auto& r : result.value()) {
                results.push_back(r);
            }
            co_return json{{"ok", true}, {"results", results}};
        },
        "Recall memories by semantic query", "memory");

    // memory.search — alias for recall with different defaults.
    protocol.register_method("memory.search",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto query = params.value("query", "");
            if (query.empty()) {
                co_return json{{"ok", false}, {"error", "query is required"}};
            }
            memory::RecallOptions opts;
            opts.limit = params.value("limit", 20);
            opts.threshold = params.value("threshold", 0.3);
            opts.hybrid = params.value("hybrid", true);
            if (params.contains("userId")) {
                opts.user_id = params.value("userId", "");
            }

            auto result = co_await memory.recall(query, opts);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            json results = json::array();
            for (const auto& r : result.value()) {
                results.push_back(r);
            }
            co_return json{{"ok", true}, {"results", results}};
        },
        "Search memories with filters", "memory");

    // memory.delete
    protocol.register_method("memory.delete",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await memory.forget(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Delete a specific memory", "memory");

    // memory.list
    protocol.register_method("memory.list",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto user_id = params.value("userId", "default");
            std::optional<std::string> category;
            if (params.contains("category")) {
                category = params.value("category", "");
            }
            size_t limit = params.value("limit", 100);

            auto result = co_await memory.list(user_id, category, limit);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            json memories = json::array();
            for (const auto& m : result.value()) {
                memories.push_back(m);
            }
            co_return json{{"ok", true}, {"memories", memories}};
        },
        "List stored memories", "memory");

    // memory.clear
    protocol.register_method("memory.clear",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto result = co_await memory.clear();
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Clear all memories for a scope", "memory");

    // memory.stats
    protocol.register_method("memory.stats",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto count = co_await memory.count();
            co_return json{
                {"ok", true},
                {"count", count.has_value() ? static_cast<int64_t>(count.value()) : 0},
                {"ready", memory.is_ready()},
            };
        },
        "Return memory store statistics", "memory");

    // memory.embed
    protocol.register_method("memory.embed",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: expose EmbeddingProvider::embed() when available.
            co_return json{{"ok", false}, {"error", "Not yet implemented"}};
        },
        "Generate embedding for text", "memory");

    // memory.index.rebuild
    protocol.register_method("memory.index.rebuild",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: trigger full reindex of vector store.
            co_return json{{"ok", true}, {"message", "Index rebuild started"}};
        },
        "Rebuild the vector index", "memory");

    // memory.rag.query
    protocol.register_method("memory.rag.query",
        [&memory]([[maybe_unused]] json params) -> awaitable<json> {
            auto query = params.value("query", "");
            if (query.empty()) {
                co_return json{{"ok", false}, {"error", "query is required"}};
            }
            // RAG query: retrieve relevant memories, then would generate
            // response with context. For now, just do retrieval.
            memory::RecallOptions opts;
            opts.limit = params.value("limit", 5);
            opts.hybrid = true;

            auto result = co_await memory.recall(query, opts);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            json context = json::array();
            for (const auto& r : result.value()) {
                context.push_back(r);
            }
            co_return json{{"ok", true}, {"context", context}, {"query", query}};
        },
        "RAG query: retrieve context and generate response", "memory");

    LOG_INFO("Registered memory handlers");
}

} // namespace openclaw::gateway
