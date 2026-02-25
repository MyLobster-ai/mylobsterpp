#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;

/// Parameters for the tools.catalog RPC method.
struct ToolsCatalogParams {
    std::optional<std::string> group;    // filter by tool group
    std::optional<std::string> profile;  // filter by tool profile
    bool include_hidden = false;         // include hidden/internal tools
};

/// A single tool entry in the catalog.
struct ToolCatalogEntry {
    std::string name;
    std::string description;
    std::string group;
    std::optional<std::string> plugin_source;  // plugin provenance if from a plugin
    bool hidden = false;
    json parameters_schema;
};

void to_json(json& j, const ToolCatalogEntry& e);
void from_json(const json& j, ToolCatalogEntry& e);

/// A group of related tools.
struct ToolCatalogGroup {
    std::string name;
    std::string description;
    std::vector<ToolCatalogEntry> tools;
};

void to_json(json& j, const ToolCatalogGroup& g);
void from_json(const json& j, ToolCatalogGroup& g);

/// A tool access profile (Minimal, Coding, Messaging, Full).
struct ToolCatalogProfile {
    std::string name;
    std::vector<std::string> included_groups;
};

void to_json(json& j, const ToolCatalogProfile& p);
void from_json(const json& j, ToolCatalogProfile& p);

/// Result of the tools.catalog RPC method.
struct ToolsCatalogResult {
    std::vector<ToolCatalogGroup> groups;
    std::vector<ToolCatalogProfile> profiles;
    int total_tools = 0;
};

void to_json(json& j, const ToolsCatalogResult& r);
void from_json(const json& j, ToolsCatalogResult& r);

/// Builds a ToolsCatalogResult from the current tool registry.
auto build_tools_catalog(const ToolsCatalogParams& params) -> Result<ToolsCatalogResult>;

} // namespace openclaw::gateway
