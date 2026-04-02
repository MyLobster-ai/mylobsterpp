#include <catch2/catch_test_macros.hpp>

#include "openclaw/channels/whatsapp.hpp"

using namespace openclaw::channels;

TEST_CASE("WhatsAppConfig v2026.3.31 reactions", "[channels][whatsapp]") {
    WhatsAppConfig config;

    SECTION("reaction_level defaults to 'none'") {
        CHECK(config.reaction_level == "none");
    }

    SECTION("reaction_level accepts valid values") {
        config.reaction_level = "auto";
        CHECK(config.reaction_level == "auto");

        config.reaction_level = "guided";
        CHECK(config.reaction_level == "guided");
    }
}
