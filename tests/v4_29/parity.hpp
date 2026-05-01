#pragma once

// v2026.4.29 OpenClaw test parity helpers.
//
// Each upstream Vitest file (`extensions/<id>/<name>.test.ts`) is mirrored by a
// Catch2 file under `tests/providers/v4_29/<id>/<name>.cpp`. Behavioral
// assertions that mylobsterpp's C++ surface can express are ported as live
// Catch2 SECTION/REQUIRE checks. Upstream assertions that depend on plugin-SDK
// abstractions mylobsterpp does not have surface as PARITY_GAP() FAILs so the
// red bar is the v4.29 parity backlog.
//
// PARITY_GAP arguments:
//   upstream_ref  — "extensions/<id>/<name>.test.ts:<line>"
//   missing       — short C++-shaped name of the missing abstraction
//   note          — free-form context (why it's missing / what it should do)
//
// Example:
//   PARITY_GAP("extensions/anthropic/index.test.ts:41",
//              "Plugin::cliBackends",
//              "mylobsterpp has no plugin-SDK CLI backend registry");
//
// When the abstraction lands, replace the PARITY_GAP with real assertions and
// the test flips green automatically.

#include <catch2/catch_test_macros.hpp>

#define PARITY_GAP(upstream_ref, missing, note)                                              \
    FAIL("v4.29 parity gap [" missing "] (" upstream_ref "): " note)

// Tag aliases used for consistency:
//   [v4_29]        — every ported test
//   [parity_gap]   — currently FAILs because abstraction is missing
//   [providers]    — provider subsystem
//   [<provider id>] — anthropic / openai / google / bedrock / ...
//   [behavior]     — behavioral assertion ported from upstream
