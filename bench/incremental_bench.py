#!/usr/bin/env python3
"""Incremental solving: one solver instance answering many assumption queries
versus a fresh solver run per query (the formula plus unit clauses).

    bench/incremental_bench.py [--out FILE.md]

For each formula a fixed list of random assumption sets is generated.  The
two methods must give the same SAT/UNSAT answer for every query; the table
reports total time and total conflicts.  Fresh runs include process start-up
and parsing, which is the honest comparison: that is exactly the work an
incremental interface avoids repeating.
"""
import argparse
import os
import random
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import cnf  # noqa: E402
import gen  # noqa: E402

CASES = [
    # (name, family, args, number of queries, assumptions per query)
    ("planted 3-SAT n=5000 r=3.7", "planted", (5000, 18500, 3, 11), 40, 4),
    ("random 3-SAT n=200 r=4.2", "random", (200, 840, 3, 12), 40, 3),
    ("3-colouring planted n=600 m=1300", "color-planted", (600, 1300, 3, 13), 40, 4),
    ("pigeonhole 9 into 9 (sat)", "php", (9, 9), 40, 6),
    ("planted 5-SAT 200k vars, 1M clauses", "planted", (200000, 1000000, 5, 14), 20, 5),
]


def conflicts(text):
    for line in text.splitlines():
        if line.startswith("c conflicts"):
            return int(line.split()[2])
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--bin-dir", default=os.path.join(ROOT, "build"))
    args = ap.parse_args()
    solver = os.path.join(args.bin_dir, "sat_solver")
    work = os.path.join(ROOT, "build", "bench", "incremental")
    os.makedirs(work, exist_ok=True)
    lines = ["| formula | queries (SAT / UNSAT) | incremental: s | conflicts | fresh runs: s | conflicts | time ratio |",
             "|---|---|---|---|---|---|---|"]
    for name, family, gargs, nq, k in CASES:
        n, clauses, _ = gen.generate(family, *gargs)
        base = os.path.join(work, "f.cnf")
        cnf.write_cnf(base, n, clauses)
        rng = random.Random(len(name))
        queries = [[rng.choice([1, -1]) * rng.randint(1, n) for _ in range(k)] for _ in range(nq)]
        qfile = os.path.join(work, "q.txt")
        with open(qfile, "w") as f:
            for q in queries:
                f.write(" ".join(map(str, q)) + " 0\n")
        t = time.time()
        r = subprocess.run([solver, "--no-model", "--queries", qfile, base], capture_output=True, text=True)
        t_inc = time.time() - t
        inc_answers = [res["status"] for res in cnf.parse_solver_output(r.stdout)]
        c_inc = conflicts(r.stdout)
        t_fresh, c_fresh, fresh_answers = 0.0, 0, []
        for q in queries:
            path = os.path.join(work, "g.cnf")
            cnf.write_cnf(path, n, clauses + [[l] for l in q])
            t = time.time()
            r = subprocess.run([solver, "--no-model", path], capture_output=True, text=True)
            t_fresh += time.time() - t
            c_fresh += conflicts(r.stdout)
            fresh_answers.append({10: "SAT", 20: "UNSAT"}.get(r.returncode, "?"))
        same = all(a == b or (a == "UNSAT_ASSUMPTIONS" and b == "UNSAT") for a, b in zip(inc_answers, fresh_answers))
        if not same or len(inc_answers) != nq:
            print("ANSWERS DIFFER on %s" % name)
            return 1
        sat = sum(1 for a in fresh_answers if a == "SAT")
        lines.append("| %s | %d (%d / %d) | %.2f | %d | %.2f | %d | %.1fx |" % (
            name, nq, sat, nq - sat, t_inc, c_inc, t_fresh, c_fresh, t_fresh / max(t_inc, 1e-3)))
        print(lines[-1])
        sys.stdout.flush()
    text = "\n".join(lines) + "\n\nAll answers agree between the two methods.\n"
    if args.out:
        open(args.out, "w").write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
