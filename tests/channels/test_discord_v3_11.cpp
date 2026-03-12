#include <catch2/catch_test_macros.hpp>
#include "openclaw/channels/discord.hpp"

using namespace openclaw::channels;

// ---------------------------------------------------------------------------
// DiscordConfig new v2026.3.7–3.11 fields
// ---------------------------------------------------------------------------

TEST_CASE("DiscordConfig auto_archive_duration defaults to nullopt", "[channels][discord]") {
    DiscordConfig config;
    CHECK_FALSE(config.auto_archive_duration.has_value());
}

TEST_CASE("DiscordConfig auto_archive_duration valid values", "[channels][discord]") {
    DiscordConfig config;

    SECTION("1 hour") {
        config.auto_archive_duration = 60;
        CHECK(*config.auto_archive_duration == 60);
    }
    SECTION("1 day") {
        config.auto_archive_duration = 1440;
        CHECK(*config.auto_archive_duration == 1440);
    }
    SECTION("3 days") {
        config.auto_archive_duration = 4320;
        CHECK(*config.auto_archive_duration == 4320);
    }
    SECTION("1 week") {
        config.auto_archive_duration = 10080;
        CHECK(*config.auto_archive_duration == 10080);
    }
}

TEST_CASE("DiscordConfig allow_bots defaults to nullopt (none)", "[channels][discord]") {
    DiscordConfig config;
    CHECK_FALSE(config.allow_bots.has_value());
}

TEST_CASE("DiscordConfig allow_bots modes", "[channels][discord]") {
    DiscordConfig config;

    SECTION("mentions mode") {
        config.allow_bots = "mentions";
        CHECK(*config.allow_bots == "mentions");
    }
    SECTION("all mode") {
        config.allow_bots = "all";
        CHECK(*config.allow_bots == "all");
    }
    SECTION("none mode") {
        config.allow_bots = "none";
        CHECK(*config.allow_bots == "none");
    }
}

TEST_CASE("DiscordConfig reaction_allowlist defaults to nullopt", "[channels][discord]") {
    DiscordConfig config;
    CHECK_FALSE(config.reaction_allowlist.has_value());
}

TEST_CASE("DiscordConfig reaction_allowlist with entries", "[channels][discord]") {
    DiscordConfig config;
    config.reaction_allowlist = {{"123456789", "987654321", "role:admin"}};
    REQUIRE(config.reaction_allowlist.has_value());
    CHECK(config.reaction_allowlist->size() == 3);
    CHECK((*config.reaction_allowlist)[2] == "role:admin");
}

TEST_CASE("DiscordConfig thread binding fields from v2026.3.2 coexist", "[channels][discord]") {
    DiscordConfig config;
    config.idle_hours = 24;
    config.max_age_hours = 168;
    config.auto_archive_duration = 4320;

    CHECK(*config.idle_hours == 24);
    CHECK(*config.max_age_hours == 168);
    CHECK(*config.auto_archive_duration == 4320);
}

TEST_CASE("DiscordConfig preserves existing v2026.2.24 DAVE fields", "[channels][discord]") {
    DiscordConfig config;
    config.dave_encryption = true;
    config.decryption_failure_tolerance = 50;
    config.auto_archive_duration = 1440;
    config.allow_bots = "mentions";

    CHECK(config.dave_encryption);
    CHECK(config.decryption_failure_tolerance == 50);
    CHECK(*config.auto_archive_duration == 1440);
    CHECK(*config.allow_bots == "mentions");
}
