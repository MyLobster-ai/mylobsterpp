#include <catch2/catch_test_macros.hpp>

#include "openclaw/providers/bedrock.hpp"

using namespace openclaw::providers;

TEST_CASE("BedrockProvider Guardrails config (v2026.4.1)", "[providers][bedrock]") {
    SECTION("GuardrailsConfig defaults") {
        BedrockProvider::GuardrailsConfig config;
        CHECK(config.guardrail_version == "DRAFT");
        CHECK(config.trace_enabled == false);
        CHECK(config.guardrail_identifier.empty());
    }

    SECTION("GuardrailsConfig custom values") {
        BedrockProvider::GuardrailsConfig config;
        config.guardrail_identifier = "my-guardrail-id";
        config.guardrail_version = "3";
        config.trace_enabled = true;
        CHECK(config.guardrail_identifier == "my-guardrail-id");
        CHECK(config.guardrail_version == "3");
        CHECK(config.trace_enabled);
    }
}
