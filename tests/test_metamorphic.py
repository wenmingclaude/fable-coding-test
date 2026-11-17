"""D. Metamorphic testing.

Transformations that cannot change satisfiability are applied to a formula;
the solver must give the same answer for every variant, each time with a
certificate that checks against *that variant*:

  rename      permute the variables and flip the polarity of some of them
  clauses     shuffle the clause order
  literals    shuffle the literal order inside every clause
  duplicates  repeat a random subset of the clauses (and some literals)
  tautologies insert clauses containing x and ¬x
  unused      declare additional variables that occur nowhere
  all         everything at once

Base formulas: small random ones (truth known from brute force) and medium
instances of every family (truth known by construction, or taken from the
first run and required to stay the same).
"""
import random

import gen
from common import Failure, Report, brute_status, certify, write_formula


def t_rename(n, clauses, rng):
    perm = list(range(1, n + 1))
    rng.shuffle(perm)
    flip = [rng.random() < 0.5 for _ in range(n + 1)]
    def m(l):
        v = abs(l)
        return perm[v - 1] * (1 if (l > 0) != flip[v] else -1)
    return n, [[m(l) for l in c] for c in clauses]


def t_clauses(n, clauses, rng):
    out = [list(c) for c in clauses]
    rng.shuffle(out)
    return n, out


def t_literals(n, clauses, rng):
    out = [list(c) for c in clauses]
    for c in out:
        rng.shuffle(c)
    return n, out


def t_duplicates(n, clauses, rng):
    out = [list(c) for c in clauses]
    for c in clauses:
        if rng.random() < 0.4:
            d = list(c)
            if d and rng.random() < 0.5:
                d.append(rng.choice(d))
            out.insert(rng.randint(0, len(out)), d)
    return n, out


def t_tautologies(n, clauses, rng):
    out = [list(c) for c in clauses]
    if n == 0:
        return n, out
    for _ in range(max(1, len(clauses) // 5)):
        v = rng.randint(1, n)
        t = [v, -v] + [rng.choice([1, -1]) * rng.randint(1, n) for _ in range(rng.randint(0, 3))]
        rng.shuffle(t)
        out.insert(rng.randint(0, len(out)), t)
    return n, out


def t_unused(n, clauses, rng):
    return n + rng.randint(1, 40), [list(c) for c in clauses]


def t_all(n, clauses, rng):
    for t in (t_rename, t_duplicates, t_tautologies, t_unused, t_literals, t_clauses):
        n, clauses = t(n, clauses, rng)
    return n, clauses


TRANSFORMS = [("rename", t_rename), ("clauses", t_clauses), ("literals", t_literals), ("duplicates", t_duplicates),
              ("tautologies", t_tautologies), ("unused", t_unused), ("all", t_all)]

MEDIUM = [
    ("php", (7, 6)), ("php", (6, 6)), ("color-planted", (120, 260, 3, 1)), ("color-clique", (80, 150, 3, 2)),
    ("random", (120, 511, 3, 3)), ("random", (120, 511, 3, 4)), ("random", (140, 596, 3, 5)),
    ("planted", (1500, 5250, 3, 6)), ("xor-planted", (30, 30, 3, 7)), ("parity", (40, 8, 0)),
    ("parity", (16, 9, 1)), ("tseitin", (3, 4, 10)), ("ordering", (14,)), ("chain", (3000, 1, 11)),
    ("ladder", (2000, 1)), ("ladder", (2000, 0)), ("duplicates", (150, 500, 5, 12)), ("with-core", (200, 700, 3, 4, 13)),
]


def run_group(ctx):
    rep = Report("D. metamorphic transformations")

    def variants(n, clauses, truth, seed, timeout):
        rng = random.Random(seed)
        for name, t in TRANSFORMS:
            n2, c2 = t(n, clauses, rng)
            path = write_formula(ctx, n2, c2)
            v = certify(ctx, path, expected=truth, clauses=c2, num_vars=n2, timeout=timeout)
            if v.status != truth:
                raise Failure("variant %r changed the answer from %s to %s: %s" % (name, truth, v.status, path))
            rep.count("variant '%s' agreed" % name)

    def small(seed):
        rng = random.Random(seed)
        n = rng.randint(3, 16)
        if seed % 2:
            n, clauses, _ = gen.random_ksat(n, int(n * rng.uniform(3.6, 4.8)), 3, seed)
        else:
            n, clauses, _ = gen.fuzz(n, rng.randint(1, 5 * n), seed)
        truth = brute_status(ctx, write_formula(ctx, n, clauses))
        variants(n, clauses, truth, seed, 60)
        rep.count("small base formulas (truth from brute force)")

    def medium(item):
        family, args = item
        n, clauses, expected = gen.generate(family, *args)
        base = certify(ctx, write_formula(ctx, n, clauses), expected=expected, timeout=300)
        if base.status == "UNKNOWN":
            raise Failure("base instance %s%r timed out" % (family, args))
        variants(n, clauses, base.status, hash(args) & 0xFFFF, 300)
        rep.count("medium base formulas (truth by construction / first run)")

    ctx.map(lambda s: rep.guard(small, s), range(1, (150 if ctx.quick else 1500) + 1))
    ctx.map(lambda i: rep.guard(medium, i), MEDIUM)
    return rep
