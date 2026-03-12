#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/context_engine/context_engine.hpp"
#include "openclaw/context_engine/slot_registry.hpp"

using namespace openclaw::context_engine;
using json = nlohmann::json;

TEST_CASE("LegacyContextEngine basics", "[context_engine]") {
    LegacyContextEngine engine;

    SECTION("has correct identity") {
        CHECK(engine.id() == "legacy");
        CHECK(engine.name() == "Legacy Context Engine");
        CHECK(engine.supports_compaction());
        CHECK(engine.supports_subagents());
    }

    SECTION("hooks are non-null") {
        auto& hooks = engine.hooks();
        CHECK(hooks.bootstrap != nullptr);
        CHECK(hooks.ingest != nullptr);
        CHECK(hooks.assemble != nullptr);
        CHECK(hooks.compact != nullptr);
        CHECK(hooks.after_turn != nullptr);
        CHECK(hooks.prepare_subagent_spawn != nullptr);
        CHECK(hooks.on_subagent_ended != nullptr);
    }
}

TEST_CASE("SlotRegistry resolution", "[context_engine]") {
    SlotRegistry registry;

    SECTION("default is legacy") {
        auto engine = registry.active();
        REQUIRE(engine != nullptr);
        CHECK(engine->id() == "legacy");
    }

    SECTION("resolving with no plugin_id stays legacy") {
        ContextEngineSlotConfig config;
        registry.resolve(config);
        CHECK(registry.active()->id() == "legacy");
    }

    SECTION("resolving unknown plugin falls back to legacy") {
        ContextEngineSlotConfig config;
        config.plugin_id = "nonexistent";
        registry.resolve(config);
        CHECK(registry.active()->id() == "legacy");
    }

    SECTION("reset returns to legacy") {
        registry.reset();
        CHECK(registry.active()->id() == "legacy");
    }

    SECTION("has_engine returns false for unregistered") {
        CHECK_FALSE(registry.has_engine("nonexistent"));
    }
}

TEST_CASE("ContextEngineSlotConfig JSON serialization", "[context_engine]") {
    SECTION("round-trip with plugin_id") {
        ContextEngineSlotConfig config;
        config.plugin_id = "lossless-claw";
        config.plugin_config = {{"max_tokens", 100000}};
        json j = config;
        auto decoded = j.get<ContextEngineSlotConfig>();
        CHECK(decoded.plugin_id == "lossless-claw");
        CHECK(decoded.plugin_config["max_tokens"] == 100000);
    }

    SECTION("round-trip without plugin_id") {
        ContextEngineSlotConfig config;
        json j = config;
        auto decoded = j.get<ContextEngineSlotConfig>();
        CHECK_FALSE(decoded.plugin_id.has_value());
    }
}
