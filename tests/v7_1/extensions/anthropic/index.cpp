// MYLOBSTERPP_HANDWRITTEN_TEST
// v2026.7.1 parity: extensions/anthropic/index.test.ts (anthropic plugin entry)
//
// Upstream test exercises the Anthropic plugin's plugin-SDK hooks:
//   register, resolveReasoningOutputMode, buildReplayPolicy, normalizeConfig,
//   applyConfigDefaults, resolveDynamicModel, resolveThinkingProfile,
//   normalizeResolvedModel, resolveSyntheticAuth, auth[].run().
//
// mylobsterpp models providers as a flat C++ Provider class with no plugin
// SDK / no normalizeConfig / no applyConfigDefaults / no auth profiles. Most
// upstream cases collapse into PARITY_GAP. The 1M-context normalization case
// has a partial behavioral analog via is_1m_eligible_model().

#include "../../parity.hpp"

#include "openclaw/providers/anthropic.hpp"

using namespace openclaw::providers;

TEST_CASE("v7.1 anthropic plugin: registers the claude-cli backend",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:41",
               "Plugin::cliBackends",
               "mylobsterpp has no plugin-SDK CLI backend registry; "
               "claude-cli runtime is unmodeled.");
}

TEST_CASE("v7.1 anthropic plugin: owns native reasoning output mode",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:57",
               "Plugin::resolveReasoningOutputMode",
               "No reasoning-output-mode hook exists; "
               "thinking is handled inline via agent::apply_thinking_anthropic.");
}

TEST_CASE("v7.1 anthropic plugin: owns replay policy for Claude transports",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:69",
               "Plugin::buildReplayPolicy",
               "No replay policy abstraction; tool_use id sanitization, "
               "preserveSignatures, validateAnthropicTurns are not modeled.");
}

TEST_CASE("v7.1 anthropic plugin: normalizeConfig defaults provider api",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:90",
               "Plugin::normalizeConfig",
               "No config-normalization hook; config is read directly from "
               "ProviderConfig, no defaulting of api='anthropic-messages'.");
}

TEST_CASE("v7.1 anthropic plugin: normalizeConfig defaults claude-cli provider api",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:105",
               "Plugin::normalizeConfig (claude-cli)",
               "claude-cli runtime not modeled in mylobsterpp.");
}

TEST_CASE("v7.1 anthropic plugin: normalizeConfig leaves non-Anthropic provider untouched",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:120",
               "Plugin::normalizeConfig (passthrough)",
               "No normalize hook means no behavior to passthrough either.");
}

TEST_CASE("v7.1 anthropic plugin: applyConfigDefaults sets pruning + heartbeat",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:135",
               "Plugin::applyConfigDefaults",
               "No contextPruning or heartbeat defaulting in mylobsterpp; "
               "core/config has no equivalents for cache-ttl mode.");
}

TEST_CASE("v7.1 anthropic plugin: backfills Claude CLI allowlist defaults",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:167",
               "Plugin::applyConfigDefaults (claude-cli backfill)",
               "No model-allowlist backfill on legacy configs.");
}

TEST_CASE("v7.1 anthropic plugin: resolveDynamicModel for claude-opus-4-7 from 4.6 family",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:204",
               "Plugin::resolveDynamicModel",
               "No dynamic model resolution; mylobsterpp uses static models() list. "
               "claude-opus-4-7 is implicitly accepted by is_1m_eligible_model "
               "but contextWindow=1048576 is not surfaced anywhere.");
}

TEST_CASE("v7.1 anthropic plugin: 1M context eligibility for claude-opus-4.7 variants",
          "[v7_1][providers][anthropic][behavior]") {
    // Partial behavioral parity: upstream normalizes both "claude-opus-4-7" and
    // "claude-opus-4.7-20260219" to a 1M context window. mylobsterpp doesn't
    // model context windows numerically, but is_1m_eligible_model gates the
    // anthropic-beta context-1m-2025-08-07 header. Both variants must be
    // recognized by that gate.
    CHECK(is_1m_eligible_model("claude-opus-4-7"));
    CHECK(is_1m_eligible_model("claude-opus-4.7-20260219"));

    // 4.6 must remain eligible (was 4.6 in upstream test that expects 1M).
    CHECK(is_1m_eligible_model("claude-opus-4-6"));
    CHECK(is_1m_eligible_model("claude-sonnet-4-6"));
}

TEST_CASE("v7.1 anthropic plugin: resolveThinkingProfile for 4.6 vs 4.7",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:232",
               "Plugin::resolveThinkingProfile",
               "Thinking profiles (xhigh/adaptive/max levels, defaultLevel) are not "
               "modeled per-model in mylobsterpp; only ThinkingMode::None|Adaptive exists.");
}

TEST_CASE("v7.1 anthropic plugin: normalizeResolvedModel forces 1M for 4.7",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:260",
               "Plugin::normalizeResolvedModel",
               "No resolved-model normalization hook; mylobsterpp does not carry "
               "contextWindow/contextTokens metadata on its provider model list.");
}

TEST_CASE("v7.1 anthropic plugin: resolveSyntheticAuth for claude-cli oauth",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:291",
               "Plugin::resolveSyntheticAuth",
               "No synthetic auth resolution; mylobsterpp reads ANTHROPIC_API_KEY "
               "from env at construction only.");
}

TEST_CASE("v7.1 anthropic plugin: resolveSyntheticAuth for claude-cli token",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:316",
               "Plugin::resolveSyntheticAuth (token mode)",
               "No claude-cli token credential ingestion.");
}

TEST_CASE("v7.1 anthropic plugin: stores claude-cli auth profile via cli auth.run",
          "[v7_1][parity_gap][providers][anthropic]") {
    PARITY_GAP("extensions/anthropic/index.test.ts:339",
               "Plugin::auth[].run (cli migration)",
               "No auth profile storage; mylobsterpp has no auth profiles concept.");
}
