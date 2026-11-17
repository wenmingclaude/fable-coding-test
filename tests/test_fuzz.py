"""A. Differential testing against brute force.

Thousands of small formulas (0..18 variables), deliberately irregular: empty
clauses, unit clauses, repeated literals, tautologies, duplicate clauses,
unused variables.  For each one:

  * the solver's SAT/UNSAT answer must equal exhaustive enumeration;
  * the model / proof must pass the independent checkers;
  * the baseline DPLL solver must agree as well (it is tested, too);
  * on a sample, the proof is additionally replayed by the naive Python
    checker (also under strict deletion semantics), in additions-only mode,
    and by drat-trim if a binary is present;
  * on a sample, the solver is re-run with each ablation switch, which
    exercises code paths the default configuration rarely takes.
"""
import random

import gen
from common import Failure, Report, brute_status, certify, check_proof, run, write_formula

ABLATIONS = [
    ["--no-minimize"], ["--no-phase-saving"], ["--no-reduce"], ["--no-blocker"], ["--no-inline-binary"],
    ["--no-vsids"], ["--no-simplify"], ["--no-stable"], ["--no-target-phase"], ["--restart=luby"],
    ["--restart=none"], ["--phase=1"], ["--proof-no-deletions"],
    ["--no-blocker", "--no-inline-binary", "--no-minimize", "--no-vsids", "--restart=none", "--no-simplify"],
]


def make_formula(seed):
    rng = random.Random(seed)
    kind = rng.random()
    if kind < 0.55:
        n = rng.randint(0, 18)
        m = rng.randint(0, max(1, int(n * rng.uniform(1.0, 6.0))))
        return gen.fuzz(n, m, seed)
    if kind < 0.9:
        # around the 3-SAT threshold, where SAT and UNSAT are equally likely
        n = rng.randint(5, 18)
        m = int(n * rng.uniform(3.5, 5.0))
        return gen.random_ksat(n, m, 3, seed)
    n = rng.randint(4, 16)
    return gen.random_ksat(n, int(n * rng.uniform(1.5, 2.5)), 2, seed)   # 2-SAT, lots of binary clauses


def one(ctx, rep, seed):
    n, clauses, _ = make_formula(seed)
    path = write_formula(ctx, n, clauses)
    truth = brute_status(ctx, path)
    deep = seed % 10 == 0
    v = certify(ctx, path, expected=truth, clauses=clauses, num_vars=n, reference=deep, drat_trim=deep,
                keep_proof=deep, timeout=60)
    rep.tally(v)
    if v.status == "UNKNOWN":
        raise Failure("solver timed out on a %d-variable formula: %s" % (n, path))
    if deep and v.status == "UNSAT":
        ok, _, _, _ = check_proof(ctx, path, v.proof_path, ignore_deletions=True)
        if not ok:
            raise Failure("proof rejected in additions-only mode: %s" % path)
        rep.count("proofs re-checked (python reference, strict deletions, additions-only" +
                  (", drat-trim)" if ctx.drat_trim else ")"))
    # baseline solver must agree
    rc, text, _, _ = run([ctx.dpll, "-q", path], timeout=60 * ctx.slow)
    if {10: "SAT", 20: "UNSAT"}.get(rc) != truth:
        raise Failure("baseline DPLL disagrees with brute force on %s (rc=%s)" % (path, rc))
    rep.count("baseline agreed")
    if seed % 7 == 0:
        opts = ABLATIONS[(seed // 7) % len(ABLATIONS)]
        va = certify(ctx, path, expected=truth, options=opts, clauses=clauses, num_vars=n, timeout=60)
        if va.status == "UNKNOWN":
            raise Failure("solver %s timed out on %s" % (opts, path))
        rep.count("ablation configurations verified")


def run_group(ctx):
    rep = Report("A. differential fuzz vs brute force (n <= 18)")
    total = 3000 if ctx.quick else 40000
    ctx.map(lambda s: rep.guard(one, ctx, rep, s), range(1, total + 1))
    return rep
