#!/usr/bin/env python3
"""
v2026.5.2 OpenClaw test parity generator.

Walks the OpenClaw v2026.5.2 submodule and emits one Catch2 test file per
upstream Vitest test file. Each upstream `it("...")` block becomes a Catch2
TEST_CASE that calls PARITY_GAP() with the upstream file:line reference, the
test description, and a tag set derived from the upstream path.

The generator is idempotent: any file that already exists *and* contains the
sentinel `// MYLOBSTERPP_HANDWRITTEN_TEST` is skipped. Edit those by hand to
upgrade gaps into real behavioral assertions; re-running the generator will
not stomp them.

Usage:
    cd <repo>/agent/mylobsterpp/tests/v5_2
    python3 generate.py [--dry-run] [--filter <substring>]

Outputs into ./<upstream-path>/<basename>.cpp where <upstream-path> mirrors
the OpenClaw repo layout (e.g. extensions/anthropic/index.cpp).
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Iterable

# Paths assume this script lives at <repo>/agent/mylobsterpp/tests/v4_29/.
SCRIPT_DIR = Path(__file__).resolve().parent
MYLOBSTERPP_DIR = SCRIPT_DIR.parent.parent
OPENCLAW_DIR = MYLOBSTERPP_DIR.parent / "openclaw"

# Roots inside openclaw/ to scan for test files.
SCAN_ROOTS = [
    "src",
    "extensions",
    "packages",
    "ui/src",
    "test",
]

# Skip patterns (live tests, fixtures, vendor, build artifacts).
SKIP_PATTERN = re.compile(
    r"(node_modules|/dist/|/dist-runtime/|/vendor/|"
    r"\.live\.test\.ts$|\.e2e\.test\.ts$|test-fixtures/)"
)

HAND_AUTHORED_SENTINEL = "// MYLOBSTERPP_HANDWRITTEN_TEST"


def is_hand_authored(path: Path) -> bool:
    """Returns True if the file's first non-blank line is the sentinel.
    The sentinel lives on line 1 specifically so it can't be confused with the
    generator's own self-describing header text."""
    try:
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                s = line.strip()
                if not s:
                    continue
                return s == HAND_AUTHORED_SENTINEL
    except OSError:
        pass
    return False

# Match `it(` or `test(` at any indentation. Captures the description string.
IT_PATTERN = re.compile(
    r"""(?:^|\n)\s*(?:it|test)(?:\.skip|\.only|\.todo|\.fails)?\s*\(\s*["'`](?P<desc>(?:[^"'`\\]|\\.)*)["'`]"""
)
DESCRIBE_PATTERN = re.compile(
    r"""(?:^|\n)\s*describe(?:\.skip|\.only|\.each)?\s*\(\s*["'`](?P<desc>(?:[^"'`\\]|\\.)*)["'`]"""
)


def scan_upstream_tests() -> list[Path]:
    found: list[Path] = []
    for root in SCAN_ROOTS:
        base = OPENCLAW_DIR / root
        if not base.exists():
            continue
        for p in base.rglob("*.test.ts"):
            rel = p.relative_to(OPENCLAW_DIR).as_posix()
            if SKIP_PATTERN.search(rel):
                continue
            found.append(p)
    return sorted(found)


def to_tags(rel_path: str) -> list[str]:
    parts = rel_path.split("/")
    tags = ["v5_2", "parity_gap"]
    # extensions/<id>/<file>.test.ts -> add <id>
    if parts and parts[0] == "extensions" and len(parts) >= 2:
        tags.append(parts[1])
    elif parts and parts[0] == "src" and len(parts) >= 2:
        # src/<subsystem>/... -> add <subsystem>
        tags.append(parts[1])
    elif parts and parts[0] == "packages" and len(parts) >= 2:
        tags.append(parts[1])
    return tags


def cpp_string_escape(s: str) -> str:
    # Escape backslash and double quote; collapse newlines.
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    s = s.replace("\n", "\\n").replace("\r", "")
    return s


def find_it_blocks(content: str) -> list[tuple[int, str]]:
    """Returns list of (line_number, description) for every it()/test() block."""
    out: list[tuple[int, str]] = []
    for m in IT_PATTERN.finditer(content):
        start = m.start("desc")
        line = content.count("\n", 0, start) + 1
        out.append((line, m.group("desc")))
    return out


def relative_include_to_parity(out_path: Path) -> str:
    """Return a relative include path from out_path to v4_29/parity.hpp."""
    parity = SCRIPT_DIR / "parity.hpp"
    rel = Path("..") / Path(parity.relative_to(SCRIPT_DIR))
    # walk up from out_path's parent to SCRIPT_DIR
    depth = len(out_path.parent.relative_to(SCRIPT_DIR).parts)
    return ("../" * depth) + "parity.hpp"


