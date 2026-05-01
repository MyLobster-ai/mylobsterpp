// MYLOBSTERPP_HANDWRITTEN_TEST
// v2026.4.29 parity: extensions/anthropic/stream-wrappers.test.ts
//
// Upstream tests cover composable stream wrapper helpers:
//   createAnthropicBetaHeadersWrapper, wrapAnthropicProviderStream,
//   createAnthropicThinkingPrefillWrapper, createAnthropicFastModeWrapper,
//   createAnthropicServiceTierWrapper.
//
// mylobsterpp's AnthropicProvider applies the equivalent header / payload
// concerns *inline* inside complete()/stream() (e.g. the context-1m beta
// header is added when is_1m_eligible_model returns true). There is no
// composable wrapper chain. The single behavioral analog we have is the 1M
// header gating, which we cover indirectly through is_1m_eligible_model.

#include "../../parity.hpp"

#include "openclaw/providers/anthropic.hpp"

using namespace openclaw::providers;

TEST_CASE("v4.29 anthropic stream-wrappers: strips context-1m for OAuth/legacy token",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:88",
               "createAnthropicBetaHeadersWrapper (oauth strip)",
               "AnthropicProvider always sends context-1m beta when model is "
               "1M-eligible; it does not detect OAuth/legacy token apiKey shape "
               "(sk-ant-oat01-*) to strip the beta.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: keeps context-1m for API key auth",
          "[v4_29][providers][anthropic][stream_wrapper][behavior]") {
    // Behavioral parity (positive case only): when the model is 1M-eligible,
    // the context-1m beta should ride. mylobsterpp gates on the model name;
    // upstream additionally gates on the apiKey shape. This subset still holds.
    CHECK(is_1m_eligible_model("claude-sonnet-4-6"));
    CHECK(is_1m_eligible_model("claude-opus-4-20250514"));
    // Non-eligible models should not trip the beta either way.
    CHECK_FALSE(is_1m_eligible_model("claude-3-5-sonnet-20241022"));
}

TEST_CASE("v4.29 anthropic stream-wrappers: skips service_tier for OAuth in composed chain",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:105",
               "wrapAnthropicProviderStream (oauth service_tier skip)",
               "No service_tier injection in C++ request body builder.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: composes provider stream from extra params",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:112",
               "wrapAnthropicProviderStream (compose chain)",
               "No wrap-chain composition; stream() is monolithic in mylobsterpp.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: removes trailing assistant prefill when thinking enabled",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][thinking]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:129",
               "createAnthropicThinkingPrefillWrapper",
               "No assistant-prefill cleanup when ThinkingMode::Adaptive is set; "
               "build_request_body forwards messages verbatim.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: keeps assistant prefill when thinking disabled",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][thinking]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:143",
               "createAnthropicThinkingPrefillWrapper (disabled)",
               "No prefill cleanup logic to disable.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: keeps trailing assistant tool_use turns",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][thinking]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:155",
               "createAnthropicThinkingPrefillWrapper (tool_use preserve)",
               "No tool_use-aware prefill handling.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: fast-mode skips service_tier for OAuth",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][fast_mode]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:181",
               "createAnthropicFastModeWrapper (oauth)",
               "No fast-mode wrapper; service_tier=auto/standard_only knob absent.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: fast-mode injects service_tier=auto for API key",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][fast_mode]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:186",
               "createAnthropicFastModeWrapper (auto)",
               "No service_tier injection.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: fast-mode disabled injects service_tier=standard_only",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][fast_mode]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:191",
               "createAnthropicFastModeWrapper (standard_only)",
               "No service_tier injection.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: fast-mode skips service_tier for non-anthropic provider",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][fast_mode]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:196",
               "createAnthropicFastModeWrapper (non-anthropic)",
               "No provider-id gating because no wrapper exists.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: service_tier wrapper skips OAuth",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][service_tier]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:218",
               "createAnthropicServiceTierWrapper (oauth)",
               "No service_tier wrapper.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: service_tier wrapper injects auto for API key",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][service_tier]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:223",
               "createAnthropicServiceTierWrapper (auto)",
               "No service_tier wrapper.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: service_tier wrapper injects standard_only for API key",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][service_tier]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:228",
               "createAnthropicServiceTierWrapper (standard_only)",
               "No service_tier wrapper.");
}

TEST_CASE("v4.29 anthropic stream-wrappers: service_tier wrapper skips non-anthropic provider",
          "[v4_29][parity_gap][providers][anthropic][stream_wrapper][service_tier]") {
    PARITY_GAP("extensions/anthropic/stream-wrappers.test.ts:236",
               "createAnthropicServiceTierWrapper (non-anthropic)",
               "No service_tier wrapper.");
}
