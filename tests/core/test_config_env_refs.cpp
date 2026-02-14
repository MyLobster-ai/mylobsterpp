#include <catch2/catch_test_macros.hpp>

#include "openclaw/core/config.hpp"

#include <cstdlib>

using namespace openclaw;

TEST_CASE("Config ${VAR} resolution", "[core][config]") {
    SECTION("Resolves existing env var") {
#ifdef _WIN32
        _putenv_s("TEST_OPENCLAW_VAR", "hello_world");
#else
        setenv("TEST_OPENCLAW_VAR", "hello_world", 1);
#endif
        auto result = resolve_env_refs("prefix_${TEST_OPENCLAW_VAR}_suffix");
        CHECK(result == "prefix_hello_world_suffix");
    }

    SECTION("Preserves unresolved vars") {
        // Assuming NONEXISTENT_VAR_12345 is not set
        auto result = resolve_env_refs("value=${NONEXISTENT_VAR_12345}");
        CHECK(result == "value=${NONEXISTENT_VAR_12345}");
    }

    SECTION("Handles multiple refs") {
#ifdef _WIN32
        _putenv_s("TEST_A", "aaa");
        _putenv_s("TEST_B", "bbb");
#else
        setenv("TEST_A", "aaa", 1);
        setenv("TEST_B", "bbb", 1);
#endif
        auto result = resolve_env_refs("${TEST_A}:${TEST_B}");
        CHECK(result == "aaa:bbb");
    }

    SECTION("No refs returns input unchanged") {
        auto result = resolve_env_refs("no refs here");
        CHECK(result == "no refs here");
    }

    SECTION("Empty input") {
        auto result = resolve_env_refs("");
        CHECK(result.empty());
    }
}

TEST_CASE("Config $${VAR} escaping", "[core][config]") {
    SECTION("Double dollar escapes to literal") {
        auto result = resolve_env_refs("value=$${LITERAL}");
        CHECK(result == "value=${LITERAL}");
    }

    SECTION("Mixed escaping and resolution") {
#ifdef _WIN32
        _putenv_s("TEST_REAL", "resolved");
#else
        setenv("TEST_REAL", "resolved", 1);
#endif
        auto result = resolve_env_refs("$${ESCAPED} and ${TEST_REAL}");
        CHECK(result == "${ESCAPED} and resolved");
    }
}
