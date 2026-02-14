#include "openclaw/gateway/tool_policy.hpp"

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

static const std::unordered_map<std::string, std::vector<std::string>> kToolGroups = {
    {"group:sessions", {"spawn", "send", "list"}},
    {"group:automation", {"gateway", "cron"}},
    {"group:memory", {"memory_search", "memory_store", "memory_delete"}},
    {"group:browser", {"browser_open", "browser_navigate", "browser_screenshot"}},
};

auto ToolPolicy::owner_only_tools() -> const std::unordered_set<std::string>& {
    static const std::unordered_set<std::string> tools = {
        "whatsapp_login",
    };
    return tools;
}

auto ToolPolicy::expand_group(std::string_view group_name) -> std::vector<std::string> {
    auto it = kToolGroups.find(std::string(group_name));
    if (it != kToolGroups.end()) {
        return it->second;
    }
    return {};
}

auto ToolPolicy::profile_tools(ToolProfile profile) -> std::unordered_set<std::string> {
    std::unordered_set<std::string> tools;

    // Minimal: only safe read-only tools
    tools.insert("help");
    tools.insert("version");
    tools.insert("health");

    if (profile == ToolProfile::Minimal) {
        return tools;
    }

    // Coding adds development tools
    tools.insert("code_search");
    tools.insert("code_edit");
    tools.insert("file_read");
    tools.insert("file_write");
    tools.insert("shell");
    tools.insert("git");

    if (profile == ToolProfile::Coding) {
        return tools;
    }

    // Messaging adds channel/communication tools
    for (const auto& t : expand_group("group:sessions")) tools.insert(t);
    tools.insert("send_message");
    tools.insert("broadcast");

    if (profile == ToolProfile::Messaging) {
        return tools;
    }

    // Full: everything including automation and browser
    for (const auto& [group_name, group_tools] : kToolGroups) {
        for (const auto& t : group_tools) tools.insert(t);
    }
    for (const auto& t : owner_only_tools()) tools.insert(t);

    return tools;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void ToolPolicy::configure(const json& settings) {
    // Profile
    if (settings.contains("tools.profile") || settings.contains("profile")) {
        std::string profile_str = settings.contains("tools.profile")
            ? settings["tools.profile"].get<std::string>()
            : settings.value("profile", "full");

        if (profile_str == "minimal") profile_ = ToolProfile::Minimal;
        else if (profile_str == "coding") profile_ = ToolProfile::Coding;
        else if (profile_str == "messaging") profile_ = ToolProfile::Messaging;
        else profile_ = ToolProfile::Full;
    }

    // Explicit allow list
    auto load_list = [&](const std::string& key, std::unordered_set<std::string>& target) {
        if (!settings.contains(key)) return;
        const auto& arr = settings[key];
        if (!arr.is_array()) return;
        for (const auto& item : arr) {
            std::string name = item.get<std::string>();
            // Expand groups
            if (name.starts_with("group:")) {
                for (const auto& t : expand_group(name)) {
                    target.insert(t);
                }
            } else {
                target.insert(name);
            }
        }
    };

    load_list("tools.allow", allow_list_);
    load_list("tools.deny", deny_list_);

    LOG_DEBUG("ToolPolicy configured: profile={}, allow={}, deny={}",
              static_cast<int>(profile_), allow_list_.size(), deny_list_.size());
}

// ---------------------------------------------------------------------------
// Authorization checks
// ---------------------------------------------------------------------------

void ToolPolicy::set_owner(std::string_view owner_identity) {
    owner_identity_ = std::string(owner_identity);
}

auto ToolPolicy::is_owner(std::string_view identity) const -> bool {
    return !owner_identity_.empty() && identity == owner_identity_;
}

void ToolPolicy::allow(std::string_view tool_name) {
    allow_list_.insert(std::string(tool_name));
}

void ToolPolicy::deny(std::string_view tool_name) {
    deny_list_.insert(std::string(tool_name));
}

void ToolPolicy::set_profile(ToolProfile profile) {
    profile_ = profile;
}

auto ToolPolicy::is_allowed(std::string_view tool_name,
                              std::string_view identity) const -> bool {
    std::string name(tool_name);

    // 1. Explicit deny always wins
    if (deny_list_.contains(name)) {
        return false;
    }

    // 2. Owner-only tools require owner identity
    if (owner_only_tools().contains(name)) {
        return is_owner(identity);
    }

    // 3. Explicit allow overrides profile restrictions
    if (allow_list_.contains(name)) {
        return true;
    }

    // 4. Check against active profile
    auto allowed = profile_tools(profile_);
    return allowed.contains(name);
}

} // namespace openclaw::gateway
