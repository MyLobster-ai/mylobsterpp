#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "openclaw/context_engine/context_engine.hpp"

namespace openclaw::context_engine {

using json = nlohmann::json;

/// v2026.3.7: Configuration for the context engine slot.
struct ContextEngineSlotConfig {
    std::optional<std::string> plugin_id;  // Plugin to use (nullopt = legacy)
    json plugin_config;                     // Plugin-specific configuration
};

inline void to_json(json& j, const ContextEngineSlotConfig& c) {
    j = json{{"plugin_config", c.plugin_config}};
    if (c.plugin_id) j["plugin_id"] = *c.plugin_id;
}

inline void from_json(const json& j, ContextEngineSlotConfig& c) {
    if (j.contains("plugin_id") && !j["plugin_id"].is_null())
        c.plugin_id = j["plugin_id"].get<std::string>();
    if (j.contains("plugin_config"))
        c.plugin_config = j["plugin_config"];
}

/// v2026.3.7: Slot-based registry for context engine plugins.
/// Maintains the active context engine and handles resolution.
class SlotRegistry {
public:
    SlotRegistry();
    ~SlotRegistry();

    /// Set the active context engine from config.
    /// If no plugin_id specified, uses LegacyContextEngine.
    void resolve(const ContextEngineSlotConfig& config);

    /// Get the active context engine.
    [[nodiscard]] auto active() const -> std::shared_ptr<ContextEngine>;

    /// Register a custom context engine implementation.
    void register_engine(std::shared_ptr<ContextEngine> engine);

    /// Check if a custom engine is registered with the given ID.
    [[nodiscard]] auto has_engine(std::string_view id) const -> bool;

    /// Reset to legacy engine.
    void reset();

private:
    mutable std::mutex mutex_;
    std::shared_ptr<ContextEngine> active_;
    std::unordered_map<std::string, std::shared_ptr<ContextEngine>> engines_;
};

} // namespace openclaw::context_engine
