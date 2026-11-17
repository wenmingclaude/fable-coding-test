#!/usr/bin/env python3
"""A/B timing of two solver command lines on the same instances.

    bench/compare.py --a "build/sat_solver" --b "build/sat_solver_prefetch" \
                     [--reps 3] [--limit 60] [--out FILE.md] name1 name2 ...

Instance names are those of bench/run_bench.py (generated on demand into
build/bench/instances).  Each command is run --reps times per instance and the
median wall-clock time is reported, together with the conflict count, which
must be identical for changes that are not supposed to alter the search.
"""
import argparse
import os
import statistics
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "bench"))
import cnf  # noqa: E402
import gen  # noqa: E402
from run_bench import INSTANCES, stat  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--limit", type=float, default=60)
    ap.add_argument("--out")
    ap.add_argument("names", nargs="+")
    args = ap.parse_args()
    inst_dir = os.path.join(ROOT, "build", "bench", "instances")
    os.makedirs(inst_dir, exist_ok=True)
    lines = ["| instance | A: median s | B: median s | B / A | conflicts A | conflicts B |", "|---|---|---|---|---|---|"]
    ratios = []
    for name in args.names:
        spec = [i for i in INSTANCES if i[1] == name]
        if not spec:
            sys.exit("unknown instance " + name)
        path = os.path.join(inst_dir, name + ".cnf")
        if not os.path.exists(path):
            n, clauses, expected = gen.generate(spec[0][2], *spec[0][3])
            cnf.write_cnf(path, n, clauses, ["expected " + expected] if expected else [])
        res = {}
        for key, cmd in (("a", args.a), ("b", args.b)):
            times, confl = [], None
            for _ in range(args.reps):
                t = time.time()
                try:
                    r = subprocess.run(cmd.split() + ["--no-model", path], capture_output=True, text=True,
                                       timeout=args.limit)
                    times.append(time.time() - t)
                    confl = stat(r.stdout, "conflicts")
                except subprocess.TimeoutExpired:
                    times.append(float("inf"))
            res[key] = (statistics.median(times), confl)
        (ta, ca), (tb, cb) = res["a"], res["b"]
        ratio = tb / ta if ta > 0 and ta != float("inf") and tb != float("inf") else float("nan")
        if ratio == ratio:
            ratios.append(ratio)
        fmt = lambda t: "%.2f" % t if t != float("inf") else ">%.0f" % args.limit
        lines.append("| %s | %s | %s | %s | %s | %s |" % (
            name, fmt(ta), fmt(tb), "%.2f" % ratio if ratio == ratio else "-",
            "%d" % ca if ca is not None else "-", "%d" % cb if cb is not None else "-"))
        print(lines[-1])
        sys.stdout.flush()
    if ratios:
        lines.append("")
        lines.append("Geometric mean of B / A over %d instances: %.3f" % (
            len(ratios), statistics.geometric_mean(ratios)))
    text = "A = `%s`\n\nB = `%s`\n\n%d repetitions, median.\n\n%s\n" % (args.a, args.b, args.reps, "\n".join(lines))
    print(text)
    if args.out:
        open(args.out, "w").write(text)


if __name__ == "__main__":
    main()
