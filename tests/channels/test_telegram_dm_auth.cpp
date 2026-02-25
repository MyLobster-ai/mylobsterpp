#include <catch2/catch_test_macros.hpp>

#include "openclaw/channels/telegram.hpp"

using namespace openclaw::channels;

// TelegramConfig DM policy tests

TEST_CASE("TelegramConfig default dm_policy is open", "[channels][telegram][dm_auth]") {
    TelegramConfig config;
    config.bot_token = "test-token";
    CHECK(config.dm_policy == "open");
    CHECK(config.allowed_sender_ids.empty());
}

TEST_CASE("TelegramConfig allowlist mode", "[channels][telegram][dm_auth]") {
    TelegramConfig config;
    config.bot_token = "test-token";
    config.dm_policy = "allowlist";
    config.allowed_sender_ids = {"12345", "67890"};

    CHECK(config.dm_policy == "allowlist");
    CHECK(config.allowed_sender_ids.size() == 2);
}
