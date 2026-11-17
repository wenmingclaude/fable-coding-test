#!/usr/bin/env python3
"""Benchmarks: baseline DPLL vs. the final solver, and one-feature-off ablations.

    bench/run_bench.py [--limit SEC] [--configs a,b,...] [--families x,y,...]
                       [--out DIR] [--jobs N] [--proof]

For every (instance, configuration) pair the wall-clock time, the answer and
the solver's own counters are written to <out>/runs.csv; <out>/summary.md
aggregates them.  Instances are generated deterministically (fixed seeds) by
tools/gen.py, so the whole table can be reproduced with one command.

Scoring: a run that hits the time limit counts as 2 x limit (PAR-2), the
usual convention when some configurations do not finish.

Answers are cross-checked: if two configurations give different SAT/UNSAT
answers for the same instance the script reports it loudly.  With --proof the
default configuration is also run with proof logging and the proof is checked,
which measures the cost of producing and verifying certificates.
"""
import argparse
import csv
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import cnf  # noqa: E402
import gen  # noqa: E402

# (family label, instance name, generator, args)
INSTANCES = [
    ("pigeonhole", "php-7-6", "php", (7, 6)),
    ("pigeonhole", "php-8-7", "php", (8, 7)),
    ("pigeonhole", "php-9-8", "php", (9, 8)),
    ("pigeonhole", "php-10-9", "php", (10, 9)),
    ("random 3-SAT (threshold)", "rand-150-a", "random", (150, 639, 3, 101)),
    ("random 3-SAT (threshold)", "rand-150-b", "random", (150, 639, 3, 102)),
    ("random 3-SAT (threshold)", "rand-150-c", "random", (150, 639, 3, 103)),
    ("random 3-SAT (threshold)", "rand-150-d", "random", (150, 639, 3, 104)),
    ("random 3-SAT (threshold)", "rand-200-a", "random", (200, 852, 3, 105)),
    ("random 3-SAT (threshold)", "rand-200-b", "random", (200, 852, 3, 106)),
    ("random 3-SAT (threshold)", "rand-200-c", "random", (200, 852, 3, 107)),
    ("random 3-SAT (threshold)", "rand-200-d", "random", (200, 852, 3, 108)),
    ("random 3-SAT (threshold)", "rand-250-a", "random", (250, 1065, 3, 109)),
    ("random 3-SAT (threshold)", "rand-250-b", "random", (250, 1065, 3, 110)),
    ("hidden solution", "planted-2000-r3.8-a", "planted", (2000, 7600, 3, 201)),
    ("hidden solution", "planted-2000-r3.8-b", "planted", (2000, 7600, 3, 202)),
    ("hidden solution", "planted-2000-r4.0", "planted", (2000, 8000, 3, 203)),
    ("hidden solution", "planted-5000-r3.7", "planted", (5000, 18500, 3, 204)),
    ("hidden solution", "planted-20000-r3.5", "planted", (20000, 70000, 3, 205)),
    ("hidden solution", "planted-400-r4.25", "planted", (400, 1700, 3, 206)),
    ("hidden solution", "planted5-1000-r14", "planted", (1000, 14000, 5, 207)),
    ("graph colouring", "col3-planted-600-1200", "color-planted", (600, 1200, 3, 301)),
    ("graph colouring", "col3-planted-600-1350", "color-planted", (600, 1350, 3, 302)),
    ("graph colouring", "col4-planted-300-1100", "color-planted", (300, 1100, 4, 303)),
    ("graph colouring", "col3-clique-200", "color-clique", (200, 300, 3, 304)),
    ("graph colouring", "col5-clique-150", "color-clique", (150, 500, 5, 305)),
    ("graph colouring", "col3-random-250-590", "color-random", (250, 590, 3, 306)),
    ("graph colouring", "col3-random-300-700", "color-random", (300, 700, 3, 307)),
    ("parity", "parity-same-60", "parity", (60, 401, 0)),
    ("parity", "parity-same-2000", "parity", (2000, 402, 0)),
    ("parity", "parity-shuffled-20", "parity", (20, 403, 1)),
    ("parity", "parity-shuffled-24", "parity", (24, 404, 1)),
    ("parity", "tseitin-4x4", "tseitin", (4, 4, 405)),
    ("parity", "tseitin-5x5", "tseitin", (5, 5, 406)),
    ("parity", "xor-planted-40", "xor-planted", (40, 40, 3, 407)),
    ("parity", "xor-planted-70", "xor-planted", (70, 70, 3, 408)),
    ("ordering principle", "ordering-14", "ordering", (14,)),
    ("ordering principle", "ordering-20", "ordering", (20,)),
    ("ordering principle", "ordering-26", "ordering", (26,)),
    ("propagation chains", "chain-200k-unsat", "chain", (200000, 1, 501)),
    ("propagation chains", "ladder-100k-unsat", "ladder", (100000, 1)),
    ("propagation chains", "cycle-199999", "cycle", (199999,)),
    ("duplicates / hidden core", "dup-2000-x50", "duplicates", (2000, 7000, 50, 601)),
    ("duplicates / hidden core", "core-3000", "with-core", (3000, 10500, 3, 5, 602)),
    ("duplicates / hidden core", "core-50000", "with-core", (50000, 175000, 3, 6, 603)),
    ("large", "planted5-200k-1M", "planted", (200000, 1000000, 5, 701)),
    ("large", "planted3-200k-600k", "planted", (200000, 600000, 3, 702)),
]

