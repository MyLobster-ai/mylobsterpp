#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "openclaw/core/error.hpp"

namespace openclaw::infra {

/// Registry for outbound channel plugins (message.send resolution).
///
/// When an agent invokes message.send, the outbound channel must be resolved
/// from the configured channel registry. This module handles the cold-start
/// bootstrap case where the channel plugin may not yet be loaded, and provides
/// actionable error messages when resolution fails.
class OutboundChannelResolver {
public:
    OutboundChannelResolver() = default;

    /// Register a channel plugin by normalized name.
    void register_channel(std::string name, std::string plugin_id);

    /// Remove a channel registration.
    auto unregister_channel(std::string_view name) -> bool;

    /// Resolve an outbound channel plugin by name.
    ///
    /// Normalizes the name (lowercase, trim whitespace) before lookup.
    /// Returns the plugin_id if found, or an actionable error describing
    /// what channels are available.
    [[nodiscard]] auto resolve_outbound_channel_plugin(std::string_view name) const
        -> Result<std::string>;

    /// Returns the number of registered channels.
    [[nodiscard]] auto channel_count() const noexcept -> size_t {
        return channels_.size();
    }

    /// Returns true if a channel with the given name is registered.
    [[nodiscard]] auto has_channel(std::string_view name) const -> bool;

private:
    /// Normalizes a channel name: lowercase, trim whitespace.
    [[nodiscard]] static auto normalize_name(std::string_view name) -> std::string;

    std::unordered_map<std::string, std::string> channels_;
};

} // namespace openclaw::infra
