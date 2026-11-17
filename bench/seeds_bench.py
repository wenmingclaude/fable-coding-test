#!/usr/bin/env python3
"""Ablations over many random seeds per family.

    bench/seeds_bench.py [--seeds 8] [--limit 30] [--jobs 2] [--configs a,b] [--out FILE.md]

Why this exists: in the single-instance-per-row benchmark (run_bench.py) one
instance that happened to sit near the time limit decided almost every PAR-2
comparison.  Here every family contributes several instances generated with
different seeds, and the table reports, per family and configuration, the
number solved, the median time and the PAR-2 sum -- so a configuration has to
win on most seeds, not on one lucky instance.
"""
import argparse
import os
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import cnf  # noqa: E402
import gen  # noqa: E402

FAMILIES = [
    ("planted 3-SAT n=2000 r=4.0", "planted", lambda s: (2000, 8000, 3, 1000 + s)),
    ("planted 3-SAT n=5000 r=3.8", "planted", lambda s: (5000, 19000, 3, 2000 + s)),
    ("random 3-SAT n=250 r=4.26", "random", lambda s: (250, 1065, 3, 3000 + s)),
    ("3-colouring planted n=800 m=1840", "color-planted", lambda s: (800, 1840, 3, 4000 + s)),
    ("tseitin torus 5x5", "tseitin", lambda s: (5, 5, 5000 + s)),
    ("parity, shuffled, n=24", "parity", lambda s: (24, 6000 + s, 1)),
]

CONFIGS = {
    "default": [],
    "-phase-saving": ["--no-phase-saving"],
    "-minimize": ["--no-minimize"],
    "-blocker": ["--no-blocker"],
    "-stable": ["--no-stable"],
    "phase=0": ["--phase=0"],
    # control experiment for the phase-saving result (docs/RESULTS.md, experiment E4)
    "-phase-saving,phase=0": ["--no-phase-saving", "--phase=0"],
    "-phase-saving,phase=1": ["--no-phase-saving", "--phase=1"],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=8)
    ap.add_argument("--limit", type=float, default=30)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--configs", default="default,-phase-saving,-minimize,-blocker,-stable,phase=0",
                    help="comma-separated; use ';' as separator for names that contain a comma")
    ap.add_argument("--families", help="substring filter on family names, comma-separated")
    ap.add_argument("--solver", default=os.path.join(ROOT, "build", "sat_solver"),
                    help="solver binary (e.g. build/sat_solver_occorder for experiment E2)")
    ap.add_argument("--out")
    args = ap.parse_args()
    configs = [c for c in CONFIGS if c in args.configs.split(";")] if ";" in args.configs else args.configs.split(",")
    global FAMILIES
    if args.families:
        FAMILIES = [f for f in FAMILIES if any(x in f[0] for x in args.families.split(","))]
    solver = args.solver
    work = os.path.join(ROOT, "build", "bench", "seeds")
    os.makedirs(work, exist_ok=True)

    jobs = []
    for fam, g, argf in FAMILIES:
        for s in range(args.seeds):
            path = os.path.join(work, "%s-%d.cnf" % (g, argf(s)[-2] if g == "parity" else argf(s)[-1]))
            if not os.path.exists(path):
                n, clauses, _ = gen.generate(g, *argf(s))
                cnf.write_cnf(path, n, clauses)
            for c in configs:
                jobs.append((fam, s, c, path))

    results = {}

    def run(job):
        fam, s, c, path = job
        t = time.time()
        try:
            r = subprocess.run([solver, "--no-model", *CONFIGS[c], path], capture_output=True, text=True,
                               timeout=args.limit)
            status = {10: "SAT", 20: "UNSAT"}.get(r.returncode, "error")
            secs = time.time() - t
        except subprocess.TimeoutExpired:
            status, secs = "timeout", args.limit
        results[(fam, s, c)] = (status, secs)
        print("%-34s seed %d  %-14s %-8s %6.2f s" % (fam, s, c, status, secs))
        sys.stdout.flush()

    with ThreadPoolExecutor(args.jobs) as pool:
        list(pool.map(run, jobs))

    problems = []
    for fam, _, _ in FAMILIES:
        for s in range(args.seeds):
            answers = {results[(fam, s, c)][0] for c in configs} - {"timeout"}
            if len(answers) > 1 or "error" in answers:
                problems.append("%s seed %d: %s" % (fam, s, answers))

    lines = ["%d seeds per family, limit %.0f s, %d parallel jobs.  Cell: solved / median s / PAR-2 s "
             "(timeout = 2 x limit; median counts a timeout as the limit)." % (args.seeds, args.limit, args.jobs), "",
             "| family | " + " | ".join("`%s`" % c for c in configs) + " |", "|---|" + "---|" * len(configs)]
    totals = {c: [0, 0.0] for c in configs}
    for fam, _, _ in FAMILIES:
        cells = []
        for c in configs:
            rs = [results[(fam, s, c)] for s in range(args.seeds)]
            solved = sum(1 for st, _ in rs if st != "timeout")
            med = statistics.median(secs for _, secs in rs)
            par2 = sum(secs if st != "timeout" else 2 * args.limit for st, secs in rs)
            totals[c][0] += solved
            totals[c][1] += par2
            cells.append("%d / %.2f / %.0f" % (solved, med, par2))
        lines.append("| %s | %s |" % (fam, " | ".join(cells)))
    lines.append("| **total** | %s |" % " | ".join("**%d / %.0f**" % (totals[c][0], totals[c][1]) for c in configs))
    lines.append("")
    lines.append("Answer consistency: " + ("all configurations agree on every instance." if not problems
                                          else "PROBLEMS: " + "; ".join(problems)))
    text = "\n".join(lines) + "\n"
    print(text)
    if args.out:
        open(args.out, "w").write(text)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
