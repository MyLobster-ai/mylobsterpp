# OpenClaw v2026.7.1 parity report — mylobsterpp

This is the bootstrap baseline for the v2026.7.1 parity tree. Each
auto-generated mirror (`tests/v7_1/<rel>/<name>.cpp`) contains one Catch2
TEST_CASE per upstream `it()` / `test()` block, and every body is a
`PARITY_GAP()` FAIL. The full red bar is the parity backlog mylobsterpp
still owes against upstream.

## Headline

| | |
|---|---|
| Upstream test files mirrored | **6784** |
| Hand-authored mirrors | **6** |
| Total mirror files | **6784** |
| Total TEST_CASEs | **80,728** |
| Submodule pin | `agent/openclaw` @ tag **v2026.7.1** (commit `2d2ddc43d0dcf71f31283d780f9fe9ff4cc04fe4`) |

The 6 hand-authored mirrors live in `extensions/anthropic/` (1M-context
model gating; `is_1m_eligible_model`). They were ported forward from the
v5.2 tree and re-tagged for v7.1; all six upstream files still exist at
v2026.7.1, so the assertions carry over unchanged.

The runnable fail count is sourced from the nightly CI workflow
(`.github/workflows/v7_1_parity.yml`); the binary is too large to
register all 80,728 ctest entries individually, so a single ctest entry
runs the whole binary and the report is parsed from the Catch2 summary.

## Reproducing

```bash
cd agent/mylobsterpp
cmake -B build -DMYLOBSTER_BUILD_V7_1_PARITY=ON
cmake --build build --target mylobster_v7_1_parity_tests -j 12
./build/tests/mylobster_v7_1_parity_tests --reporter compact '[v7_1]'

# Filter to a subsystem:
./build/tests/mylobster_v7_1_parity_tests --reporter compact '[v7_1][anthropic]'
```

## v7.1 vs v5.2 delta (the version-bump backlog)

The v5.2 tree lives at `tests/v5_2/` (now frozen, alongside the v4.29
baseline). Comparing relative file paths between the two trees gives the
upstream churn across ~25,181 commits (v2026.5.3 → v2026.7.1):

| | |
|---:|---|
| **+2400** | upstream test files added between v5.2 and v7.1 |
| **−554** | upstream test files removed/renamed in v7.1 |
| **+1846** | net file-count delta (6784 − 4938) |
| **+36,843** | net TEST_CASE delta (80,728 − 43,885) |

This bump is an order of magnitude larger than v4.29 → v5.2 (+87/−32
files, +1,280 cases). The 4384 mirrors common to both trees are *path*
matches only — an unchanged filename says nothing about unchanged
assertions, so common files still carry their own re-derived backlog.

### Newly mirrored in v7.1 (top subsystems)

| Added | Subsystem |
|---:|---|
| 428 | `src/agents` (compaction ownership, tool-search, code mode, subagents, failover) |
| 279 | `test` (harness/contract suites) |
| 142 | `ui` (session groups, worktrees + background-task pages) |
|  98 | `src/gateway` (task ledger, session-organization RPCs, safe mode, delivery recovery) |
|  72 | `src/plugins` (managed install lifecycle, operator install policy, state → SQLite) |
|  71 | `src/infra` (state migrations, timeout clamps, proxy stack) |
|  69 | `src/commands` |
|  54 | `src/skills` (Skill Workshop pipeline, ClawHub installs) |
|  54 | `extensions/codex` (app-server harness) |
|  46 | `src/cli`, `src/auto-reply` |
|  44 | `extensions/qa-lab` |
|  40 | `extensions/oc-path` |
|  38 | `src/cron`, `src/config`, `extensions/browser` |
|  36 | `extensions/imessage` (the `imsg` backend that replaced BlueBubbles) |
|  35 | `extensions/qqbot` |

### Extensions that are new at v7.1

`admin-http-rpc`, `canvas`, `clawrouter`, `clickclack`, `codex-supervisor`,
`cohere`, `copilot`, `featherless`, `gmi`, `llama-cpp`, `logbook`, `longcat`,
`meta`, `novita`, `oc-path`, `parallel`, `pixverse`, `policy`, `raft`, `sms`,
`stepfun`, `tencent`, `voyage`, `workboard`

These correspond to the externalization wave (Copilot, Cohere and the
browser/Slack/WhatsApp/Matrix/Discord moves to on-demand plugins) plus the
genuinely new provider and feature surfaces tracked in
`agent/mylobster/PARITY_v2026.7.1.md`.

### Extensions removed at v7.1

`bluebubbles` (−26 files), `skill-workshop`, `speech-core`

`bluebubbles` is the largest single removal and is not a regression: the
channel was retired upstream in favour of `channels.imessage` with the
`imsg` backend, which is why `extensions/imessage` grew by 36 files in the
same window. `skill-workshop` and `speech-core` were folded into
`src/skills` and the Talk session controller respectively.

## Scope note — what "100% parity" means here

mylobsterpp is a **conceptual** C++ port of OpenClaw, not a file-for-file
translation: single C++ classes collapse many upstream TS modules, and the
port has subsystems upstream does not. A green bar on this tree is
therefore a multi-quarter program, not a task — 80,728 mirrored assertions
across ~25,181 upstream commits of churn, where whole subsystems (Skill
Workshop, code mode, managed worktrees, the Codex app-server harness,
Workboard, transcripts) have no C++ analog at all yet.

What this tree delivers today is the *measurable* form of that backlog:
every upstream behaviour we owe, named, tagged by subsystem, and pinned to
an upstream `file:line`. Converting a gap into real coverage is a local,
reviewable edit — replace the `PARITY_GAP()` body with assertions and add
the `// MYLOBSTERPP_HANDWRITTEN_TEST` sentinel so the generator stops
regenerating it. The nightly workflow enforces the ratchet: the FAIL count
may fall, never rise.
