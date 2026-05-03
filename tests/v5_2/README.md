# mylobsterpp v2026.5.2 OpenClaw parity tests

This tree is the **runnable parity backlog** for OpenClaw v2026.5.2. Each
upstream `*.test.ts` file under `agent/openclaw/` is mirrored 1:1 here as a
Catch2 file with one `TEST_CASE` per upstream `it()` / `test()` block.

## Layout

```
tests/v5_2/
    parity.hpp                    PARITY_GAP() macro + tagging conventions
    generate.py                   Generator (idempotent; protects hand-authored)
    extensions/<id>/<name>.cpp    Mirror of openclaw/extensions/<id>/<name>.test.ts
    src/<area>/<name>.cpp         Mirror of openclaw/src/<area>/<name>.test.ts
    ui/...                        Mirror of openclaw/ui/...
    test/...                      Mirror of openclaw/test/...
```

## Why this exists

mylobsterpp is a **conceptual** C++ port of OpenClaw, not a file-for-file
translation. v5.2 (target) is the latest stable OpenClaw release; the C++
port still lags many upstream behaviors (plugin SDK, ACP binding registry,
replay policy, auth profiles, ClawHub artifact resolver, etc.).

Running this suite gives a precise red bar that names every upstream
behavior we still owe, with file:line references back to upstream tests.
Each FAIL is a parity backlog item.

## Building

The mirrors are gated behind a CMake option because there are ~4.9k of them:

```bash
# Default build: existing per-subsystem tests only, fast.
cmake -B build
cmake --build build --target mylobster_tests

# Full v5.2 parity build (CI-grade workload).
cmake -B build -DMYLOBSTER_BUILD_V5_2_PARITY=ON
cmake --build build --target mylobster_v5_2_parity_tests
ctest --test-dir build --output-on-failure -R "v5\\.2 \\["
```

## Hand-authored tests

A file is treated as hand-authored — the generator will not overwrite it —
when it contains the sentinel `// MYLOBSTERPP_HANDWRITTEN_TEST` as the first
non-blank line of the file.

To convert a parity gap into a real behavioral test:

1. Add `// MYLOBSTERPP_HANDWRITTEN_TEST` as the first line.
2. Replace `PARITY_GAP(...)` calls with real `CHECK` / `REQUIRE` assertions
   against mylobsterpp's C++ surface.
3. Keep the upstream file:line reference in a comment near the test so the
   trace back to upstream is preserved.

## Regenerating

When the OpenClaw submodule is bumped to a new tag:

```bash
cd agent/openclaw && git checkout v2026.5.2   # or the next stable tag
cd ../mylobsterpp/tests/v5_2
python3 generate.py            # writes new mirrors; preserves hand-authored
```

The generator is idempotent and skips any file containing the sentinel.

## Tagging convention

Catch2 tags drive ctest filtering:

- `[v5_2]` — every mirror test (use to run the full backlog)
- `[parity_gap]` — currently FAILs because abstraction is missing
- `[behavior]` — hand-authored, asserts against mylobsterpp's actual surface
- `[<id>]` — extension or subsystem id (anthropic, openai, agents, infra, ...)

Examples:

```bash
ctest -L v5_2                        # all parity tests
ctest -L "v5_2.*anthropic"           # anthropic only
ctest -R "behavior" --output-on-failure   # only hand-authored real tests
```
