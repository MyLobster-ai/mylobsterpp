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
