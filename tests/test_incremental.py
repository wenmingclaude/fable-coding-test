"""F. Incremental solving under assumptions.

One solver process answers a list of queries on the same formula F; each
query is a set of assumption literals A.  Checked for every query:

  SAT                 the model satisfies F and every literal of A;
  UNSAT under A       the reported core C is a subset of A, and F ∧ C is
                      unsatisfiable -- established independently by
                        (1) brute force (small F), or a fresh certified solver
                            run on F plus the unit clauses C (larger F), and
                        (2) the session proof: rup_check --assume C must
                            derive the empty clause from F ∧ C;
  UNSAT (F refuted)   the session proof alone refutes F (no --assume);
  the answer equals what a fresh, non-incremental computation gives;
  the answers do not depend on which queries were asked before (the query
  list is replayed in reverse order and with every query repeated).
"""
import random

import cnf
import gen
from common import Failure, Report, brute_status, certify, check_proof, run, write_formula


def ask(ctx, formula, queries, extra=()):
    """Runs one incremental session; returns (results, proof_path)."""
    qfile = ctx.path(".queries")
    with open(qfile, "w") as f:
        for q in queries:
            f.write(" ".join(map(str, q)) + " 0\n")
    proof = ctx.path(".rup")
    rc, text, _, _ = run([ctx.solver, "-q", "--queries", qfile, "--proof", proof, *extra, formula],
                         timeout=600 * ctx.slow)
    if rc != 0:
        raise Failure("incremental session failed (rc=%s) on %s %s: %s" % (rc, formula, qfile, text[-300:]))
    results = cnf.parse_solver_output(text)
    if len(results) != len(queries):
        raise Failure("%d answers for %d queries: %s %s" % (len(results), len(queries), formula, qfile))
    return results, proof


def verify_answers(ctx, rep, formula, n, clauses, queries, results, proof, truth_of):
    """truth_of(assumptions) -> 'SAT' / 'UNSAT', computed without the incremental solver."""
    for q, res in zip(queries, results):
        truth = truth_of(q)
        where = "%s, assumptions %s" % (formula, q)
        if res["status"] == "SAT":
            if truth != "SAT":
                raise Failure("WRONG ANSWER (SAT, should be UNSAT): " + where)
            why = cnf.model_satisfies(n, clauses, res["model"] or [], q) if res["model_terminated"] else "no model"
            if why:
                raise Failure("INVALID MODEL under assumptions (%s): %s" % (why, where))
            rep.count("SAT answers: model satisfies formula and assumptions")
        elif res["status"] == "UNSAT_ASSUMPTIONS":
            if truth != "UNSAT":
                raise Failure("WRONG ANSWER (UNSAT under assumptions, should be SAT): " + where)
            core = res["core"]
            if core is None or not set(core) <= set(q):
                raise Failure("core %s is not a subset of the assumptions: %s" % (core, where))
            if truth_of(core) != "UNSAT":
                raise Failure("INVALID CORE: formula ∧ %s is satisfiable: %s" % (core, where))
            ok, msg, _, _ = check_proof(ctx, formula, proof, assume=core)
            if not ok:
                raise Failure("session proof does not refute formula ∧ core %s: %s: %s" % (core, where, msg[-200:]))
            rep.count("UNSAT-under-assumptions answers: core verified (independent solve + proof)")
            rep.count("core literals", len(core))
            rep.count("assumption literals in those queries", len(set(q)))
        elif res["status"] == "UNSAT":
            if truth_of([]) != "UNSAT":
                raise Failure("WRONG ANSWER (formula reported refuted, but it is satisfiable): " + where)
            ok, msg, _, _ = check_proof(ctx, formula, proof)
            if not ok:
                raise Failure("session proof does not refute the formula: %s: %s" % (where, msg[-200:]))
            rep.count("formula-refuted answers: proof verified without assumptions")
        else:
            raise Failure("unexpected status %s: %s" % (res["status"], where))


