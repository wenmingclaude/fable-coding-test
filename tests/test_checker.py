"""E. Testing the proof checker itself.

A checker that accepts everything would make every other test in this suite
pass.  So the checker is attacked directly:

  1. hand-written proofs with a known verdict (valid, non-RUP steps, missing
     empty clause, use of a deleted clause, malformed text, ...);
  2. valid solver proofs, randomly corrupted: the fast C++ checker and the
     naive Python reference must return the same verdict on every one, with
     and without deletions;
  3. soundness probes: for a *satisfiable* formula no proof whatsoever may be
     accepted.  The probes are near-misses: a correct refutation of F checked
     against F minus one clause, the lemma stream of a real run with "0"
     appended, proofs of a different formula, random clauses.
"""
import random

import cnf
import gen
from common import Failure, Report, brute_status, check_model, check_proof, check_proof_reference, run, write_formula

# (description, formula text, proof text, verified?, verified with --ignore-deletions?)
HAND = [
    ("valid two-step refutation",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "2 0\n0\n", True, True),
    ("empty clause not explicitly written, but derived",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "2 0\n", True, True),
    ("empty clause claimed too early",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "0\n", False, False),
    ("no proof at all",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "", False, False),
    ("proof without the final step",
     "p cnf 3 6\n1 2 0\n-1 2 0\n1 -2 3 0\n-1 -2 3 0\n1 -2 -3 0\n-1 -2 -3 0\n", "2 0\n", False, False),
    ("implied lemma that unit propagation alone cannot see",
     # F entails (1 ∨ 2) only through a case split on 3 and 4: not RUP
     "p cnf 4 4\n1 3 4 0\n1 -3 4 0\n2 3 -4 0\n2 -3 -4 0\n", "1 2 0\n", False, False),
    ("lemma that is not implied by the formula",
     "p cnf 2 2\n1 2 0\n-1 -2 0\n", "1 0\n-2 0\n0\n", False, False),
    ("lemma equal to the negation of the formula's only model",
     "p cnf 2 3\n1 0\n-1 2 0\n1 2 0\n", "-1 -2 0\n0\n", False, False),
    ("formula refuted by unit propagation alone, empty proof",
     "p cnf 2 3\n1 0\n-1 2 0\n-2 0\n", "", True, True),
    ("formula containing the empty clause, empty proof",
     "p cnf 2 2\n1 2 0\n0\n", "", True, True),
    ("satisfiable formula, proof is just the empty clause",
     "p cnf 2 2\n1 2 0\n-1 0\n", "0\n", False, False),
    ("deleted clause is used afterwards",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "d 1 2 0\n2 0\n0\n", False, True),
    ("deleted clause, different literal order and repeated literal",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "d 2 1 2 0\n2 0\n0\n", False, True),
    ("deleting a clause that does not exist is ignored",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "d 1 0\nd -1 -2 1 0\n2 0\n0\n", True, True),
    ("only one copy of a duplicated clause is deleted",
     "p cnf 2 5\n1 2 0\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "d 1 2 0\n2 0\n0\n", True, True),
    ("both copies deleted",
     "p cnf 2 5\n1 2 0\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "d 1 2 0\nd 2 1 0\n2 0\n0\n", False, True),
    ("units stay known after their reason clause is deleted (documented semantics)",
     # T = {1, 2}; the proof deletes the clause that produced 2 and still relies on 2
     "p cnf 4 6\n1 0\n-1 2 0\n-2 3 4 0\n-2 3 -4 0\n-2 -3 4 0\n-2 -3 -4 0\n", "d -1 2 0\n3 0\n0\n", True, True),
    ("tautological lemma is accepted but proves nothing",
     "p cnf 2 2\n1 2 0\n-1 -2 0\n", "1 -1 0\n0\n", False, False),
    ("lemma with repeated literals",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "2 2 2 0\n0\n", True, True),
    ("lemma subsumed by a known unit",
     "p cnf 3 5\n1 0\n2 3 0\n-2 3 0\n2 -3 0\n-2 -3 0\n", "1 2 3 0\n3 0\n0\n", True, True),
    ("unterminated line before the refutation is complete",
     "p cnf 3 6\n1 2 0\n-1 2 0\n1 -2 3 0\n-1 -2 3 0\n1 -2 -3 0\n-1 -2 -3 0\n", "2 0\n3", False, False),
    ("same proof, properly terminated",
     "p cnf 3 6\n1 2 0\n-1 2 0\n1 -2 3 0\n-1 -2 3 0\n1 -2 -3 0\n-1 -2 -3 0\n", "2 0\n3 0\n0\n", True, True),
    ("non-numeric token",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "2 x 0\n0\n", False, False),
    ("literal outside the formula's variables",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "2 3 0\n2 0\n0\n", False, False),
    ("anything after the refutation is complete is not read",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "2 0\nd\nnonsense", True, True),
    ("'d' applies to the next clause, line breaks are not significant",
     "p cnf 2 4\n1 2 0\n-1 2 0\n1 -2 0\n-1 -2 0\n", "d\n2 0\n0\n", False, False),
]


# The SAT certificate checker gets the same treatment.
# (description, formula text, solver output text, accepted?)
MODEL_FORMULA = "p cnf 4 3\n1 2 0\n-1 3 0\n-2 -3 0\n"      # variable 4 occurs nowhere
MODELS = [
    ("correct model", "s SATISFIABLE\nv 1 -2 3 -4 0\n", True),
    ("correct model split over lines, with comments", "c hi\ns SATISFIABLE\nv 1\nc mid\nv -2\nv 3 4 0\n", True),
    ("other correct model", "s SATISFIABLE\nv -1 2 -3 4 0\n", True),
    ("first clause falsified", "s SATISFIABLE\nv -1 -2 3 4 0\n", False),
    ("middle clause falsified", "s SATISFIABLE\nv 1 -2 -3 4 0\n", False),
    ("last clause falsified", "s SATISFIABLE\nv 1 2 3 4 0\n", False),
    ("used variable without a value", "s SATISFIABLE\nv 1 3 4 0\n", False),
    ("unused variable without a value", "s SATISFIABLE\nv 1 -2 3 0\n", False),
    ("variable with both values", "s SATISFIABLE\nv 1 -2 3 2 4 0\n", False),
    ("unused variable with both values", "s SATISFIABLE\nv 1 -2 3 4 -4 0\n", False),
    ("same literal twice is harmless", "s SATISFIABLE\nv 1 1 -2 3 4 0\n", True),
    ("value list not terminated", "s SATISFIABLE\nv 1 -2 3 4\n", False),
    ("unknown variable", "s SATISFIABLE\nv 1 -2 3 4 5 0\n", False),
    ("status is UNSATISFIABLE", "s UNSATISFIABLE\nv 1 -2 3 4 0\n", False),
    ("status is UNKNOWN", "s UNKNOWN\nv 1 -2 3 4 0\n", False),
    ("no status line", "v 1 -2 3 4 0\n", False),
    ("two status lines", "s SATISFIABLE\ns SATISFIABLE\nv 1 -2 3 4 0\n", False),
    ("values hidden in a comment", "s SATISFIABLE\nc v 1 -2 3 4 0\n", False),
    ("garbage value", "s SATISFIABLE\nv 1 -2 x 4 0\n", False),
    ("empty output", "", False),
]


def mutate(lines, n, rng):
    """One random corruption of a proof given as a list of text lines."""
    lines = list(lines)
    if not lines:
        return ["%d 0" % rng.choice([1, -1])]
    i = rng.randrange(len(lines))
    toks = lines[i].split()
    kind = rng.randrange(9)
    if kind == 0:
        del lines[i]                                           # drop a step
    elif kind == 1 and len(toks) > 1:
        j = rng.randrange(len(toks) - 1)                       # flip a literal
        if toks[j] != "d":
            toks[j] = str(-int(toks[j]))
        lines[i] = " ".join(toks)
    elif kind == 2 and len(toks) > 2:
        j = rng.randrange(len(toks) - 1)                       # drop a literal (strengthen)
        if toks[j] != "d":
            del toks[j]
        lines[i] = " ".join(toks)
    elif kind == 3:
        toks.insert(len(toks) - 1, str(rng.choice([1, -1]) * rng.randint(1, max(1, n))))   # add a literal
        lines[i] = " ".join(toks)
    elif kind == 4:
        j = rng.randrange(len(lines))                          # swap two steps
        lines[i], lines[j] = lines[j], lines[i]
    elif kind == 5:
        size = rng.randint(0, 3)                               # insert a random clause
        cl = [str(rng.choice([1, -1]) * rng.randint(1, max(1, n))) for _ in range(size)]
        lines.insert(i, " ".join(cl + ["0"]))
    elif kind == 6:
        lines[i] = lines[i][2:] if toks[0] == "d" else "d " + lines[i]   # addition <-> deletion
    elif kind == 7:
        lines = lines[:i]                                      # truncate
    else:
        lines = lines[i:]                                      # drop the beginning
    return lines


def both(ctx, formula, proof_lines, rep, what):
    """Verdicts of the C++ checker and the reference, in both deletion modes;
    raises if they differ.  Returns the default-mode verdict."""
    proof = ctx.path(".rup")
    with open(proof, "w") as f:
        f.write("\n".join(proof_lines) + ("\n" if proof_lines else ""))
    verdicts = []
    for ignore in (False, True):
        fast, _, _, _ = check_proof(ctx, formula, proof, ignore_deletions=ignore)
        ref = check_proof_reference(formula, proof, ignore_deletions=ignore)
        if fast != ref:
            raise Failure("checkers disagree (%s, ignore_deletions=%s): C++ says %s, reference says %s: %s %s"
                          % (what, ignore, fast, ref, formula, proof))
        verdicts.append(fast)
        rep.count("verdicts compared between C++ checker and Python reference")
    if verdicts[0] and not verdicts[1]:
        raise Failure("proof accepted with deletions but rejected without: %s %s" % (formula, proof))
    return verdicts[0]


def run_group(ctx):
    rep = Report("E. proof checker under attack")

    def hand(case):
        name, formula_text, proof_text, want, want_ignore = case
        formula = ctx.path(".cnf")
        open(formula, "w").write(formula_text)
        proof = ctx.path(".rup")
        open(proof, "w").write(proof_text)
        for ignore, expected in ((False, want), (True, want_ignore)):
            fast, out, _, _ = check_proof(ctx, formula, proof, ignore_deletions=ignore)
            ref = check_proof_reference(formula, proof, ignore_deletions=ignore)
            if fast != expected or ref != expected:
                raise Failure("hand-written case %r (ignore_deletions=%s): expected %s, C++ %s, reference %s"
                              % (name, ignore, expected, fast, ref))
        rep.count("hand-written proofs with the expected verdict")

    for case in HAND:
        rep.guard(hand, case)

    def model_case(case):
        name, output_text, want = case
        formula = ctx.path(".cnf")
        open(formula, "w").write(MODEL_FORMULA)
        out = ctx.path(".out")
        open(out, "w").write(output_text)
        ok, _ = check_model(ctx, formula, out)
        if ok != want:
            raise Failure("model_check case %r: expected %s, got %s" % (name, want, ok))
        # the Python substitution check must agree whenever a model is present
        try:
            res = cnf.parse_solver_output(output_text)
        except ValueError:   # not even numbers: nothing to substitute
            res = []
        if len(res) == 1 and res[0]["status"] == "SAT" and res[0]["model_terminated"]:
            n, clauses = cnf.read_cnf(formula)
            try:
                py = cnf.model_satisfies(n, clauses, res[0]["model"]) is None
            except Exception:  # noqa: BLE001
                py = False
            if py != want:
                raise Failure("python model check disagrees on case %r" % name)
        ok2, _ = check_model(ctx, formula, out, assume=[-2])
        if ok2 != (want and "-2" in output_text.split()):
            raise Failure("model_check --assume gives the wrong verdict on case %r" % name)
        rep.count("hand-written models with the expected verdict")

    for case in MODELS:
        rep.guard(model_case, case)

    def corrupted(seed):
        rng = random.Random(seed)
        # a small unsatisfiable formula and the solver's own proof of it
        for attempt in range(50):
            n = rng.randint(4, 14)
            n, clauses, _ = gen.random_ksat(n, int(n * rng.uniform(4.3, 6.0)), 3, seed * 100 + attempt)
            formula = write_formula(ctx, n, clauses)
            if brute_status(ctx, formula) == "UNSAT":
                break
        else:
            return
        proof = ctx.path(".rup")
        opts = [[], ["--no-minimize"], ["--restart=luby"], ["--no-simplify"]][seed % 4]
        rc, _, _, _ = run([ctx.solver, "-q", "--proof", proof, *opts, formula], timeout=60 * ctx.slow)
        lines = open(proof).read().splitlines()
        if rc != 20 or not both(ctx, formula, lines, rep, "original proof"):
            raise Failure("valid proof not accepted: %s" % formula)
        rep.count("valid proofs accepted by both checkers")
        for _ in range(12):
            bad = lines
            for _ in range(rng.randint(1, 3)):
                bad = mutate(bad, n, rng)
            accepted = both(ctx, formula, bad, rep, "corrupted proof")
            rep.count("corrupted proofs still valid (accepted by both)" if accepted
                      else "corrupted proofs rejected by both")

        # Soundness probe 1: shrink the formula to a minimal unsatisfiable core
        # (dropping any one clause makes it satisfiable), prove the core, and
        # offer that correct proof for each formula with one clause missing.
        core = list(clauses)
        i = 0
        while i < len(core):
            trial = core[:i] + core[i + 1:]
            if brute_status(ctx, write_formula(ctx, n, trial)) == "UNSAT":
                core = trial
            else:
                i += 1
        f_core = write_formula(ctx, n, core)
        p_core = ctx.path(".rup")
        if run([ctx.solver, "-q", "--proof", p_core, f_core], timeout=60 * ctx.slow)[0] != 20:
            raise Failure("minimal core not reported unsatisfiable: %s" % f_core)
        core_lines = open(p_core).read().splitlines()
        if not both(ctx, f_core, core_lines, rep, "proof of minimal core"):
            raise Failure("valid proof of minimal core not accepted: %s" % f_core)
        for k in rng.sample(range(len(core)), min(8, len(core))):
            f2 = write_formula(ctx, n, core[:k] + core[k + 1:])
            if both(ctx, f2, core_lines, rep, "proof against formula minus one clause"):
                raise Failure("UNSOUND: refutation accepted for a satisfiable formula: %s %s" % (f2, p_core))
            rep.count("near-miss proofs (correct proof, one clause removed from the formula) rejected")

    def sat_probe(seed):
        rng = random.Random(seed)
        n = rng.randint(3, 14)
        n, clauses, _ = gen.planted_ksat(n, int(n * rng.uniform(2.0, 4.5)), 3, seed)
        formula = write_formula(ctx, n, clauses)
        proof = ctx.path(".rup")
        rc, _, _, _ = run([ctx.solver, "-q", "--proof", proof, formula], timeout=60 * ctx.slow)
        if rc != 10:
            raise Failure("planted formula not reported satisfiable: %s" % formula)
        stream = open(proof).read().splitlines()
        candidates = [stream + ["0"], ["0"], []]
        # the refutation of an unrelated unsatisfiable formula over the same variables
        _, other, _ = gen.random_ksat(n, 8 * n, 3, seed + 7)
        f2 = write_formula(ctx, n, other)
        p2 = ctx.path(".rup")
        if run([ctx.solver, "-q", "--proof", p2, f2], timeout=60 * ctx.slow)[0] == 20:
            candidates.append(open(p2).read().splitlines())
        for _ in range(6):
            rnd = [" ".join([str(rng.choice([1, -1]) * rng.randint(1, n)) for _ in range(rng.randint(1, 3))] + ["0"])
                   for _ in range(rng.randint(1, 12))]
            candidates.append(rnd + ["0"])
        for cand in candidates:
            if both(ctx, formula, cand, rep, "proof for satisfiable formula"):
                raise Failure("UNSOUND: proof accepted for a satisfiable formula: %s" % formula)
            rep.count("bogus proofs for satisfiable formulas rejected")

    ctx.map(lambda s: rep.guard(corrupted, s), range(1, (60 if ctx.quick else 500) + 1))
    ctx.map(lambda s: rep.guard(sat_probe, s), range(1, (60 if ctx.quick else 500) + 1))
    return rep
