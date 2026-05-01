#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"
#include "openclaw/providers/provider.hpp"

namespace openclaw::gateway {

/// Per-provider running token totals.
struct ProviderUsage {
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::uint64_t request_count = 0;
};

/// Provider registry for runtime provider management.
class ProviderRegistry {
public:
    void add(std::string name, std::shared_ptr<providers::Provider> provider);
    [[nodiscard]] auto get(std::string_view name) -> std::shared_ptr<providers::Provider>;
    [[nodiscard]] auto list() const -> std::vector<std::string>;
    [[nodiscard]] auto primary() -> std::shared_ptr<providers::Provider>;
    [[nodiscard]] auto primary_name() const -> std::string_view { return primary_name_; }
    void set_primary(std::string_view name);

    /// Record a completed call against the named provider. Counters are
    /// per-provider and aggregate across the lifetime of the registry.
    void record_usage(std::string_view name, int input_tokens, int output_tokens);

    /// Read the current usage for one provider. Returns zeros for unknown names.
    [[nodiscard]] auto usage(std::string_view name) const -> ProviderUsage;

    /// Read aggregate usage across all providers.
    [[nodiscard]] auto total_usage() const -> ProviderUsage;

    /// Reset all counters to zero.
    void reset_usage();

private:
    std::unordered_map<std::string, std::shared_ptr<providers::Provider>> providers_;
    std::unordered_map<std::string, ProviderUsage> usage_;
    std::string primary_name_;
};

/// Registers provider.list, provider.chat, provider.chat.stream,
/// provider.models, provider.embed, provider.status, provider.configure,
/// provider.usage handlers on the protocol.
void register_provider_handlers(Protocol& protocol,
                                GatewayServer& server,
                                ProviderRegistry& providers);

} // namespace openclaw::gateway
