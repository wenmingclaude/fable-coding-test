"""C. Classical instance families.

Each instance is labelled *easy* or *hard*:

  easy  must be solved within the time limit; a timeout is a test failure;
  hard  may time out (reported, not a failure) -- hardness is a property of
        the family (e.g. pigeonhole formulas have no short resolution proofs),
        not a defect.

Correctness is never judged by running time: whatever answer is given must
come with a certificate that passes the independent checkers, and must match
the status known by construction where there is one.
"""
import gen
from common import Report, certify, write_formula

# (name, family, args, tier)
INSTANCES = [
    # pigeonhole: n+1 pigeons, n holes
    ("php 5 into 4", "php", (5, 4), "easy"),
    ("php 7 into 6", "php", (7, 6), "easy"),
    ("php 8 into 7", "php", (8, 7), "easy"),
    ("php 9 into 8", "php", (9, 8), "hard"),
    ("php 10 into 9", "php", (10, 9), "hard"),
    ("php 9 into 9 (sat)", "php", (9, 9), "easy"),
    ("php 40 into 40 (sat)", "php", (40, 40), "easy"),
    # graph colouring
    ("3-colouring planted n=200 m=440", "color-planted", (200, 440, 3, 1), "easy"),
    ("3-colouring planted n=600 m=1200", "color-planted", (600, 1200, 3, 2), "easy"),
    ("3-colouring planted n=600 m=1380 (near threshold)", "color-planted", (600, 1380, 3, 2), "hard"),
    ("4-colouring planted n=300 m=1100", "color-planted", (300, 1100, 4, 3), "easy"),
    ("4-colouring planted n=300 m=1300 (near threshold)", "color-planted", (300, 1300, 4, 3), "hard"),
    ("3-colouring with 4-clique n=200", "color-clique", (200, 300, 3, 4), "easy"),
    ("5-colouring with 6-clique n=150", "color-clique", (150, 500, 5, 5), "easy"),
    ("3-colouring random n=150 m=345", "color-random", (150, 345, 3, 6), "easy"),
    ("3-colouring random n=250 m=590", "color-random", (250, 590, 3, 7), "hard"),
    # random 3-SAT at the threshold (ratio 4.26)
    ("random 3-SAT n=100 r=4.26 #1", "random", (100, 426, 3, 11), "easy"),
    ("random 3-SAT n=100 r=4.26 #2", "random", (100, 426, 3, 12), "easy"),
    ("random 3-SAT n=150 r=4.26 #1", "random", (150, 639, 3, 13), "easy"),
    ("random 3-SAT n=150 r=4.26 #2", "random", (150, 639, 3, 14), "easy"),
    ("random 3-SAT n=200 r=4.26 #1", "random", (200, 852, 3, 15), "easy"),
    ("random 3-SAT n=200 r=4.26 #2", "random", (200, 852, 3, 16), "easy"),
    ("random 3-SAT n=250 r=4.26", "random", (250, 1065, 3, 17), "hard"),
    ("random 3-SAT n=300 r=4.26", "random", (300, 1278, 3, 18), "hard"),
    ("random 3-SAT n=200 r=6.0 (overconstrained)", "random", (200, 1200, 3, 19), "easy"),
    # hidden solutions
    ("planted 3-SAT n=2000 r=4.0", "planted", (2000, 8000, 3, 21), "easy"),
    ("planted 3-SAT n=5000 r=3.7", "planted", (5000, 18500, 3, 22), "easy"),
    ("planted 3-SAT n=20000 r=3.5", "planted", (20000, 70000, 3, 23), "easy"),
    ("planted 3-SAT n=5000 r=5.0", "planted", (5000, 25000, 3, 22), "hard"),
    ("planted 3-SAT n=20000 r=6.0", "planted", (20000, 120000, 3, 23), "hard"),
    ("planted 3-SAT n=400 r=4.25", "planted", (400, 1700, 3, 24), "hard"),
    ("planted 5-SAT n=1000 r=18", "planted", (1000, 18000, 5, 25), "hard"),
    # long propagation chains
    ("unit chain n=200000 (sat)", "chain", (200000, 0, 1), "easy"),
    ("unit chain n=200000 (unsat)", "chain", (200000, 1, 2), "easy"),
    ("ladder n=100000 (sat)", "ladder", (100000, 0), "easy"),
    ("ladder n=100000 (unsat)", "ladder", (100000, 1), "easy"),
    # heavy duplication
    ("duplicates n=300, each clause x20", "duplicates", (300, 1100, 20, 31), "easy"),
    ("duplicates n=2000, each clause x50", "duplicates", (2000, 7000, 50, 32), "easy"),
    # parity
    ("xor planted n=40 m=40 k=3", "xor-planted", (40, 40, 3, 41), "easy"),
    ("xor planted n=60 m=60 k=3", "xor-planted", (60, 60, 3, 42), "hard"),
    ("parity contradiction n=60, same order", "parity", (60, 43, 0), "easy"),
    ("parity contradiction n=2000, same order", "parity", (2000, 44, 0), "easy"),
    ("parity contradiction n=24, shuffled", "parity", (24, 45, 1), "easy"),
    ("parity contradiction n=40, shuffled", "parity", (40, 46, 1), "hard"),
    ("tseitin torus 4x4", "tseitin", (4, 4, 47), "easy"),
    ("tseitin torus 5x5", "tseitin", (5, 5, 48), "hard"),
    # ordering principle
    ("ordering principle n=20", "ordering", (20,), "easy"),
    ("ordering principle n=30", "ordering", (30,), "hard"),
    ("ordering principle n=50", "ordering", (50,), "hard"),
]


def run_group(ctx):
    rep = Report("C. classical families (easy / hard)")
    limit = 30 if ctx.quick else 120
    rep.rows.append("%-50s %-5s %-8s %-9s %9s %8s %9s %8s" % (
        "instance", "tier", "expected", "result", "conflicts", "solve s", "proof MB", "check s"))

    def one(inst):
        name, family, args, tier = inst
        n, clauses, expected = gen.generate(family, *args)
        path = write_formula(ctx, n, clauses, expected)
        v = rep.guard(certify, ctx, path, expected, (), limit)
        if v is None:
            return "%-50s %-5s %-8s FAILED" % (name, tier, expected or "?")
        if v.status == "UNKNOWN":
            rep.count("timeouts (hard tier)" if tier == "hard" else "timeouts (easy tier)")
            if tier == "easy":
                rep.fail("easy instance timed out after %d s: %s" % (limit, name))
            return "%-50s %-5s %-8s %-9s %9s %8s" % (name, tier, expected or "?", "timeout", "-", ">%d" % limit)
        rep.tally(v)
        if expected:
            rep.count("answers matching the status known by construction")
        return "%-50s %-5s %-8s %-9s %9d %8.2f %9s %8s" % (
            name, tier, expected or "?", v.status, v.stats.get("conflicts", 0), v.solve_time,
            "%.2f" % (v.proof_bytes / 1e6) if v.status == "UNSAT" else "-",
            "%.2f" % v.check_time if v.status == "UNSAT" else "-")

    instances = INSTANCES
    if ctx.slow > 1:
        # Sanitizer builds are an order of magnitude slower; the hard tier would only produce timeouts.
        instances = [i for i in INSTANCES if i[3] == "easy"]
        rep.notes.append("sanitizer build: hard tier skipped")
    rep.rows += ctx.map(one, instances)
    return rep
