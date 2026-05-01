#include "openclaw/gateway/provider_handler.hpp"

#include <algorithm>
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

void ProviderRegistry::record_usage(std::string_view name, int input_tokens, int output_tokens) {
    auto& slot = usage_[std::string(name)];
    slot.input_tokens += static_cast<std::uint64_t>(std::max(0, input_tokens));
    slot.output_tokens += static_cast<std::uint64_t>(std::max(0, output_tokens));
    slot.request_count += 1;
}

auto ProviderRegistry::usage(std::string_view name) const -> ProviderUsage {
    auto it = usage_.find(std::string(name));
    if (it == usage_.end()) return {};
    return it->second;
}

auto ProviderRegistry::total_usage() const -> ProviderUsage {
    ProviderUsage total;
    for (const auto& [_, u] : usage_) {
        total.input_tokens += u.input_tokens;
        total.output_tokens += u.output_tokens;
        total.request_count += u.request_count;
    }
    return total;
}

void ProviderRegistry::reset_usage() {
    usage_.clear();
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
            providers.record_usage(provider_name.empty()
                                       ? std::string(providers.primary_name())
                                       : provider_name,
                                   resp.input_tokens, resp.output_tokens);
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
            providers.record_usage(provider_name.empty()
                                       ? std::string(providers.primary_name())
                                       : provider_name,
                                   resp.input_tokens, resp.output_tokens);
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
        [&providers]([[maybe_unused]] json params) -> awaitable<json> {
            // Currently the only runtime mutation supported is choosing the
            // primary provider. Per-provider credential / endpoint changes
            // require a process restart with new env (api_key, base_url),
            // since Provider implementations don't expose runtime
            // reconfiguration hooks.
            if (params.contains("primary")) {
                auto name = params.value("primary", "");
                if (name.empty()) {
                    co_return json{{"ok", false}, {"error", "primary cannot be empty"}};
                }
                if (!providers.get(name)) {
                    co_return json{{"ok", false}, {"error", "Provider not registered: " + name}};
                }
                providers.set_primary(name);
                co_return json{{"ok", true}, {"primary", name}};
            }
            co_return json{
                {"ok", false},
                {"error", "Only `primary` switching is supported at runtime; "
                          "restart with new env to change credentials"},
            };
        },
        "Update provider configuration at runtime", "provider");

    // provider.usage
    protocol.register_method("provider.usage",
        [&providers]([[maybe_unused]] json params) -> awaitable<json> {
            auto provider_name = params.value("provider", "");
            json per_provider = json::object();
            for (const auto& name : providers.list()) {
                auto u = providers.usage(name);
                per_provider[name] = json{
                    {"input_tokens", u.input_tokens},
                    {"output_tokens", u.output_tokens},
                    {"request_count", u.request_count},
                };
            }
            auto total = providers.total_usage();
            json result = {
                {"ok", true},
                {"total_input_tokens", total.input_tokens},
                {"total_output_tokens", total.output_tokens},
                {"total_requests", total.request_count},
                {"per_provider", per_provider},
            };
            if (!provider_name.empty()) {
                auto u = providers.usage(provider_name);
                result["provider"] = provider_name;
                result["input_tokens"] = u.input_tokens;
                result["output_tokens"] = u.output_tokens;
                result["request_count"] = u.request_count;
            }
            co_return result;
        },
        "Get token/cost usage statistics", "provider");

    LOG_INFO("Registered provider handlers");
}

} // namespace openclaw::gateway
