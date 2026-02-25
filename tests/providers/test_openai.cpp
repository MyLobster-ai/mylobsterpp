#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/openai.hpp"

using namespace openclaw::providers;

TEST_CASE("normalize_vercel_model_ref maps Vercel AI Gateway refs", "[providers][openai]") {
    SECTION("vercel-ai-gateway claude models") {
        CHECK(OpenAIProvider::normalize_vercel_model_ref("vercel-ai-gateway/claude-opus-4.6")
              == "claude-opus-4-6-20250514");
        CHECK(OpenAIProvider::normalize_vercel_model_ref("vercel-ai-gateway/claude-sonnet-4.6")
              == "claude-sonnet-4-6-20250514");
        CHECK(OpenAIProvider::normalize_vercel_model_ref("vercel-ai-gateway/claude-haiku-4.5")
              == "claude-haiku-4-5-20251001");
    }

    SECTION("passthrough for non-vercel models") {
        CHECK(OpenAIProvider::normalize_vercel_model_ref("gpt-4o") == "gpt-4o");
        CHECK(OpenAIProvider::normalize_vercel_model_ref("claude-opus-4.6") == "claude-opus-4.6");
        CHECK(OpenAIProvider::normalize_vercel_model_ref("") == "");
    }

    SECTION("passthrough for unknown vercel models") {
        CHECK(OpenAIProvider::normalize_vercel_model_ref("vercel-ai-gateway/gpt-4o")
              == "vercel-ai-gateway/gpt-4o");
    }
}
