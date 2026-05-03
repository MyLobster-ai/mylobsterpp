# OpenClaw v2026.5.2 parity report — mylobsterpp

This is the bootstrap baseline for the v2026.5.2 parity tree. Each
auto-generated mirror (`tests/v5_2/<rel>/<name>.cpp`) contains one Catch2
TEST_CASE per upstream `it()` / `test()` block, and every body is a
`PARITY_GAP()` FAIL. The full red bar is the parity backlog mylobsterpp
still owes against upstream.

## Headline

| | |
|---|---|
| Upstream test files mirrored | **4938** |
| Hand-authored mirrors | **6** |
| Total mirror files | **4938** |
| Total TEST_CASEs | **43,885** |
| Submodule pin | `agent/openclaw` @ tag **v2026.5.2** (commit `8b2a6e57fef6c582ec6d27b85150616f9e3a7ba4`) |

The 6 hand-authored mirrors live in `extensions/anthropic/` (1M-context
model gating; `is_1m_eligible_model`). They were ported forward from the
v4.29 baseline and re-tagged for v5.2.

The runnable fail count is sourced from the nightly CI workflow
(`.github/workflows/v5_2_parity.yml`); the binary is too large to
register all 43,885 ctest entries individually, so a single ctest entry
runs the whole binary and the report is parsed from the Catch2 summary.

## Reproducing

```bash
cd agent/mylobsterpp
cmake -B build -DMYLOBSTER_BUILD_V5_2_PARITY=ON
cmake --build build --target mylobster_v5_2_parity_tests -j 12
./build/tests/mylobster_v5_2_parity_tests --reporter compact '[v5_2]'

# Filter to a subsystem:
./build/tests/mylobster_v5_2_parity_tests --reporter compact '[v5_2][anthropic]'
```

## v5.2 vs v4.29 delta (the version-bump backlog)

The v4.29 baseline lives at `tests/v4_29/` (frozen). Comparing relative
file paths between the two trees gives the upstream churn:

| | |
|---:|---|
| **+87** | upstream test files added between v4.29 and v5.2 |
| **−32** | upstream test files removed/renamed in v5.2 |
| **+55** | net file-count delta |
| **+1,280** | net TEST_CASE delta (43,885 − 42,605) |

### Newly mirrored in v5.2 (top subsystems)

| Added | Subsystem |
|---:|---|
| 18 | `src/plugins` (registry repair, runtime mirrors, restart drain) |
| 11 | `src/commands` (auth profiles, channel pack registry) |
|  9 | `src/agents` (auth-profiles, runtime-capabilities, web-search signals) |
|  6 | `extensions/discord` (monitor/access-control + retry surface) |
|  5 | `src/config` (channel pack catalog, restart blockers) |
|  4 | `src/gateway` (start repair, restart drain controls) |
|  3 | `test/scripts` (release packaging) |
|  3 | `src/tools`, `src/infra`, `src/cli` (each) |
|  2 | `extensions/voice-call` (realtime fast context, webhook exposure) |
|  2 | `extensions/bonjour` (new extension) |
|  2 | `extensions/slack` (subteam mentions, scopes) |

### Removed/renamed in v5.2

| Removed | Subsystem | Note |
|---:|---|---|
| 11 | `src/memory-host-sdk/*` | Extracted to `packages/memory-host-sdk/src/*` (workspace package). Mirrors land under `packages/memory-host-sdk/...` instead. |
|  9 | `src/plugins/bundled-runtime-*` | Consolidated/renamed; replaced by the new repair + drift detection surface above. |
|  2 | `extensions/whatsapp/*heartbeat*` | Heartbeat surface dropped (matches mylobster `OpenClaw Heartbeat Disabled` decision). |
|  2 | `src/cli/{debug-timing,plugins-deps-command}` | Refactored away. |
|  2 | `src/agents/{models-config.providers.static, openai-codex-models-add-legacy}` | Replaced by static-catalog + runtime-snapshot surfaces. |

The 32 removed files do **not** automatically delete from `tests/v5_2/` —
they don't exist upstream at v5.2 so the generator simply doesn't write
them. The corresponding `tests/v4_29/` mirrors stay around as historical
backlog (frozen baseline).

## Top-N parity hotspots

Same shape as v4.29, with v5.2 file counts. Numbers below are the
**file count** per subsystem (not assertion count — assertion totals are
available via the `[v5_2][<tag>]` runs once CI publishes them).

| Files | Subsystem |
|---:|---|
| 672 | `src/agents` (agent runtime, tools, planning, auth-profiles) |
| 327 | `src/infra` (paths, exec, sandbox, identity, archives) |
| 280 | `src/gateway` (gateway server, RPC, restart drain) |
| 253 | `src/commands` (CLI command handlers, channel pack registry) |
| 230 | `src/plugins` (plugin loader, contracts, repair, runtime mirrors) |
| 180 | `src/auto-reply` (auto-reply engine + reply rules) |
| 153 | `src/cli` (program plumbing, doctor, install) |
| 140 | `extensions/discord` |
| 134 | `src/config` (config schema, migrations, doctor fixes) |
| 129 | `src/channels` (ACP binding registry, protocols) |
| 116 | `extensions/matrix` |
| 104 | `extensions/telegram` |
|  96 | `extensions/browser` |
|  89 | `extensions/slack` |
|  88 | `src/cron` |
|  70 | `extensions/whatsapp` |
|  64 | `extensions/msteams` |
|  61 | `extensions/feishu` |

(Tallied with `find tests/v5_2/<dir> -name '*.cpp' -type f | wc -l`.)

## Reading the failures

Each FAIL line points back to the upstream test file and line. Example:

```
tests/v5_2/extensions/openai/index.cpp:14: FAILED:
v5.2 parity gap [upstream it() block #0]
(extensions/openai/index.test.ts:145):
Auto-generated parity gap; replace with real assertion when mylobsterpp
grows the upstream abstraction.
```

To act on a gap:

1. Read `agent/openclaw/extensions/openai/index.test.ts` at line 145 to see
   the upstream assertion.
2. If mylobsterpp can express the same behavior today, hand-author the
   mirror file (set first line to `// MYLOBSTERPP_HANDWRITTEN_TEST` and
   replace `PARITY_GAP(...)` with real `CHECK`/`REQUIRE` calls).
3. If mylobsterpp is missing the abstraction (most cases), the FAIL is the
   backlog item. Implement the abstraction in `src/...`, then upgrade the
   mirror.

## What's NOT in this report

- **`*.live.test.ts`** — upstream live tests that hit real APIs are
  excluded by the generator. Add them later via a separate `[live]` tag if
  needed.
- **Browser-screenshot directories** — upstream snapshot tooling creates
  directories named `*.test.ts` under `__screenshots__/`; the generator
  skips them.
- **Node-only abstractions** — many upstream files exist solely to test
  Vitest mocks of plugin-SDK runtime helpers. These will likely never have
  C++ analogs and the PARITY_GAP for them serves as documentation.
- **UI** — `ui/src/...` mirrors exist but mylobsterpp ships no UI; treat
  these gaps as informational.

## Trend tracking

To track parity progress over time, capture this report into the repo on a
schedule (CI nightly), diffed against the previous run. Each
`PARITY_GAP → behavior` conversion shows up as one fewer FAIL. The PR
regression guard in `.github/workflows/v5_2_parity.yml` parses the
`Fail (= parity backlog)` line above and fails the PR if the count grows.

Last generated: against `agent/openclaw` at git tag **v2026.5.2**
(submodule pinned at `8b2a6e57fef6c582ec6d27b85150616f9e3a7ba4`).
