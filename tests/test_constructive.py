"""B. Instances whose status is known by construction.

Here the oracle is not another program but the way the formula was built:

  SAT    a hidden assignment is drawn first and only clauses it satisfies are
         generated (so a solution exists no matter what any solver says);
  UNSAT  a satisfiable formula is combined with a small self-contradictory
         gadget: all 2^k sign patterns over k variables, an implication cycle
         x -> ... -> ¬x -> ... -> x, or a hidden pigeonhole core.
"""
import itertools
import random

import gen
from common import Failure, Report, certify, write_formula


def contradiction_gadget(rng, n, kind):
    """A set of clauses over variables 1..n that no assignment satisfies."""
    if kind == 0:
        k = rng.randint(1, 4)
        vs = rng.sample(range(1, n + 1), k)
        return [[v if s else -v for v, s in zip(vs, signs)] for signs in itertools.product([0, 1], repeat=k)]
    # x -> y1 -> ... -> ¬x  and  ¬x -> z1 -> ... -> x, through distinct variables
    length = rng.randint(1, min(30, (n - 1) // 2))
    vs = rng.sample(range(1, n + 1), 2 * length + 1)
    x, ys, zs = vs[0], vs[1:length + 1], vs[length + 1:]
    signed = lambda group: [v * rng.choice([1, -1]) for v in group]
    out = []
    for start, mids in ((x, signed(ys)), (-x, signed(zs))):
        path = [start] + mids + [-start]
        out += [[-a, b] for a, b in zip(path, path[1:])]
    return out


def run_group(ctx):
    rep = Report("B. status known by construction")
    sizes = [20, 50, 100, 200, 500, 1000] + ([] if ctx.quick else [2000, 5000, 20000])
    per_size = 6 if ctx.quick else 20
    jobs = []
    for n in sizes:
        for i in range(per_size):
            jobs.append((n, i))

    def one(job):
        n, i = job
        rng = random.Random(n * 1000 + i)
        k = rng.choice([3, 3, 4, 5])
        # Near the satisfiability threshold (about 4.27 / 9.9 / 21.1 clauses per
        # variable for k = 3 / 4 / 5) large instances are genuinely hard; those
        # belong to the "hard" tier of group C, not here.
        top = 1.0 if n <= 200 else 0.8 if n <= 1000 else 0.7
        ratio = {3: rng.uniform(2.0, 4.2 * top), 4: rng.uniform(4.0, 9.0 * top), 5: rng.uniform(6.0, 18.0 * top)}[k]
        nv, clauses, _ = gen.planted_ksat(n, int(n * ratio), k, n * 1000 + i)
        path = write_formula(ctx, nv, clauses, "SAT")
        v = certify(ctx, path, expected="SAT", clauses=clauses, num_vars=nv, timeout=300)
        if v.status == "UNKNOWN":
            rep.count("timeouts (planted SAT, n=%d)" % n)
        else:
            rep.count("planted SAT: model verified")
        # the same formula with a contradiction mixed in
        unsat = clauses + contradiction_gadget(rng, nv, i % 2)
        rng.shuffle(unsat)
        path = write_formula(ctx, nv, unsat, "UNSAT")
        v = certify(ctx, path, expected="UNSAT", clauses=unsat, num_vars=nv, timeout=300,
                    reference=nv <= 50)
        if v.status == "UNKNOWN":
            raise Failure("timeout on formula with an explicit contradiction: %s" % path)
        rep.count("planted + contradiction gadget: proof verified")

    ctx.map(lambda j: rep.guard(one, j), jobs)

    def core(seed):
        n = 300 if ctx.quick else 3000
        nv, clauses, _ = gen.with_core(n, int(3.5 * n), 3, 5, seed)
        path = write_formula(ctx, nv, clauses, "UNSAT")
        v = certify(ctx, path, expected="UNSAT", timeout=600)
        if v.status == "UNKNOWN":
            rep.count("timeouts (hidden pigeonhole core)")
        else:
            rep.count("hidden pigeonhole core: proof verified")

    ctx.map(lambda s: rep.guard(core, s), range(4 if ctx.quick else 12))
    return rep
