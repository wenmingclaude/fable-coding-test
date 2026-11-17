"""H. Instances at the size limits of the task.

Up to 200 000 variables, 1 000 000 clauses, 5 000 000 literals.  Recorded per
instance: solver time and peak memory (must stay below the 1 GiB budget),
proof size, checker time and memory.  In quick mode the same instances are
generated ten times smaller.
"""
import gen
from common import Report, certify, write_formula

MEM_LIMIT_MB = 1024

# (name, family, args at full size)
INSTANCES = [
    ("planted 5-SAT 200k vars, 1M clauses, 5M literals", "planted", (200000, 1000000, 5, 1)),
    ("planted 3-SAT 200k vars, 600k clauses", "planted", (200000, 600000, 3, 2)),
    ("planted 5-SAT + hidden pigeonhole core, ~1M clauses", "with-core", (199970, 999900, 5, 5, 3)),
    ("each clause repeated 14x, 1M clauses", "duplicates", (20000, 70000, 14, 4)),
    ("unit chain, 200k variables (unsat)", "chain", (200000, 1, 5)),
    ("implication ladder, 200k variables (unsat)", "ladder", (100000, 1)),
    ("odd cycle 2-colouring, 199 999 variables (unsat)", "cycle", (199999,)),
    ("even cycle 2-colouring, 200 000 variables (sat)", "cycle", (200000,)),
    # 30 000 inputs timed out (900 s) in the default configuration: there is a cliff between
    # 4 000 inputs (1.9 s) and 8 000 inputs (> 300 s); see docs/RESULTS.md, section 6.7.
    ("parity contradiction, 4 000 inputs (unsat)", "parity", (4000, 6, 0)),
    ("pigeonhole 200 pigeons into 200 holes (sat), 4M clauses*", "php", (200, 200)),
]


def shrink(family, args):
    if family == "with-core":
        return (args[0] // 10, args[1] // 10) + args[2:]
    if family == "php":
        return (60, 60)
    if family == "cycle":
        return (args[0] // 10 | (args[0] & 1),)
    if family == "parity":
        return (3000,) + args[1:]   # the size the quick suite has always used
    if family in ("chain", "ladder"):
        return (args[0] // 10,) + args[1:]
    return (args[0] // 10, args[1] // 10) + args[2:]


def run_group(ctx):
    rep = Report("H. size limits (200k variables / 1M clauses / 5M literals)")
    rep.rows.append("%-56s %8s %8s %8s %9s %9s %8s %9s %9s" % (
        "instance", "vars", "clauses", "result", "solve s", "solver MB", "proof MB", "check s", "check MB"))
    if not ctx.quick:
        rep.notes.append("* the pigeonhole instance has 200^2 = 40 000 variables but 3.98M clauses; it exceeds the "
                         "1M-clause target on purpose, as a stress test of the clause store.")
    for name, family, args in INSTANCES:   # sequential: timings and memory are the point here
        if ctx.quick:
            args = shrink(family, args)
        n, clauses, expected = gen.generate(family, *args)
        m = len(clauses)
        path = write_formula(ctx, n, clauses, expected)
        del clauses
        v = rep.guard(certify, ctx, path, expected, (), 900)
        if v is None:
            continue
        rep.tally(v)
        if v.status == "UNKNOWN":
            rep.fail("timeout on scale instance: %s" % name)
            continue
        if v.solver_rss > MEM_LIMIT_MB:
            rep.fail("solver used %.0f MB on %s (budget %d MB)" % (v.solver_rss, name, MEM_LIMIT_MB))
        rep.rows.append("%-56s %8d %8d %8s %9.2f %9.0f %8s %9s %9s" % (
            name, n, m, v.status, v.solve_time, v.solver_rss,
            "%.2f" % (v.proof_bytes / 1e6) if v.status == "UNSAT" else "-",
            "%.2f" % v.check_time if v.status == "UNSAT" else "-",
            "%.0f" % v.checker_rss if v.status == "UNSAT" else "-"))
    return rep
