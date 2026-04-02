#include <catch2/catch_test_macros.hpp>

#include "openclaw/channels/telegram.hpp"

using namespace openclaw::channels;

TEST_CASE("TelegramConfig v2026.4.1 error policy", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test-token";

    SECTION("error_policy defaults to 'default'") {
        CHECK(config.error_policy == "default");
    }

    SECTION("error_cooldown_ms defaults to 60000") {
        CHECK(config.error_cooldown_ms == 60000);
    }

    SECTION("error_policy accepts valid values") {
        config.error_policy = "suppress";
        CHECK(config.error_policy == "suppress");

        config.error_policy = "log-only";
        CHECK(config.error_policy == "log-only");
    }

    SECTION("error_cooldown_ms can be customized") {
        config.error_cooldown_ms = 30000;
        CHECK(config.error_cooldown_ms == 30000);
    }
}
