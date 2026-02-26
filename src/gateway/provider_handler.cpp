#include "openclaw/gateway/provider_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

// ---------------------------------------------------------------------------
// ProviderRegistry
// ---------------------------------------------------------------------------

void ProviderRegistry::add(std::string name,
                           std::shared_ptr<providers::Provider> provider) {
    if (providers_.empty()) {
        primary_name_ = name;
    }
    providers_[std::move(name)] = std::move(provider);
}

auto ProviderRegistry::get(std::string_view name)
    -> std::shared_ptr<providers::Provider> {
    auto it = providers_.find(std::string(name));
    if (it != providers_.end()) return it->second;
    return nullptr;
}

auto ProviderRegistry::list() const -> std::vector<std::string> {
    std::vector<std::string> names;
    names.reserve(providers_.size());
    for (const auto& [name, _] : providers_) {
        names.push_back(name);
    }
    return names;
}

auto ProviderRegistry::primary() -> std::shared_ptr<providers::Provider> {
    return get(primary_name_);
}

void ProviderRegistry::set_primary(std::string_view name) {
    primary_name_ = std::string(name);
}

// ---------------------------------------------------------------------------
// Handler registration
// ---------------------------------------------------------------------------

void register_provider_handlers(Protocol& protocol,
                                [[maybe_unused]] GatewayServer& server,
                                ProviderRegistry& providers) {
    // provider.list
    protocol.register_method("provider.list",
        [&providers]([[maybe_unused]] json params) -> awaitable<json> {
            auto names = providers.list();
            json result = json::array();
            for (const auto& name : names) {
                auto p = providers.get(name);
                result.push_back(json{
                    {"name", name},
                    {"type", p ? std::string(p->name()) : "unknown"},
                });
            }
            co_return json{{"providers", result}};
        },
        "List configured AI providers", "provider");

    // provider.chat
    protocol.register_method("provider.chat",
        [&providers]([[maybe_unused]] json params) -> awaitable<json> {
            auto provider_name = params.value("provider", "");
            auto p = provider_name.empty()
                ? providers.primary()
                : providers.get(provider_name);
            if (!p) {
                co_return json{{"ok", false}, {"error", "Provider not found"}};
            }

            providers::CompletionRequest req;
            req.model = params.value("model", "");
            if (params.contains("messages")) {
                for (const auto& msg : params["messages"]) {
                    Message m;
                    m.role = msg.value("role", Role::User);
                    m.content.push_back(ContentBlock{
                        .type = "text",
                        .text = msg.value("content", ""),
                    });
                    req.messages.push_back(std::move(m));
                }
            }
            if (params.contains("system_prompt")) {
                req.system_prompt = params.value("system_prompt", "");
            }
            if (params.contains("temperature")) {
                req.temperature = params.value("temperature", 0.7);
            }
            if (params.contains("max_tokens")) {
                req.max_tokens = params.value("max_tokens", 4096);
            }

            auto result = co_await p->complete(std::move(req));
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto& resp = result.value();
            std::string text;
            for (const auto& block : resp.message.content) {
                if (block.type == "text") text += block.text;
            }
            co_return json{
                {"ok", true},
                {"text", text},
                {"model", resp.model},
                {"input_tokens", resp.input_tokens},
                {"output_tokens", resp.output_tokens},
                {"stop_reason", resp.stop_reason},
            };
        },
        "Send a chat completion request", "provider");

    // provider.chat.stream
    protocol.register_method("provider.chat.stream",
        [&providers]([[maybe_unused]] json params) -> awaitable<json> {
            auto provider_name = params.value("provider", "");
            auto p = provider_name.empty()
                ? providers.primary()
                : providers.get(provider_name);
            if (!p) {
                co_return json{{"ok", false}, {"error", "Provider not found"}};
            }

            providers::CompletionRequest req;
            req.model = params.value("model", "");
            if (params.contains("messages")) {
                for (const auto& msg : params["messages"]) {
                    Message m;
                    m.role = msg.value("role", Role::User);
                    m.content.push_back(ContentBlock{
                        .type = "text",
                        .text = msg.value("content", ""),
                    });
                    req.messages.push_back(std::move(m));
                }
            }

            auto result = co_await p->stream(std::move(req),
                [](const providers::CompletionChunk& /*chunk*/) {
                    // Chunks are collected by the provider stream method.
                });
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto& resp = result.value();
            std::string text;
            for (const auto& block : resp.message.content) {
                if (block.type == "text") text += block.text;
            }
            co_return json{
                {"ok", true},
                {"text", text},
                {"model", resp.model},
                {"input_tokens", resp.input_tokens},
                {"output_tokens", resp.output_tokens},
            };
        },
        "Stream a chat completion", "provider");

    // provider.models
    protocol.register_method("provider.models",
        [&providers]([[maybe_unused]] json params) -> awaitable<json> {
            auto provider_name = params.value("provider", "");
            auto p = provider_name.empty()
                ? providers.primary()
                : providers.get(provider_name);
            if (!p) {
                co_return json{{"ok", false}, {"error", "Provider not found"}};
            }
            co_return json{{"models", p->models()}};
        },
        "List available models for a provider", "provider");

    // provider.embed
    protocol.register_method("provider.embed",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // Embedding is handled by the memory subsystem's EmbeddingProvider.
            co_return json{
                {"ok", false},
                {"error", "Use memory.embed for embedding generation"},
            };
        },
        "Generate embeddings via a provider", "provider");

    // provider.status
    protocol.register_method("provider.status",
        [&providers]([[maybe_unused]] json params) -> awaitable<json> {
            auto provider_name = params.value("provider", "");
            auto p = provider_name.empty()
                ? providers.primary()
                : providers.get(provider_name);
            co_return json{
                {"ok", true},
                {"available", p != nullptr},
                {"name", p ? std::string(p->name()) : "none"},
            };
        },
        "Check provider availability", "provider");

    // provider.configure
    protocol.register_method("provider.configure",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: runtime provider configuration changes.
            co_return json{{"ok", true}};
        },
        "Update provider configuration at runtime", "provider");

    // provider.usage
    protocol.register_method("provider.usage",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: track token usage statistics.
            co_return json{
                {"ok", true},
                {"total_input_tokens", 0},
                {"total_output_tokens", 0},
            };
        },
        "Get token/cost usage statistics", "provider");

    LOG_INFO("Registered provider handlers");
}

} // namespace openclaw::gateway
