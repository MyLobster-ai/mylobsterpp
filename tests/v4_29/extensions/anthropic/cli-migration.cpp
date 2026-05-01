// MYLOBSTERPP_HANDWRITTEN_TEST
// v2026.4.29 parity: extensions/anthropic/cli-migration.test.ts
//
// Upstream tests cover the Claude CLI -> mylobsterpp auth profile migration:
//   buildAnthropicCliMigrationResult, hasClaudeCliAuth, the cli auth method,
//   and non-interactive credential ingestion.
//
// mylobsterpp has no auth-profile concept and no Claude CLI integration. The
// whole file is a parity gap.

#include "../../parity.hpp"

TEST_CASE("v4.29 anthropic cli-migration: hasClaudeCliAuth detects existing profile",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "hasClaudeCliAuth",
               "No auth profile registry to inspect.");
}

TEST_CASE("v4.29 anthropic cli-migration: buildAnthropicCliMigrationResult shapes profile",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts:22",
               "buildAnthropicCliMigrationResult",
               "No CLI -> auth-profile migration helper.");
}

TEST_CASE("v4.29 anthropic cli-migration: cli auth.run interactive path",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "Plugin::auth[cli].run (interactive)",
               "No interactive prompter / wizard infrastructure in mylobsterpp.");
}

TEST_CASE("v4.29 anthropic cli-migration: cli auth.run non-interactive path",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "Plugin::auth[cli].runNonInteractive",
               "No non-interactive auth method context.");
}

TEST_CASE("v4.29 anthropic cli-migration: surfaces wizard errors when CLI absent",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "wizard error surface",
               "No wizard error model.");
}

TEST_CASE("v4.29 anthropic cli-migration: skips migration when profile already exists",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "skip-on-existing",
               "No idempotent profile creation logic.");
}

TEST_CASE("v4.29 anthropic cli-migration: does not migrate without claude-cli credentials",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "no-creds short-circuit",
               "No credential probe seam.");
}

TEST_CASE("v4.29 anthropic cli-migration: handles oauth credential type",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "oauth credential",
               "No oauth profile shape in mylobsterpp.");
}

TEST_CASE("v4.29 anthropic cli-migration: handles token credential type",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "token credential",
               "No token profile shape in mylobsterpp.");
}

TEST_CASE("v4.29 anthropic cli-migration: returns empty profiles when read returns null",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "null-credential handling",
               "No empty-profile-list contract.");
}

TEST_CASE("v4.29 anthropic cli-migration: respects allowSecretRefPrompt",
          "[v4_29][parity_gap][providers][anthropic][cli][migration]") {
    PARITY_GAP("extensions/anthropic/cli-migration.test.ts",
               "allowSecretRefPrompt flag",
               "No secret-ref prompt control in mylobsterpp.");
}
