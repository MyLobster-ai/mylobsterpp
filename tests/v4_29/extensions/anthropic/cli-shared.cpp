// MYLOBSTERPP_HANDWRITTEN_TEST
// v2026.4.29 parity: extensions/anthropic/cli-shared.test.ts
//
// Upstream tests cover Claude CLI argument-list normalization helpers:
//   normalizeClaudePermissionArgs, normalizeClaudeSettingSourcesArgs,
//   normalizeClaudeBackendConfig, resolveClaudePermissionMode,
//   CLAUDE_CLI_CLEAR_ENV.
//
// mylobsterpp does not embed a Claude CLI backend; it talks directly to the
// Anthropic Messages API via AnthropicProvider. The whole file is a parity
// gap until/unless a CLI backend is added.

#include "../../parity.hpp"

TEST_CASE("v4.29 anthropic cli-shared: normalizeClaudePermissionArgs",
          "[v4_29][parity_gap][providers][anthropic][cli]") {
    PARITY_GAP("extensions/anthropic/cli-shared.test.ts:11",
               "normalizeClaudePermissionArgs",
               "No Claude CLI backend in mylobsterpp; permission-arg normalization "
               "has no analog.");
}

TEST_CASE("v4.29 anthropic cli-shared: normalizeClaudeSettingSourcesArgs",
          "[v4_29][parity_gap][providers][anthropic][cli]") {
    PARITY_GAP("extensions/anthropic/cli-shared.test.ts:47",
               "normalizeClaudeSettingSourcesArgs",
               "No setting-sources flag handling in mylobsterpp.");
}

TEST_CASE("v4.29 anthropic cli-shared: normalizeClaudeBackendConfig",
          "[v4_29][parity_gap][providers][anthropic][cli]") {
    PARITY_GAP("extensions/anthropic/cli-shared.test.ts:78",
               "normalizeClaudeBackendConfig",
               "No backend-config normalization stage.");
}

TEST_CASE("v4.29 anthropic cli-shared: resolveClaudePermissionMode",
          "[v4_29][parity_gap][providers][anthropic][cli]") {
    PARITY_GAP("extensions/anthropic/cli-shared.test.ts",
               "resolveClaudePermissionMode",
               "No permission mode resolution in mylobsterpp.");
}

TEST_CASE("v4.29 anthropic cli-shared: CLAUDE_CLI_CLEAR_ENV constant",
          "[v4_29][parity_gap][providers][anthropic][cli]") {
    PARITY_GAP("extensions/anthropic/cli-shared.test.ts",
               "CLAUDE_CLI_CLEAR_ENV",
               "No CLI env-clearing list; mylobsterpp does not spawn child processes "
               "for Anthropic.");
}

TEST_CASE("v4.29 anthropic cli-shared: buildAnthropicCliBackend",
          "[v4_29][parity_gap][providers][anthropic][cli]") {
    PARITY_GAP("extensions/anthropic/cli-shared.test.ts:2",
               "buildAnthropicCliBackend",
               "No CLI backend factory.");
}
