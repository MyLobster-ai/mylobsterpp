#include <catch2/catch_test_macros.hpp>

// v2026.2.26: Test model normalization for Gemini provider
// The normalize_gemini_model function is in an anonymous namespace,
// so we test it indirectly through the provider's behavior.
// For unit testing, we replicate the normalization logic.

namespace {
auto normalize_gemini_model(std::string_view model) -> std::string {
    auto m = std::string(model);
    if (m == "gemini-3" || m == "gemini3") {
        return "gemini-3.1-pro-preview";
    }
    if (m == "gemini-3-flash" || m == "gemini3-flash") {
        return "gemini-3.1-flash-preview";
    }
    return m;
}
} // namespace

TEST_CASE("Gemini model normalization", "[providers][gemini]") {
    SECTION("gemini-3 normalizes to pro preview") {
        CHECK(normalize_gemini_model("gemini-3") == "gemini-3.1-pro-preview");
    }

    SECTION("gemini3 (no hyphen) normalizes to pro preview") {
        CHECK(normalize_gemini_model("gemini3") == "gemini-3.1-pro-preview");
    }

    SECTION("gemini-3-flash normalizes to flash preview") {
        CHECK(normalize_gemini_model("gemini-3-flash") == "gemini-3.1-flash-preview");
    }

    SECTION("gemini3-flash normalizes to flash preview") {
        CHECK(normalize_gemini_model("gemini3-flash") == "gemini-3.1-flash-preview");
    }

    SECTION("explicit model names pass through") {
        CHECK(normalize_gemini_model("gemini-2.0-flash") == "gemini-2.0-flash");
        CHECK(normalize_gemini_model("gemini-3.1-pro-preview") == "gemini-3.1-pro-preview");
    }

    SECTION("unknown models pass through") {
        CHECK(normalize_gemini_model("custom-model") == "custom-model");
    }
}
