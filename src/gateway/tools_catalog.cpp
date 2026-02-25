#include "openclaw/gateway/tools_catalog.hpp"

namespace openclaw::gateway {

void to_json(json& j, const ToolCatalogEntry& e) {
    j = json{
        {"name", e.name},
        {"description", e.description},
        {"group", e.group},
        {"hidden", e.hidden},
        {"parameters_schema", e.parameters_schema},
    };
    if (e.plugin_source) {
        j["plugin_source"] = *e.plugin_source;
    }
}

void from_json(const json& j, ToolCatalogEntry& e) {
    e.name = j.value("name", "");
    e.description = j.value("description", "");
    e.group = j.value("group", "");
    e.hidden = j.value("hidden", false);
    e.parameters_schema = j.value("parameters_schema", json::object());
    if (j.contains("plugin_source") && !j["plugin_source"].is_null()) {
        e.plugin_source = j.value("plugin_source", "");
    }
}

void to_json(json& j, const ToolCatalogGroup& g) {
    j = json{
        {"name", g.name},
        {"description", g.description},
        {"tools", g.tools},
    };
}

void from_json(const json& j, ToolCatalogGroup& g) {
    g.name = j.value("name", "");
    g.description = j.value("description", "");
    if (j.contains("tools") && j["tools"].is_array()) {
        g.tools = j["tools"].get<std::vector<ToolCatalogEntry>>();
    }
}

void to_json(json& j, const ToolCatalogProfile& p) {
    j = json{
        {"name", p.name},
        {"included_groups", p.included_groups},
    };
}

void from_json(const json& j, ToolCatalogProfile& p) {
    p.name = j.value("name", "");
    if (j.contains("included_groups") && j["included_groups"].is_array()) {
        p.included_groups = j["included_groups"].get<std::vector<std::string>>();
    }
}

void to_json(json& j, const ToolsCatalogResult& r) {
    j = json{
        {"groups", r.groups},
        {"profiles", r.profiles},
        {"total_tools", r.total_tools},
    };
}

void from_json(const json& j, ToolsCatalogResult& r) {
    if (j.contains("groups") && j["groups"].is_array()) {
        r.groups = j["groups"].get<std::vector<ToolCatalogGroup>>();
    }
    if (j.contains("profiles") && j["profiles"].is_array()) {
        r.profiles = j["profiles"].get<std::vector<ToolCatalogProfile>>();
    }
    r.total_tools = j.value("total_tools", 0);
}

auto build_tools_catalog(const ToolsCatalogParams& params) -> Result<ToolsCatalogResult> {
    ToolsCatalogResult result;

    // Default profiles
    result.profiles = {
        {"Minimal", {"core"}},
        {"Coding", {"core", "filesystem", "shell", "browser"}},
        {"Messaging", {"core", "channels", "delivery"}},
        {"Full", {"core", "filesystem", "shell", "browser", "channels",
                  "delivery", "memory", "cron", "sessions", "automation"}},
    };

    // Tool groups would be populated from the actual tool registry.
    // This is a stub that returns the catalog structure.
    result.total_tools = 0;
    for (const auto& group : result.groups) {
        result.total_tools += static_cast<int>(group.tools.size());
    }

    return result;
}

} // namespace openclaw::gateway
