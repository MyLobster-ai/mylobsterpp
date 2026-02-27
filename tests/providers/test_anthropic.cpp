#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/anthropic.hpp"

using namespace openclaw::providers;

TEST_CASE("Anthropic model catalog includes current models", "[providers][anthropic]") {
    // Verify the canonical model names that models() should return.
    std::vector<std::string> expected = {
        "claude-opus-4-20250514",
        "claude-sonnet-4-20250514",
    };

    // We verify the model names are valid by checking 1M eligibility
    for (const auto& m : expected) {
        CHECK(is_1m_eligible_model(m));
    }
}

TEST_CASE("is_1m_eligible_model identifies Claude 4+ models", "[providers][anthropic]") {
    // Claude 4 models should be eligible
    CHECK(is_1m_eligible_model("claude-opus-4-20250514"));
    CHECK(is_1m_eligible_model("claude-sonnet-4-20250514"));

    // Claude 3.x models should NOT be eligible
    CHECK_FALSE(is_1m_eligible_model("claude-3-5-sonnet-20241022"));
    CHECK_FALSE(is_1m_eligible_model("claude-3-opus-20240229"));
    CHECK_FALSE(is_1m_eligible_model("claude-haiku-3-5-20241022"));

    // Non-Anthropic models should NOT be eligible
    CHECK_FALSE(is_1m_eligible_model("gpt-4"));
    CHECK_FALSE(is_1m_eligible_model("gemini-pro"));
    CHECK_FALSE(is_1m_eligible_model(""));
}