def run_group(ctx):
    rep = Report("F. incremental solving under assumptions")

    def small(seed):
        rng = random.Random(seed)
        n = rng.randint(2, 14)
        if seed % 3 == 0:
            n, clauses, _ = gen.fuzz(n, rng.randint(1, 4 * n), seed)
        else:
            n, clauses, _ = gen.random_ksat(max(n, 3), int(max(n, 3) * rng.uniform(2.5, 4.6)), 3, seed)
        formula = write_formula(ctx, n, clauses)
        queries = []
        for _ in range(rng.randint(4, 12)):
            q = [rng.choice([1, -1]) * rng.randint(1, n) for _ in range(rng.randint(0, 6))]
            if q and rng.random() < 0.15:
                q.append(q[0])           # repeated assumption
            if q and rng.random() < 0.1:
                q.append(-q[0])          # contradictory assumptions
            queries.append(q)
        queries.append([])
        cache = {}
        def truth_of(assume):
            key = tuple(sorted(set(assume)))
            if key not in cache:
                cache[key] = brute_status(ctx, formula, list(key))
            return cache[key]
        opts = [(), ("--no-minimize",), ("--restart=luby",), ("--no-inline-binary", "--no-blocker")][seed % 4]
        results, proof = ask(ctx, formula, queries, opts)
        verify_answers(ctx, rep, formula, n, clauses, queries, results, proof, truth_of)
        # history independence: reversed order, every query asked twice
        replay = [q for q in reversed(queries) for _ in (0, 1)]
        results2, proof2 = ask(ctx, formula, replay, opts)
        verify_answers(ctx, rep, formula, n, clauses, replay, results2, proof2, truth_of)
        rep.count("sessions (small formulas, truth from brute force)")
        rep.count("queries", len(queries) + len(replay))

    def medium(item):
        family, args, nq, seed = item
        rng = random.Random(seed)
        n, clauses, _ = gen.generate(family, *args)
        formula = write_formula(ctx, n, clauses)
        queries = [[rng.choice([1, -1]) * rng.randint(1, n) for _ in range(rng.choice([1, 2, 3, 5, 8, 20]))]
                   for _ in range(nq)] + [[]]
        cache = {}
        def truth_of(assume):
            key = tuple(sorted(set(assume)))
            if key not in cache:
                # fresh, non-incremental, fully certified run on F plus unit clauses
                path = write_formula(ctx, n, clauses + [[l] for l in key])
                v = certify(ctx, path, timeout=300)
                if v.status == "UNKNOWN":
                    raise Failure("fresh run timed out: %s" % path)
                cache[key] = v.status
                rep.count("fresh certified runs used as oracle")
            return cache[key]
        results, proof = ask(ctx, formula, queries)
        verify_answers(ctx, rep, formula, n, clauses, queries, results, proof, truth_of)
        rep.count("sessions (medium formulas, truth from fresh certified runs)")
        rep.count("queries", len(queries))

    ctx.map(lambda s: rep.guard(small, s), range(1, (150 if ctx.quick else 1500) + 1))
    medium_items = [
        ("planted", (300, 1200, 3, 1), 12, 1), ("planted", (2000, 7000, 3, 2), 12, 2),
        ("random", (120, 500, 3, 3), 12, 3), ("random", (150, 630, 3, 4), 10, 4),
        ("color-planted", (100, 220, 3, 5), 12, 5), ("php", (7, 7), 12, 6), ("php", (7, 6), 6, 7),
        ("xor-planted", (30, 28, 3, 8), 12, 8), ("ladder", (500, 0), 12, 9), ("chain", (2000, 0, 10), 8, 10),
        ("color-clique", (60, 100, 3, 11), 6, 11), ("duplicates", (100, 380, 4, 12), 10, 12),
    ]
    ctx.map(lambda it: rep.guard(medium, it), medium_items[:6] if ctx.quick else medium_items)
    return rep
