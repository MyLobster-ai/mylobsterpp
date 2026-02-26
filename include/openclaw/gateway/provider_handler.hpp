#pragma once

#include <memory>
#include <vector>

#include "openclaw/gateway/protocol.hpp"
#include "openclaw/gateway/server.hpp"
#include "openclaw/providers/provider.hpp"

namespace openclaw::gateway {

/// Provider registry for runtime provider management.
class ProviderRegistry {
public:
    void add(std::string name, std::shared_ptr<providers::Provider> provider);
    [[nodiscard]] auto get(std::string_view name) -> std::shared_ptr<providers::Provider>;
    [[nodiscard]] auto list() const -> std::vector<std::string>;
    [[nodiscard]] auto primary() -> std::shared_ptr<providers::Provider>;
    void set_primary(std::string_view name);

private:
    std::unordered_map<std::string, std::shared_ptr<providers::Provider>> providers_;
    std::string primary_name_;
};

/// Registers provider.list, provider.chat, provider.chat.stream,
/// provider.models, provider.embed, provider.status, provider.configure,
/// provider.usage handlers on the protocol.
void register_provider_handlers(Protocol& protocol,
                                GatewayServer& server,
                                ProviderRegistry& providers);

} // namespace openclaw::gateway
