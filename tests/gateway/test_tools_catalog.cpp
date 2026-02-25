#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/tools_catalog.hpp"

using namespace openclaw::gateway;
using json = nlohmann::json;

TEST_CASE("ToolCatalogEntry JSON serialization", "[gateway][tools_catalog]") {
    ToolCatalogEntry entry;
    entry.name = "browser.navigate";
    entry.description = "Navigate to a URL";
    entry.group = "browser";
    entry.plugin_source = "core";
    entry.hidden = false;
    entry.parameters_schema = json{{"type", "object"}};

    json j = entry;
    CHECK(j["name"] == "browser.navigate");
    CHECK(j["description"] == "Navigate to a URL");
    CHECK(j["group"] == "browser");
    CHECK(j["plugin_source"] == "core");
    CHECK(j["hidden"] == false);
    CHECK(j["parameters_schema"]["type"] == "object");

    auto roundtrip = j.get<ToolCatalogEntry>();
    CHECK(roundtrip.name == entry.name);
    CHECK(roundtrip.description == entry.description);
    CHECK(roundtrip.group == entry.group);
    CHECK(roundtrip.plugin_source == entry.plugin_source);
}

TEST_CASE("ToolCatalogGroup JSON serialization", "[gateway][tools_catalog]") {
    ToolCatalogGroup group;
    group.name = "browser";
    group.description = "Browser automation tools";
    group.tools.push_back(ToolCatalogEntry{
        .name = "browser.navigate",
        .description = "Navigate to a URL",
        .group = "browser",
    });

    json j = group;
    CHECK(j["name"] == "browser");
    CHECK(j["description"] == "Browser automation tools");
    CHECK(j["tools"].size() == 1);
    CHECK(j["tools"][0]["name"] == "browser.navigate");
}

TEST_CASE("ToolCatalogProfile JSON serialization", "[gateway][tools_catalog]") {
    ToolCatalogProfile profile;
    profile.name = "Full";
    profile.included_groups = {"browser", "exec", "memory"};

    json j = profile;
    CHECK(j["name"] == "Full");
    CHECK(j["included_groups"].size() == 3);
    CHECK(j["included_groups"][0] == "browser");
}

TEST_CASE("ToolsCatalogResult JSON serialization", "[gateway][tools_catalog]") {
    ToolsCatalogResult result;
    result.total_tools = 42;
    result.groups.push_back(ToolCatalogGroup{
        .name = "browser",
        .description = "Browser tools",
    });
    result.profiles.push_back(ToolCatalogProfile{
        .name = "Full",
        .included_groups = {"browser"},
    });

    json j = result;
    CHECK(j["total_tools"] == 42);
    CHECK(j["groups"].size() == 1);
    CHECK(j["profiles"].size() == 1);

    auto roundtrip = j.get<ToolsCatalogResult>();
    CHECK(roundtrip.total_tools == 42);
    CHECK(roundtrip.groups.size() == 1);
    CHECK(roundtrip.profiles.size() == 1);
}

TEST_CASE("build_tools_catalog returns empty catalog", "[gateway][tools_catalog]") {
    ToolsCatalogParams params;
    auto result = build_tools_catalog(params);
    REQUIRE(result.has_value());
    CHECK(result->total_tools == 0);
    CHECK(result->groups.empty());
}