def render_test_case(
    upstream_rel: str,
    upstream_line: int,
    description: str,
    tags: list[str],
    index: int,
) -> str:
    tag_str = "".join(f"[{t}]" for t in tags)
    desc_escaped = cpp_string_escape(description)
    upstream_ref = f"{upstream_rel}:{upstream_line}"
    upstream_ref_escaped = cpp_string_escape(upstream_ref)
    # Include line number in name to disambiguate duplicate it() descriptions
    # (e.g. it.each() blocks share a description text).
    name = f"v5.2 [{upstream_rel}:{upstream_line}] {description}"
    name_escaped = cpp_string_escape(name)
    # PARITY_GAP requires `note`; use a generic "ported as gap" message.
    return (
        f'TEST_CASE("{name_escaped}",\n'
        f'          "{tag_str}") {{\n'
        f'    PARITY_GAP("{upstream_ref_escaped}",\n'
        f'               "upstream it() block #{index}",\n'
        f'               "Auto-generated parity gap; replace with real assertion when '
        f'mylobsterpp grows the upstream abstraction.");\n'
        f'}}\n'
    )


def render_file(upstream_rel: str, it_blocks: list[tuple[int, str]], tags: list[str], parity_inc: str) -> str:
    header = (
        f"// v2026.5.2 OpenClaw parity mirror of {upstream_rel}\n"
        f"//\n"
        f"// Auto-generated by tests/v5_2/generate.py from the openclaw v2026.5.2\n"
        f"// submodule. Each TEST_CASE corresponds to an upstream it()/test() block;\n"
        f"// the body PARITY_GAP()s with a precise upstream file:line reference.\n"
        f"//\n"
        f"// To convert a gap into a real test, replace the PARITY_GAP body with\n"
        f"// behavioral assertions and add the {HAND_AUTHORED_SENTINEL} sentinel\n"
        f"// near the top of the file. The generator will then leave it alone.\n"
        f"\n"
        f'#include "{parity_inc}"\n'
        f"\n"
    )
    cases: list[str] = []
    if not it_blocks:
        cases.append(
            f'TEST_CASE("v5.2 [{cpp_string_escape(upstream_rel)}] empty upstream test file",\n'
            f'          "{"".join(f"[{t}]" for t in tags)}") {{\n'
            f'    PARITY_GAP("{cpp_string_escape(upstream_rel)}:1",\n'
            f'               "upstream file is empty/contract-only",\n'
            f'               "Upstream test file contains no direct it() blocks "\n'
            f'               "(usually a contract-test re-export). Track via the contract bundle.");\n'
            f'}}\n'
        )
    for i, (line, desc) in enumerate(it_blocks):
        cases.append(render_test_case(upstream_rel, line, desc, tags, i))
    return header + "\n".join(cases)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--filter", default="")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if not OPENCLAW_DIR.exists():
        print(f"openclaw dir not found at {OPENCLAW_DIR}")
        return 1

    upstream_files = scan_upstream_tests()
    print(f"Found {len(upstream_files)} upstream .test.ts files")

    written = 0
    skipped_existing = 0
    skipped_filter = 0

    for src in upstream_files:
        rel = src.relative_to(OPENCLAW_DIR).as_posix()
        if args.filter and args.filter not in rel:
            skipped_filter += 1
            continue

        # Compute output path: tests/v4_29/<rel-without-.test.ts>.cpp
        # Replace any '.' in basename with '_' to be safe for the C++ filename.
        rel_path = Path(rel)
        out_dir = SCRIPT_DIR / rel_path.parent
        # Drop ".test.ts" suffix (12 chars from .test.ts being 8) — use .stem twice
        base = rel_path.name
        if base.endswith(".test.ts"):
            base = base[: -len(".test.ts")]
        # sanitize basename: replace dots and dashes (legal but unusual in C++ filenames) -> keep as-is, dashes ok
        out_path = out_dir / f"{base}.cpp"

        if out_path.exists() and is_hand_authored(out_path):
            skipped_existing += 1
            if args.verbose:
                print(f"  skip (hand-authored): {out_path.relative_to(SCRIPT_DIR)}")
            continue

        try:
            content = src.read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            print(f"  read error: {src}: {e}")
            continue

        it_blocks = find_it_blocks(content)
        tags = to_tags(rel)
        parity_inc = relative_include_to_parity(out_path)
        rendered = render_file(rel, it_blocks, tags, parity_inc)

        if args.dry_run:
            print(f"  would write: {out_path.relative_to(SCRIPT_DIR)} ({len(it_blocks)} cases)")
            continue

        out_dir.mkdir(parents=True, exist_ok=True)
        out_path.write_text(rendered, encoding="utf-8")
        written += 1
        if args.verbose:
            print(f"  wrote: {out_path.relative_to(SCRIPT_DIR)} ({len(it_blocks)} cases)")

    print(f"\nDone. wrote={written} skipped_existing={skipped_existing} skipped_filter={skipped_filter}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
