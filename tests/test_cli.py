"""G. Input format corner cases and command-line behaviour.

Three independently written DIMACS readers exist (solver, checkers, Python).
They must agree on every well-formed input and all reject every malformed one:
a checker that parsed a different formula than the solver would be certifying
the wrong thing.
"""
import os

import cnf
from common import Failure, Report, brute_status, certify, check_proof, run

VALID = [
    ("no variables, no clauses", "p cnf 0 0\n", "SAT"),
    ("variables but no clauses", "p cnf 3 0\n", "SAT"),
    ("only the empty clause", "p cnf 1 1\n0\n", "UNSAT"),
    ("empty clause among others", "p cnf 2 3\n1 2 0\n0\n-1 0\n", "UNSAT"),
    ("empty clause with zero variables", "p cnf 0 1\n0\n", "UNSAT"),
    ("only tautologies", "p cnf 2 2\n1 -1 0\n2 -2 1 0\n", "SAT"),
    ("comments everywhere", "c a\nc p cnf 9 9\np cnf 2 2\nc b 1 0\n1 2 0\nc 0\n-1 0\nc end", "SAT"),
    ("clause spanning lines", "p cnf 3 2\n1\n2\n3 0 -1\n-2 0\n", "SAT"),
    ("several clauses on one line", "p cnf 2 4\n1 2 0 -1 2 0 1 -2 0 -1 -2 0\n", "UNSAT"),
    ("CRLF line endings", "p cnf 2 2\r\n1 2 0\r\n-1 0\r\n", "SAT"),
    ("tabs and extra blanks", "p  cnf\t2   2\n\t 1\t2  0\n   -1 0   \n\n\n", "SAT"),
    ("no newline at end", "p cnf 1 1\n1 0", "SAT"),
    ("repeated literals", "p cnf 2 3\n1 1 1 0\n-1 -1 2 2 0\n-2 -2 -2 -1 0\n", "UNSAT"),
    ("contradictory units", "p cnf 1 2\n1 0\n-1 0\n", "UNSAT"),
    ("duplicate unit clauses", "p cnf 2 4\n1 0\n1 0\n-2 0\n-2 0\n", "SAT"),
    ("unused variables", "p cnf 20 2\n7 -13 0\n13 0\n", "SAT"),
    ("tautology hides a conflict-looking clause", "p cnf 2 3\n1 0\n-1 2 -2 0\n-1 2 0\n", "SAT"),
    ("comment token after clause on same line", "p cnf 2 2\n1 2 0 c trailing 5 0\n-1 0\n", "SAT"),
    ("explicit plus sign", "p cnf 2 2\n+1 +2 0\n-1 0\n", "SAT"),
    ("unit chain to conflict", "p cnf 3 4\n1 0\n-1 2 0\n-2 3 0\n-3 0\n", "UNSAT"),
    # x2 is forced at top level by (-1 2); that clause is then deleted as satisfied, and x2 is
    # needed again to shorten (-2 3 4).  The proof is only valid for a checker that honours
    # deletions strictly if x2 was logged as a unit lemma first.
    ("top-level literal outlives its reason clause", "p cnf 4 6\n1 0\n-1 2 0\n-2 3 4 0\n-3 4 0\n3 -4 0\n-3 -4 0\n", "UNSAT"),
]

INVALID = [
    ("missing header", "1 2 0\n"),
    ("empty file", ""),
    ("header without clause count", "p cnf 2\n"),
    ("header with a non-numeric count", "p cnf 2 one\n1 0\n"),
    ("wrong format name", "p sat 2 1\n1 2 0\n"),
    ("literal out of range", "p cnf 2 1\n1 3 0\n"),
    ("negative literal out of range", "p cnf 2 1\n-3 0\n"),
    ("unterminated last clause", "p cnf 2 2\n1 2 0\n-1\n"),
    ("fewer clauses than declared", "p cnf 2 3\n1 2 0\n-1 0\n"),
    ("more clauses than declared", "p cnf 2 1\n1 2 0\n-1 0\n"),
    ("garbage token", "p cnf 2 1\n1 x 0\n"),
    ("number glued to letters", "p cnf 2 1\n1 2a 0\n"),
    ("second header", "p cnf 2 1\np cnf 2 1\n1 0\n"),
    ("negative variable count", "p cnf -2 1\n1 0\n"),
    ("floating point literal", "p cnf 2 1\n1.5 0\n"),
]


