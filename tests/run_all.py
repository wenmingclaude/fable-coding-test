#!/usr/bin/env python3
"""Runs the test groups and prints what was actually verified.

    tests/run_all.py [--quick] [--bin-dir DIR] [--only NAME[,NAME...]] [--json FILE] [--keep]

Exit status 0 iff no group recorded a failure.
"""
import argparse
import importlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
from common import Context  # noqa: E402

GROUPS = ["cli", "fuzz", "constructive", "families", "metamorphic", "checker", "incremental", "scale"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="smaller instance counts, skip the largest instances")
    ap.add_argument("--bin-dir", help="directory with the binaries (default build/)")
    ap.add_argument("--only", help="comma-separated subset of: " + ", ".join(GROUPS))
    ap.add_argument("--json", help="write the collected numbers to this file")
    ap.add_argument("--jobs", type=int)
    ap.add_argument("--keep", action="store_true", help="keep the temporary directory")
    ap.add_argument("--fail-fast", action="store_true", help="stop at the first failure (used by mutation testing)")
    args = ap.parse_args()
    common.FAIL_FAST = args.fail_fast

    ctx = Context(args.bin_dir, args.quick, args.jobs, args.keep)
    names = args.only.split(",") if args.only else GROUPS
    reports = []
    t0 = time.time()
    print("binaries: %s   mode: %s   drat-trim: %s   tmp: %s" % (
        ctx.bin_dir, "quick" if ctx.quick else "full", "yes" if ctx.drat_trim else "not installed", ctx.tmp))
    for name in names:
        mod = importlib.import_module("test_" + name)
        t = time.time()
        rep = mod.run_group(ctx)
        rep.seconds = time.time() - t
        reports.append(rep)
        status = "ok" if not rep.failures else "FAILED (%d)" % len(rep.failures)
        print("\n== %s: %s  [%.1f s]" % (rep.name, status, rep.seconds))
        for k, v in rep.counters.items():
            print("   %-58s %s" % (k, v))
        for row in rep.rows:
            print("   " + row)
        for note in rep.notes:
            print("   note: " + note)
        for f in rep.failures[:10]:
            print("   FAILURE: " + f)
        sys.stdout.flush()
        if args.fail_fast and rep.failures:
            break
    failures = sum(len(r.failures) for r in reports)
    print("\n%s  (%d groups, %.1f s)" % ("ALL TESTS PASSED" if not failures else "%d FAILURES" % failures,
                                        len(reports), time.time() - t0))
    if args.json:
        with open(args.json, "w") as f:
            json.dump([{"group": r.name, "seconds": round(r.seconds, 1), "counters": r.counters, "rows": r.rows,
                        "notes": r.notes, "failures": r.failures} for r in reports], f, indent=1)
    ctx.cleanup()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
