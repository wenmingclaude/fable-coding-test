"""DIMACS helpers for the Python-side tools and tests.

This is the third, independently written reader of the format (after
src/dimacs.hpp and checker/cnf.hpp); the tests compare all three.
"""
import re

_INT = re.compile(r"[+-]?\d+\Z")


class FormatError(Exception):
    pass


def read_cnf(path):
    """Returns (num_vars, clauses) with clauses as lists of non-zero ints."""
    with open(path, "r", errors="replace") as f:
        lines = [line.split() for line in f]
    # Flatten to tokens, dropping comments: a token starting with 'c' hides the
    # rest of its line -- except the word "cnf" directly after "p".
    tokens = []
    for parts in lines:
        for i, tok in enumerate(parts):
            if tok[0] == "c" and not (tok == "cnf" and i > 0 and parts[i - 1] == "p"):
                break
            tokens.append(tok)
    if len(tokens) < 4 or tokens[0] != "p" or tokens[1] != "cnf" or not all(_INT.match(t) for t in tokens[2:4]):
        raise FormatError("malformed 'p cnf <vars> <clauses>' header")
    num_vars, declared = int(tokens[2]), int(tokens[3])
    if num_vars < 0 or declared < 0:
        raise FormatError("malformed 'p cnf <vars> <clauses>' header")
    clauses = []
    cur = []
    for tok in tokens[4:]:
        if not _INT.match(tok):
            raise FormatError("unexpected token %r" % tok)
        x = int(tok)
        if x == 0:
            clauses.append(cur)
            cur = []
        elif abs(x) > num_vars:
            raise FormatError("literal exceeds declared variable count")
        else:
            cur.append(x)
    if cur:
        raise FormatError("last clause is not terminated by 0")
    if len(clauses) != declared:
        raise FormatError("clause count differs from header")
    return num_vars, clauses


def write_cnf(path, num_vars, clauses, comments=()):
    with open(path, "w") as f:
        for c in comments:
            f.write("c %s\n" % c)
        f.write("p cnf %d %d\n" % (num_vars, len(clauses)))
        out = []
        for cl in clauses:
            out.append(" ".join(map(str, cl)) + " 0\n" if cl else "0\n")
            if len(out) >= 50000:
                f.write("".join(out))
                out = []
        f.write("".join(out))


def parse_solver_output(text):
    """Returns a list of results, one per 's' line: dicts with keys
    status ('SAT' / 'UNSAT' / 'UNSAT_ASSUMPTIONS' / 'UNKNOWN'), model (list of
    ints or None) and core (list of ints or None)."""
    results = []
    cur = None
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "s":
            status = " ".join(parts[1:])
            cur = {
                "status": {
                    "SATISFIABLE": "SAT",
                    "UNSATISFIABLE": "UNSAT",
                    "UNSATISFIABLE UNDER ASSUMPTIONS": "UNSAT_ASSUMPTIONS",
                    "UNKNOWN": "UNKNOWN",
                }.get(status, "?" + status),
                "model": None,
                "core": None,
                "model_terminated": False,
            }
            results.append(cur)
        elif parts[0] == "v" and cur is not None:
            if cur["model"] is None:
                cur["model"] = []
            for t in parts[1:]:
                x = int(t)
                if x == 0:
                    cur["model_terminated"] = True
                else:
                    cur["model"].append(x)
        elif parts[0] == "a" and cur is not None:
            cur["core"] = [int(t) for t in parts[1:-1]]
    return results


def model_satisfies(num_vars, clauses, model, assumptions=()):
    """Direct substitution check, written independently of checker/model_check.cpp.
    Returns None if the model is a total satisfying assignment, else a reason."""
    value = {}
    for lit in model:
        v = abs(lit)
        if v < 1 or v > num_vars:
            return "value for unknown variable %d" % v
        if value.get(v, lit > 0) != (lit > 0):
            return "variable %d assigned both values" % v
        value[v] = lit > 0
    if len(value) != num_vars:
        return "model does not cover all variables"
    for i, cl in enumerate(clauses):
        if not any(value[abs(l)] == (l > 0) for l in cl):
            return "clause %d falsified" % (i + 1)
    for l in assumptions:
        if value[abs(l)] != (l > 0):
            return "assumption %d false" % l
    return None
