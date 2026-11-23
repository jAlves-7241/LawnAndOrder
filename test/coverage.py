#!/usr/bin/env python3
"""Aggregate gcov line and function coverage across PlatformIO native unit tests.

Usage:
    # Linux / macOS:
    PLATFORMIO_BUILD_FLAGS="--coverage -O0 -lgcov" pio test -e native
    python3 test/coverage.py

    # Windows PowerShell:
    $env:PLATFORMIO_BUILD_FLAGS="--coverage -O0 -lgcov"; pio test -e native
    python test/coverage.py
"""
import glob
import json
import os
import subprocess
import sys
from collections import defaultdict


def main() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    prod_files = sorted(
        os.path.relpath(p, repo_root).replace("\\", "/")
        for p in (
            glob.glob(os.path.join(repo_root, "src", "**", "*.cpp"), recursive=True)
            + glob.glob(os.path.join(repo_root, "lib", "**", "*.cpp"), recursive=True)
        )
    )

    gcda_files = sorted(
        glob.glob(
            os.path.join(repo_root, ".pio", "build", "native", "test", "**", "*.gcda"),
            recursive=True,
        )
    )
    if not gcda_files:
        sys.stderr.write(
            "No .gcda files found under .pio/build/native/test/.\n"
            "Run: PLATFORMIO_BUILD_FLAGS=\"--coverage -O0 -lgcov\" pio test -e native\n"
        )
        return 1

    line_counts = defaultdict(dict)
    fn_counts = defaultdict(dict)

    for gcda in gcda_files:
        out = subprocess.check_output(
            ["gcov", "--json-format", "--stdout", gcda],
            cwd=repo_root,
            text=True,
        )
        data = json.loads(out)
        for fobj in data.get("files", []):
            norm = os.path.normpath(os.path.join(repo_root, fobj["file"]))
            rel = os.path.relpath(norm, repo_root).replace("\\", "/")
            if not rel.endswith(".cpp"):
                continue
            if not (rel.startswith("src/") or rel.startswith("lib/")):
                continue
            for ln in fobj.get("lines", []):
                num = ln["line_number"]
                line_counts[rel][num] = max(line_counts[rel].get(num, 0), ln["count"])
            for fn in fobj.get("functions", []):
                name = fn["demangled_name"]
                fn_counts[rel][name] = max(
                    fn_counts[rel].get(name, 0), fn["execution_count"]
                )

    print(f"Aggregated {len(gcda_files)} native test suites\n")
    print("| File | Lines | Line % | Functions | Func % |")
    print("|---|---:|---:|---:|---:|")

    tot_exec = tot_lines = tot_fn_exec = tot_fns = 0
    src_exec = src_lines = src_fn_exec = src_fns = 0

    for rel in prod_files:
        lmap = line_counts.get(rel, {})
        fmap = fn_counts.get(rel, {})
        n_lines = len(lmap)
        n_exec = sum(1 for c in lmap.values() if c > 0)
        n_fns = len(fmap)
        n_fn_exec = sum(1 for c in fmap.values() if c > 0)

        tot_exec += n_exec
        tot_lines += n_lines
        tot_fn_exec += n_fn_exec
        tot_fns += n_fns
        if rel.startswith("src/"):
            src_exec += n_exec
            src_lines += n_lines
            src_fn_exec += n_fn_exec
            src_fns += n_fns

        if n_lines == 0:
            print(f"| `{rel}` | 0 / 0 | 0.0% (not compiled) | 0 / 0 | 0.0% |")
        else:
            lpct = 100.0 * n_exec / n_lines
            fpct = (100.0 * n_fn_exec / n_fns) if n_fns else 100.0
            print(
                f"| `{rel}` | {n_exec} / {n_lines} | {lpct:.1f}% | "
                f"{n_fn_exec} / {n_fns} | {fpct:.1f}% |"
            )

    if src_lines:
        print(
            f"| **Total `src/` (compiled)** | **{src_exec} / {src_lines}** | "
            f"**{100.0 * src_exec / src_lines:.1f}%** | "
            f"**{src_fn_exec} / {src_fns}** | **{100.0 * src_fn_exec / src_fns:.1f}%** |"
        )
    if tot_lines:
        print(
            f"| **Total `src/` + `lib/` (compiled)** | **{tot_exec} / {tot_lines}** | "
            f"**{100.0 * tot_exec / tot_lines:.1f}%** | "
            f"**{tot_fn_exec} / {tot_fns}** | **{100.0 * tot_fn_exec / tot_fns:.1f}%** |"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
