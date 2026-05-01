# mylobsterpp v2026.4.29 OpenClaw parity tests

This tree is the **runnable parity backlog** for OpenClaw v2026.4.29. Each
upstream `*.test.ts` file under `agent/openclaw/` is mirrored 1:1 here as a
Catch2 file with one `TEST_CASE` per upstream `it()` / `test()` block.

## Layout

```
tests/v4_29/
    parity.hpp                    PARITY_GAP() macro + tagging conventions
    generate.py                   Generator (idempotent; protects hand-authored)
    extensions/<id>/<name>.cpp    Mirror of openclaw/extensions/<id>/<name>.test.ts
    src/<area>/<name>.cpp         Mirror of openclaw/src/<area>/<name>.test.ts
    ui/...                        Mirror of openclaw/ui/...
    test/...                      Mirror of openclaw/test/...
```

## Why this exists

mylobsterpp is a **conceptual** C++ port of OpenClaw, not a file-for-file
translation. v4.29 (target) is roughly 14k upstream commits ahead of the v4.1
snapshot mylobsterpp was built against. Many upstream behaviors have no C++
analog yet (plugin SDK, ACP binding registry, replay policy, auth profiles,
etc.).

Running this suite gives a precise red bar that names every upstream
behavior we still owe, with file:line references back to upstream tests.
Each FAIL is a parity backlog item.

## Building

The mirrors are gated behind a CMake option because there are ~4.9k of them:

```bash
# Default build: existing 93 tests only, fast.
cmake -B build
cmake --build build --target mylobster_tests

# Full v4.29 parity build (CI-grade workload).
cmake -B build -DMYLOBSTER_BUILD_V4_29_PARITY=ON
cmake --build build --target mylobster_tests
ctest --test-dir build --output-on-failure -R "v4\\.29 \\["
```

## Hand-authored tests

A file is treated as hand-authored — the generator will not overwrite it —
when it contains the sentinel `// MYLOBSTERPP_HAND_AUTHORED` near the top.

To convert a parity gap into a real behavioral test:

1. Add `// MYLOBSTERPP_HAND_AUTHORED` as the first line.
2. Replace `PARITY_GAP(...)` calls with real `CHECK` / `REQUIRE` assertions
   against mylobsterpp's C++ surface.
3. Keep the upstream file:line reference in a comment near the test so the
   trace back to upstream is preserved.

## Regenerating

When the OpenClaw submodule is bumped to a new tag:

```bash
cd agent/openclaw && git checkout v2026.4.30   # or whatever tag
cd ../mylobsterpp/tests/v4_29
python3 generate.py            # writes new mirrors; preserves hand-authored
```

The generator is idempotent and skips any file containing the sentinel.

## Tagging convention

Catch2 tags drive ctest filtering:

- `[v4_29]` — every mirror test (use to run the full backlog)
- `[parity_gap]` — currently FAILs because abstraction is missing
- `[behavior]` — hand-authored, asserts against mylobsterpp's actual surface
- `[<id>]` — extension or subsystem id (anthropic, openai, agents, infra, ...)

Examples:

```bash
ctest -L v4_29                       # all parity tests
ctest -L "v4_29.*anthropic"          # anthropic only
ctest -R "behavior" --output-on-failure   # only hand-authored real tests
```