CONFIGS = {
    "dpll": None,                       # baseline binary
    "cdcl": [],                         # final solver, defaults
    "-minimize": ["--no-minimize"],
    "-phase-saving": ["--no-phase-saving"],
    "-reduce": ["--no-reduce"],
    "-blocker": ["--no-blocker"],
    "-inline-binary": ["--no-inline-binary"],
    "-vsids": ["--no-vsids"],
    "-simplify": ["--no-simplify"],
    "-stable": ["--no-stable"],
    "-target-phase": ["--no-target-phase"],
    "restart=luby": ["--restart=luby"],
    "restart=none": ["--restart=none"],
    "phase=0": ["--phase=0"],
    "phase=1": ["--phase=1"],
}


def run(cmd, limit):
    t = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=limit)
        return r.returncode, r.stdout, time.time() - t
    except subprocess.TimeoutExpired:
        return None, "", limit


def stat(text, key):
    for line in text.splitlines():
        if line.startswith("c " + key):
            try:
                return float(line[len(key) + 2:].split()[0])
            except (ValueError, IndexError):
                return None
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=float, default=30.0)
    ap.add_argument("--configs", default=",".join(CONFIGS))
    ap.add_argument("--families")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "bench"))
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--proof", action="store_true", help="also measure proof logging and checking")
    ap.add_argument("--bin-dir", default=os.path.join(ROOT, "build"))
    args = ap.parse_args()
    configs = args.configs.split(",")
    inst_dir = os.path.join(args.out, "instances")
    os.makedirs(inst_dir, exist_ok=True)
    instances = [i for i in INSTANCES if not args.families or any(f in i[0] for f in args.families.split(","))]

    print("generating %d instances ..." % len(instances))
    meta = {}
    for family, name, g, a in instances:
        path = os.path.join(inst_dir, name + ".cnf")
        if not os.path.exists(path):
            n, clauses, expected = gen.generate(g, *a)
            cnf.write_cnf(path, n, clauses, ["expected " + expected] if expected else [])
        with open(path) as f:
            head = [next(f) for _ in range(2)]
        expected = head[0].split()[2] if head[0].startswith("c expected") else None
        hdr = [h for h in head if h.startswith("p cnf")][0].split()
        meta[name] = (family, path, expected, int(hdr[2]), int(hdr[3]))

    solver = os.path.join(args.bin_dir, "sat_solver")
    dpll = os.path.join(args.bin_dir, "dpll")
    checker = os.path.join(args.bin_dir, "rup_check")
    rows = []
    lock = threading.Lock()

    def job(item):
        name, config = item
        family, path, expected, nv, nc = meta[name]
        if config == "dpll":
            cmd = [dpll, "--no-model", path]
        else:
            cmd = [solver, "--no-model", *CONFIGS[config], path]
        rc, text, secs = run(cmd, args.limit)
        status = {10: "SAT", 20: "UNSAT", None: "timeout"}.get(rc, "error")
        row = {"family": family, "instance": name, "vars": nv, "clauses": nc, "expected": expected or "",
               "config": config, "status": status, "seconds": round(secs, 3),
               "conflicts": stat(text, "conflicts"), "decisions": stat(text, "decisions"),
               "propagations": stat(text, "propagations"), "peak_mb": stat(text, "peak memory"),
               "proof_mb": "", "check_seconds": "", "proof_seconds": ""}
        if args.proof and config == "cdcl" and status == "UNSAT":
            proof = os.path.join(args.out, name + ".rup")
            rc2, _, psecs = run([solver, "--no-model", "-q", "--proof", proof, path], args.limit * 4)
            if rc2 == 20:
                row["proof_seconds"] = round(psecs, 3)
                row["proof_mb"] = round(os.path.getsize(proof) / 1e6, 2)
                rc3, _, csecs = run([checker, path, proof], args.limit * 20)
                row["check_seconds"] = round(csecs, 3) if rc3 == 0 else "REJECTED"
            if os.path.exists(proof):
                os.remove(proof)
        with lock:
            rows.append(row)
            print("%-24s %-15s %-8s %8.2f s" % (name, config, status, secs))
            sys.stdout.flush()

    items = [(name, c) for name in meta for c in configs]
    with ThreadPoolExecutor(args.jobs) as pool:
        list(pool.map(job, items))

    rows.sort(key=lambda r: (list(meta).index(r["instance"]), configs.index(r["config"])))
    with open(os.path.join(args.out, "runs.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    # ---- consistency ---------------------------------------------------------
    problems = []
    for name in meta:
        answers = {r["status"] for r in rows if r["instance"] == name and r["status"] in ("SAT", "UNSAT")}
        if len(answers) > 1:
            problems.append("configurations disagree on %s" % name)
        if meta[name][2] and answers - {meta[name][2]}:
            problems.append("%s: answer differs from the status known by construction" % name)
        if any(r["status"] == "error" for r in rows if r["instance"] == name):
            problems.append("a run crashed on %s" % name)
        if any(r["check_seconds"] == "REJECTED" for r in rows if r["instance"] == name):
            problems.append("proof rejected for %s" % name)

    # ---- summary ---------------------------------------------------------------
    def par2(rs):
        return sum(r["seconds"] if r["status"] in ("SAT", "UNSAT") else 2 * args.limit for r in rs)
    families = []
    for i in instances:
        if i[0] not in families:
            families.append(i[0])
    out = ["# Benchmark summary", "",
           "Time limit %.0f s per run, %d parallel jobs, PAR-2 scoring (timeout = 2 x limit)." % (args.limit, args.jobs), ""]
    out += ["## Solved instances and PAR-2 time per configuration", "",
            "| configuration | solved | PAR-2 total (s) | " + " | ".join(families) + " |",
            "|---|---|---|" + "---|" * len(families)]
    for c in configs:
        rs = [r for r in rows if r["config"] == c]
        cells = []
        for fam in families:
            fr = [r for r in rs if r["family"] == fam]
            cells.append("%d/%d, %.1f" % (sum(1 for r in fr if r["status"] in ("SAT", "UNSAT")), len(fr), par2(fr)))
        out.append("| `%s` | %d/%d | %.1f | %s |" % (
            c, sum(1 for r in rs if r["status"] in ("SAT", "UNSAT")), len(rs), par2(rs), " | ".join(cells)))
    out += ["", "Family cells: solved/total, PAR-2 seconds.", ""]
    if "dpll" in configs and "cdcl" in configs:
        out += ["## Baseline DPLL vs. final solver, per instance", "",
                "| instance | vars | clauses | answer | DPLL s | DPLL decisions | CDCL s | CDCL conflicts | speed-up |",
                "|---|---|---|---|---|---|---|---|---|"]
        for name in meta:
            d = [r for r in rows if r["instance"] == name and r["config"] == "dpll"][0]
            c = [r for r in rows if r["instance"] == name and r["config"] == "cdcl"][0]
            ans = c["status"] if c["status"] != "timeout" else d["status"]
            def t(r):
                return "%.2f" % r["seconds"] if r["status"] != "timeout" else ">%.0f" % args.limit
            if d["status"] == "timeout" and c["status"] != "timeout":
                sp = ">%.0fx" % (args.limit / max(c["seconds"], 0.01))
            elif c["status"] == "timeout" and d["status"] != "timeout":
                sp = "<%.2fx" % (d["seconds"] / args.limit)
            elif c["status"] == "timeout":
                sp = "-"
            else:
                sp = "%.1fx" % (max(d["seconds"], 0.005) / max(c["seconds"], 0.005))
            out.append("| %s | %d | %d | %s | %s | %s | %s | %s | %s |" % (
                name, meta[name][3], meta[name][4], ans, t(d),
                "%d" % d["decisions"] if d["decisions"] is not None else "-", t(c),
                "%d" % c["conflicts"] if c["conflicts"] is not None else "-", sp))
        out.append("")
    if args.proof:
        out += ["## Cost of certificates (default configuration, UNSAT instances)", "",
                "| instance | solve s | solve s with proof | proof MB | check s | check / solve |", "|---|---|---|---|---|---|"]
        for r in rows:
            if r["config"] == "cdcl" and r["proof_seconds"] != "":
                ratio = "%.1fx" % (r["check_seconds"] / max(r["seconds"], 0.01)) \
                    if isinstance(r["check_seconds"], float) else "-"
                out.append("| %s | %.2f | %.2f | %.2f | %s | %s |" % (
                    r["instance"], r["seconds"], r["proof_seconds"], r["proof_mb"], r["check_seconds"], ratio))
        out.append("")
    out += ["## Consistency", "", ("No disagreement between configurations, no crash, every checked proof accepted."
                                  if not problems else "\n".join("* PROBLEM: " + p for p in problems)), ""]
    with open(os.path.join(args.out, "summary.md"), "w") as f:
        f.write("\n".join(out))
    print("\n".join(out))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
