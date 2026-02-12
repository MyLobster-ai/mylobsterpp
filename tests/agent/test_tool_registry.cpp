#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "openclaw/agent/tool_registry.hpp"

using namespace openclaw::agent;
using json = nlohmann::json;

/// Minimal stub tool for testing the registry.
class StubTool : public Tool {
public:
    StubTool(std::string name, std::string desc)
        : name_(std::move(name)), desc_(std::move(desc)) {}

    [[nodiscard]] auto definition() const -> ToolDefinition override {
        return ToolDefinition{
            .name = name_,
            .description = desc_,
            .parameters = {
                ToolParameter{
                    .name = "input",
                    .type = "string",
                    .description = "Input value",
                    .required = true,
                },
            },
        };
    }

    auto execute(json params) -> awaitable<openclaw::Result<json>> override {
        co_return json{{"echo", params}};
    }

private:
    std::string name_;
    std::string desc_;
};

TEST_CASE("ToolRegistry starts empty", "[agent][tool_registry]") {
    ToolRegistry reg;

    CHECK(reg.size() == 0);
    CHECK(reg.list().empty());
    CHECK_FALSE(reg.contains("anything"));
}

TEST_CASE("ToolRegistry register and lookup", "[agent][tool_registry]") {
    ToolRegistry reg;

    reg.register_tool(std::make_unique<StubTool>("calculator", "Perform math"));
    reg.register_tool(std::make_unique<StubTool>("web_search", "Search the web"));

    SECTION("size reflects registrations") {
        CHECK(reg.size() == 2);
    }

    SECTION("contains returns true for registered tool") {
        CHECK(reg.contains("calculator"));
        CHECK(reg.contains("web_search"));
    }

    SECTION("contains returns false for unknown tool") {
        CHECK_FALSE(reg.contains("nonexistent"));
    }

    SECTION("get returns registered tool") {
        auto* tool = reg.get("calculator");
        REQUIRE(tool != nullptr);
        CHECK(tool->definition().name == "calculator");
        CHECK(tool->definition().description == "Perform math");
    }

    SECTION("get returns nullptr for unknown tool") {
        CHECK(reg.get("missing") == nullptr);
    }

    SECTION("const get returns registered tool") {
        const auto& const_reg = reg;
        const auto* tool = const_reg.get("web_search");
        REQUIRE(tool != nullptr);
        CHECK(tool->definition().name == "web_search");
    }

    SECTION("list returns all tool definitions") {
        auto defs = reg.list();
        REQUIRE(defs.size() == 2);

        bool has_calc = false, has_search = false;
        for (const auto& d : defs) {
            if (d.name == "calculator") has_calc = true;
            if (d.name == "web_search") has_search = true;
        }
        CHECK(has_calc);
        CHECK(has_search);
    }
}

TEST_CASE("ToolRegistry register replaces existing tool", "[agent][tool_registry]") {
    ToolRegistry reg;

    reg.register_tool(std::make_unique<StubTool>("mytool", "Version 1"));
    reg.register_tool(std::make_unique<StubTool>("mytool", "Version 2"));

    CHECK(reg.size() == 1);
    auto* tool = reg.get("mytool");
    REQUIRE(tool != nullptr);
    CHECK(tool->definition().description == "Version 2");
}

TEST_CASE("ToolRegistry remove", "[agent][tool_registry]") {
    ToolRegistry reg;
    reg.register_tool(std::make_unique<StubTool>("removable", "To be removed"));

    SECTION("remove existing returns true") {
        CHECK(reg.remove("removable"));
        CHECK(reg.size() == 0);
        CHECK_FALSE(reg.contains("removable"));
    }

    SECTION("remove nonexistent returns false") {
        CHECK_FALSE(reg.remove("nope"));
        CHECK(reg.size() == 1);
    }
}

TEST_CASE("ToolRegistry clear", "[agent][tool_registry]") {
    ToolRegistry reg;
    reg.register_tool(std::make_unique<StubTool>("a", "Tool A"));
    reg.register_tool(std::make_unique<StubTool>("b", "Tool B"));
    reg.register_tool(std::make_unique<StubTool>("c", "Tool C"));

    REQUIRE(reg.size() == 3);
    reg.clear();
    CHECK(reg.size() == 0);
    CHECK(reg.list().empty());
}

TEST_CASE("ToolDefinition parameter metadata", "[agent][tool_registry]") {
    StubTool tool("test_tool", "A test");
    auto def = tool.definition();

    REQUIRE(def.parameters.size() == 1);
    CHECK(def.parameters[0].name == "input");
    CHECK(def.parameters[0].type == "string");
    CHECK(def.parameters[0].required == true);
}