def run_group(ctx):
    rep = Report("G. input format and command line")

    def valid(case):
        name, text, expected = case
        path = ctx.path(".cnf")
        with open(path, "w", newline="") as f:
            f.write(text)
        n, clauses = cnf.read_cnf(path)                      # Python reader accepts it
        if brute_status(ctx, path) != expected:              # checker-side reader + brute force
            raise Failure("brute force disagrees with the expected status of case %r" % name)
        v = certify(ctx, path, expected=expected, clauses=clauses, num_vars=n, reference=True)
        if v.status != expected:
            raise Failure("case %r: got %s" % (name, v.status))
        rc, _, _, _ = run([ctx.dpll, "-q", path], timeout=60 * ctx.slow)
        if {10: "SAT", 20: "UNSAT"}.get(rc) != expected:
            raise Failure("baseline disagrees on case %r" % name)
        rep.count("well-formed corner cases agreed by all readers")

    def invalid(case):
        name, text = case
        path = ctx.path(".cnf")
        with open(path, "w", newline="") as f:
            f.write(text)
        proof = ctx.path(".rup")
        open(proof, "w").write("0\n")
        outcomes = {
            "solver": run([ctx.solver, path], timeout=60 * ctx.slow)[0] == 1,
            "dpll": run([ctx.dpll, path], timeout=60 * ctx.slow)[0] == 1,
            "rup_check": run([ctx.rup_check, path, proof], timeout=60 * ctx.slow)[0] == 2,
            "model_check": run([ctx.model_check, path, proof], timeout=60 * ctx.slow)[0] == 2,
            "brute": run([ctx.brute, path], timeout=60 * ctx.slow)[0] == 2,
        }
        try:
            cnf.read_cnf(path)
            outcomes["python"] = False
        except cnf.FormatError:
            outcomes["python"] = True
        bad = [k for k, ok in outcomes.items() if not ok]
        if bad:
            raise Failure("malformed input %r was not rejected by: %s" % (name, ", ".join(bad)))
        rep.count("malformed inputs rejected by all readers")

    for case in VALID:
        rep.guard(valid, case)
    for case in INVALID:
        rep.guard(invalid, case)

    def misc():
        # unknown option / missing file -> error, never an answer
        if run([ctx.solver, "--no-such-option", "x.cnf"])[0] != 1:
            raise Failure("unknown option accepted")
        if run([ctx.solver, os.path.join(ctx.tmp, "does-not-exist.cnf")])[0] != 1:
            raise Failure("missing input file not reported")
        rep.count("command-line errors reported")
        # resource limit -> s UNKNOWN with exit code 0, never a guess
        import gen
        path = ctx.path(".cnf")
        n, clauses, _ = gen.pigeonhole(10, 9)
        cnf.write_cnf(path, n, clauses)
        rc, text, _, _ = run([ctx.solver, "--conflict-limit", "50", path], timeout=120 * ctx.slow)
        if rc != 0 or "s UNKNOWN" not in text:
            raise Failure("conflict limit did not produce s UNKNOWN (rc=%s)" % rc)
        rep.count("resource limit yields UNKNOWN")
        rc, text, _, _ = run([ctx.solver, "--time-limit", "0.2", path], timeout=120 * ctx.slow)
        if rc != 0 or "s UNKNOWN" not in text:
            raise Failure("time limit did not produce s UNKNOWN (rc=%s)" % rc)
        rep.count("resource limit yields UNKNOWN")
        big = ctx.path(".cnf")
        n, clauses, _ = gen.planted_ksat(20000, 80000, 3, 9)
        cnf.write_cnf(big, n, clauses)
        rc, text, _, _ = run([ctx.solver, "--mem-limit", "1", big], timeout=120 * ctx.slow)
        if rc != 0 or "s UNKNOWN" not in text:
            raise Failure("memory limit did not produce s UNKNOWN (rc=%s)" % rc)
        rep.count("resource limit yields UNKNOWN")
        # a proof file written during a SAT run must not verify
        path = ctx.path(".cnf")
        n, clauses, _ = gen.planted_ksat(60, 250, 3, 5)
        cnf.write_cnf(path, n, clauses)
        proof = ctx.path(".rup")
        rc, _, _, _ = run([ctx.solver, "-q", "--proof", proof, path], timeout=60 * ctx.slow)
        ok, _, _, _ = check_proof(ctx, path, proof)
        if rc != 10 or ok:
            raise Failure("proof stream of a satisfiable run was accepted as a refutation")
        rep.count("proof stream of a SAT run is not a refutation")

    rep.guard(misc)
    return rep
