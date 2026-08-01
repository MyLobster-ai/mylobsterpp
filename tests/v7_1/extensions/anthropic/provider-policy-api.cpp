// MYLOBSTERPP_HANDWRITTEN_TEST
// v2026.7.1 parity: extensions/anthropic/provider-policy-api.test.ts
//
// Upstream tests cover the public artifact (provider-policy-api.ts) that core
// uses on hot paths to avoid loading the full provider plugin:
//   normalizeConfig (anthropic / claude-cli / passthrough non-anthropic)
//   applyConfigDefaults (cache-ttl context pruning)
//
// mylobsterpp has neither. The whole file is a parity gap.

#include "../../parity.hpp"

TEST_CASE("v7.1 anthropic provider-policy-api: normalizeConfig (anthropic)",
          "[v7_1][parity_gap][providers][anthropic][policy_api]") {
    PARITY_GAP("extensions/anthropic/provider-policy-api.test.ts:23",
               "anthropicProviderPolicy::normalizeConfig",
               "No public policy artifact; provider config is consumed directly.");
}

TEST_CASE("v7.1 anthropic provider-policy-api: normalizeConfig (claude-cli)",
          "[v7_1][parity_gap][providers][anthropic][policy_api]") {
    PARITY_GAP("extensions/anthropic/provider-policy-api.test.ts:38",
               "anthropicProviderPolicy::normalizeConfig (claude-cli)",
               "No claude-cli runtime path.");
}

TEST_CASE("v7.1 anthropic provider-policy-api: normalizeConfig leaves non-Anthropic untouched",
          "[v7_1][parity_gap][providers][anthropic][policy_api]") {
    PARITY_GAP("extensions/anthropic/provider-policy-api.test.ts:52",
               "anthropicProviderPolicy::normalizeConfig (passthrough)",
               "No normalize hook to be passthrough.");
}

TEST_CASE("v7.1 anthropic provider-policy-api: applyConfigDefaults sets cache-ttl pruning",
          "[v7_1][parity_gap][providers][anthropic][policy_api]") {
    PARITY_GAP("extensions/anthropic/provider-policy-api.test.ts:66",
               "anthropicProviderPolicy::applyConfigDefaults",
               "No contextPruning concept in core/config.hpp; cache-ttl mode/1h ttl "
               "have no analog.");
}
