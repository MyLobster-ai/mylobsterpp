#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/kilocode.hpp"

using namespace openclaw::providers;

TEST_CASE("KilocodeProvider constants", "[providers][kilocode]") {
    CHECK(KilocodeProvider::kDefaultBaseUrl == "https://api.kilocode.ai/v1");
    CHECK(KilocodeProvider::kDefaultModel == "kilocode/anthropic/claude-opus-4.6");
}

TEST_CASE("is_kilocode_model detects Kilo Gateway model prefixes", "[providers][kilocode]") {
    SECTION("kilocode/ prefix") {
        CHECK(KilocodeProvider::is_kilocode_model("kilocode/anthropic/claude-opus-4.6"));
        CHECK(KilocodeProvider::is_kilocode_model("kilocode/openai/gpt-4o"));
        CHECK(KilocodeProvider::is_kilocode_model("kilocode/anything"));
    }

    SECTION("non-kilocode models") {
        CHECK_FALSE(KilocodeProvider::is_kilocode_model("claude-opus-4.6"));
        CHECK_FALSE(KilocodeProvider::is_kilocode_model("gpt-4o"));
        CHECK_FALSE(KilocodeProvider::is_kilocode_model("anthropic/claude-3-opus"));
        CHECK_FALSE(KilocodeProvider::is_kilocode_model(""));
    }
}
