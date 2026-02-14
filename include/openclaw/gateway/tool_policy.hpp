#pragma once

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace openclaw::gateway {

using json = nlohmann::json;

/// Tool access profiles.
enum class ToolProfile {
    Minimal,     // Only basic safe tools
    Coding,      // Development-related tools
    Messaging,   // Channel/communication tools
    Full,        // All tools
};

/// Manages tool invocation authorization.
/// Supports owner-only tools, tool groups, profiles, and allow/deny overrides.
class ToolPolicy {
public:
    ToolPolicy() = default;

    /// Configure from gateway settings JSON.
    /// Expected keys: "tools.allow", "tools.deny", "tools.profile"
    void configure(const json& settings);

    /// Checks whether a tool is allowed for the given identity.
    /// Returns true if allowed, false if denied.
    [[nodiscard]] auto is_allowed(std::string_view tool_name,
                                   std::string_view identity) const -> bool;

    /// Sets the owner identity (has access to owner-only tools).
    void set_owner(std::string_view owner_identity);

    /// Returns true if the identity is the owner.
    [[nodiscard]] auto is_owner(std::string_view identity) const -> bool;

    /// Adds a tool to the explicit allow list.
    void allow(std::string_view tool_name);

    /// Adds a tool to the explicit deny list.
    void deny(std::string_view tool_name);

    /// Sets the active profile.
    void set_profile(ToolProfile profile);

    /// Expands a tool group name (e.g., "group:sessions") into individual tool names.
    [[nodiscard]] static auto expand_group(std::string_view group_name)
        -> std::vector<std::string>;

    /// Returns the set of owner-only tools.
    [[nodiscard]] static auto owner_only_tools() -> const std::unordered_set<std::string>&;

    /// Returns the tools included in a given profile.
    [[nodiscard]] static auto profile_tools(ToolProfile profile)
        -> std::unordered_set<std::string>;

private:
    std::string owner_identity_;
    ToolProfile profile_ = ToolProfile::Full;
    std::unordered_set<std::string> allow_list_;
    std::unordered_set<std::string> deny_list_;
};

} // namespace openclaw::gateway
