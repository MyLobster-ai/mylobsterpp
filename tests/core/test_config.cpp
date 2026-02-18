#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "openclaw/core/config.hpp"

TEST_CASE("default_config returns sane defaults", "[config]") {
    auto cfg = openclaw::default_config();

    SECTION("gateway defaults") {
        CHECK(cfg.gateway.port == 18789);
        CHECK(cfg.gateway.bind == openclaw::BindMode::Loopback);
        CHECK(cfg.gateway.max_connections == 100);
    }

    SECTION("memory defaults") {
        CHECK(cfg.memory.enabled == true);
        CHECK(cfg.memory.store == "sqlite_vec");
        CHECK(cfg.memory.max_results == 10);
        CHECK(cfg.memory.similarity_threshold == 0.7);
    }

    SECTION("session defaults") {
        CHECK(cfg.sessions.store == "sqlite");
        CHECK(cfg.sessions.ttl_seconds == 86400);
    }

    SECTION("browser defaults") {
        CHECK(cfg.browser.enabled == false);
        CHECK(cfg.browser.pool_size == 2);
        CHECK(cfg.browser.timeout_ms == 30000);
    }

    SECTION("cron defaults") {
        CHECK(cfg.cron.enabled == false);
    }

    SECTION("log level defaults") {
        CHECK(cfg.log_level == "info");
    }

    SECTION("no providers or channels by default") {
        CHECK(cfg.providers.empty());
        CHECK(cfg.channels.empty());
        CHECK(cfg.plugins.empty());
    }
}

TEST_CASE("load_config parses JSON file correctly", "[config]") {
    namespace fs = std::filesystem;

    auto tmp = fs::temp_directory_path() / "openclaw_test_config.json";
    {
        std::ofstream out(tmp);
        out << R"({
            "gateway": {
                "port": 9999,
                "max_connections": 50
            },
            "log_level": "debug",
            "memory": {
                "enabled": false,
                "max_results": 5
            }
        })";
    }

    auto cfg = openclaw::load_config(tmp);

    CHECK(cfg.gateway.port == 9999);
    CHECK(cfg.gateway.max_connections == 50);
    CHECK(cfg.log_level == "debug");
    CHECK(cfg.memory.enabled == false);
    CHECK(cfg.memory.max_results == 5);
    // Non-specified fields keep defaults
    CHECK(cfg.sessions.store == "sqlite");

    fs::remove(tmp);
}

TEST_CASE("load_config returns defaults for missing file", "[config]") {
    auto cfg = openclaw::load_config("/nonexistent/path/config.json");

    CHECK(cfg.gateway.port == 18789);
    CHECK(cfg.log_level == "info");
}

TEST_CASE("load_config_from_env reads environment variables", "[config]") {
    // Save and set env vars
    auto save_and_set = [](const char* name, const char* value) {
#ifdef _WIN32
        ::_putenv_s(name, value);
#else
        ::setenv(name, value, 1);
#endif
    };
    auto unset = [](const char* name) {
#ifdef _WIN32
        ::_putenv_s(name, "");
#else
        ::unsetenv(name);
#endif
    };

    save_and_set("OPENCLAW_PORT", "12345");
    save_and_set("OPENCLAW_LOG_LEVEL", "trace");
    save_and_set("OPENCLAW_BIND", "all");

    auto cfg = openclaw::load_config_from_env();

    CHECK(cfg.gateway.port == 12345);
    CHECK(cfg.log_level == "trace");
    CHECK(cfg.gateway.bind == openclaw::BindMode::All);

    unset("OPENCLAW_PORT");
    unset("OPENCLAW_LOG_LEVEL");
    unset("OPENCLAW_BIND");
}

TEST_CASE("Config round-trips through JSON", "[config]") {
    openclaw::Config cfg;
    cfg.gateway.port = 4000;
    cfg.log_level = "warn";
    cfg.memory.enabled = false;

    openclaw::json j = cfg;
    auto restored = j.get<openclaw::Config>();

    CHECK(restored.gateway.port == 4000);
    CHECK(restored.log_level == "warn");
    CHECK(restored.memory.enabled == false);
    // Other fields keep defaults
    CHECK(restored.sessions.ttl_seconds == 86400);
}

// ============================================================================
// v2026.2.17 feature tests: SubagentConfig, ImageConfig, CronConfig stagger
// ============================================================================

TEST_CASE("default_config has no subagents, image, or cron stagger", "[config]") {
    auto cfg = openclaw::default_config();

    SECTION("cron defaults include stagger") {
        CHECK_FALSE(cfg.cron.default_stagger_ms.has_value());
    }

    SECTION("subagent defaults") {
        CHECK_FALSE(cfg.subagents.has_value());
    }

    SECTION("image defaults") {
        CHECK_FALSE(cfg.image.has_value());
    }
}

TEST_CASE("SubagentConfig round-trips through JSON", "[config]") {
    openclaw::SubagentConfig sc;
    sc.max_spawn_depth = 3;
    sc.max_children_per_agent = 10;

    nlohmann::json j = sc;
    auto restored = j.get<openclaw::SubagentConfig>();

    CHECK(restored.max_spawn_depth.value() == 3);
    CHECK(restored.max_children_per_agent.value() == 10);
}

TEST_CASE("ImageConfig round-trips through JSON", "[config]") {
    openclaw::ImageConfig ic;
    ic.max_dimension_px = 1200;
    ic.max_bytes = 5 * 1024 * 1024;

    nlohmann::json j = ic;
    auto restored = j.get<openclaw::ImageConfig>();

    CHECK(restored.max_dimension_px.value() == 1200);
    CHECK(restored.max_bytes.value() == 5 * 1024 * 1024);
}

TEST_CASE("CronConfig with stagger round-trips through JSON", "[config]") {
    openclaw::CronConfig cc;
    cc.enabled = true;
    cc.default_stagger_ms = 5000;

    nlohmann::json j = cc;
    auto restored = j.get<openclaw::CronConfig>();

    CHECK(restored.enabled == true);
    CHECK(restored.default_stagger_ms.value() == 5000);
}

TEST_CASE("Config with subagents and image parses from JSON", "[config]") {
    namespace fs = std::filesystem;

    auto tmp = fs::temp_directory_path() / "openclaw_test_v2026217.json";
    {
        std::ofstream out(tmp);
        out << R"({
            "subagents": { "max_spawn_depth": 2, "max_children_per_agent": 8 },
            "image": { "max_dimension_px": 800, "max_bytes": 2097152 },
            "cron": { "enabled": true, "default_stagger_ms": 3000 }
        })";
    }

    auto cfg = openclaw::load_config(tmp);

    REQUIRE(cfg.subagents.has_value());
    CHECK(cfg.subagents->max_spawn_depth.value() == 2);
    CHECK(cfg.subagents->max_children_per_agent.value() == 8);
    REQUIRE(cfg.image.has_value());
    CHECK(cfg.image->max_dimension_px.value() == 800);
    CHECK(cfg.image->max_bytes.value() == 2097152);
    CHECK(cfg.cron.default_stagger_ms.value() == 3000);

    fs::remove(tmp);
}
