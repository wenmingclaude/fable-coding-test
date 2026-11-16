#!/usr/bin/env python3
"""Instance generators.

    gen.py <family> [args...] -o out.cnf        (run with no arguments for the list)

Every generator returns (num_vars, clauses, expected) where expected is
'SAT', 'UNSAT' or None (unknown a priori).  When the status is known *by
construction* it is recorded in the file as a comment "c expected SAT|UNSAT",
which the tests use as an oracle that is independent of every solver.
"""
import itertools
import random
import sys

# ----------------------------------------------------------------------------
# Random k-SAT
# ----------------------------------------------------------------------------


def random_ksat(n, m, k, seed):
    """Uniform random k-SAT: m clauses over k distinct variables, random signs."""
    rng = random.Random(seed)
    clauses = []
    for _ in range(m):
        vs = rng.sample(range(1, n + 1), k)
        clauses.append([v if rng.random() < 0.5 else -v for v in vs])
    return n, clauses, None


def planted_ksat(n, m, k, seed):
    """Random k-SAT with a hidden solution: a random assignment is drawn first
    and only clauses it satisfies are kept.  Satisfiable by construction."""
    rng = random.Random(seed)
    hidden = [None] + [rng.random() < 0.5 for _ in range(n)]
    clauses = []
    while len(clauses) < m:
        vs = rng.sample(range(1, n + 1), k)
        cl = [v if rng.random() < 0.5 else -v for v in vs]
        if any(hidden[abs(l)] == (l > 0) for l in cl):
            clauses.append(cl)
    return n, clauses, "SAT"


def fuzz(n, m, seed):
    """Small irregular formulas for differential testing: clause lengths 0..5,
    repeated literals, tautologies, duplicate clauses, unused variables."""
    rng = random.Random(seed)
    clauses = []
    used = max(1, n - rng.randint(0, 2)) if n else 0
    for _ in range(m):
        r = rng.random()
        if r < 0.02 or used == 0:
            length = 0
        elif r < 0.12:
            length = 1
        elif r < 0.40:
            length = 2
        elif r < 0.85:
            length = 3
        else:
            length = rng.randint(4, 5)
        cl = [rng.choice([1, -1]) * rng.randint(1, used) for _ in range(length)]
        if cl and rng.random() < 0.05:
            cl.append(cl[0])       # repeated literal
        if cl and rng.random() < 0.03:
            cl.append(-cl[0])      # tautology
        clauses.append(cl)
        if rng.random() < 0.05:
            clauses.append(list(cl))  # duplicate clause
    return n, clauses, None


# ----------------------------------------------------------------------------
# Pigeonhole principle
# ----------------------------------------------------------------------------


def pigeonhole(pigeons, holes):
    """p_{i,j}: pigeon i sits in hole j.  Every pigeon sits somewhere, no hole
    holds two pigeons.  Unsatisfiable iff pigeons > holes; every resolution
    refutation is exponential in the number of holes (Haken 1985)."""
    def var(i, j):
        return i * holes + j + 1
    clauses = [[var(i, j) for j in range(holes)] for i in range(pigeons)]
    for j in range(holes):
        for a, b in itertools.combinations(range(pigeons), 2):
            clauses.append([-var(a, j), -var(b, j)])
    return pigeons * holes, clauses, "UNSAT" if pigeons > holes else "SAT"


# ----------------------------------------------------------------------------
# Graph colouring
# ----------------------------------------------------------------------------


def _colouring_cnf(n, edges, k):
    def var(v, c):
        return v * k + c + 1
    clauses = [[var(v, c) for c in range(k)] for v in range(n)]
    for v in range(n):
        for a, b in itertools.combinations(range(k), 2):
            clauses.append([-var(v, a), -var(v, b)])
    for u, v in edges:
        for c in range(k):
            clauses.append([-var(u, c), -var(v, c)])
    return n * k, clauses


def colouring_planted(n, m, k, seed):
    """Random graph with m edges that respects a hidden k-colouring: satisfiable."""
    rng = random.Random(seed)
    colour = [rng.randrange(k) for _ in range(n)]
    edges = set()
    while len(edges) < m:
        u, v = rng.sample(range(n), 2)
        if colour[u] != colour[v]:
            edges.add((min(u, v), max(u, v)))
    nv, clauses = _colouring_cnf(n, sorted(edges), k)
    return nv, clauses, "SAT"


def colouring_clique(n, m, k, seed):
    """Random graph with m edges plus a hidden (k+1)-clique: not k-colourable."""
    rng = random.Random(seed)
    edges = set()
    while len(edges) < m:
        u, v = rng.sample(range(n), 2)
        edges.add((min(u, v), max(u, v)))
    clique = rng.sample(range(n), k + 1)
    for u, v in itertools.combinations(clique, 2):
        edges.add((min(u, v), max(u, v)))
    nv, clauses = _colouring_cnf(n, sorted(edges), k)
    return nv, clauses, "UNSAT"


def colouring_random(n, m, k, seed):
    """k-colouring of a uniformly random graph with m edges (status unknown)."""
    rng = random.Random(seed)
    edges = set()
    while len(edges) < m:
        u, v = rng.sample(range(n), 2)
        edges.add((min(u, v), max(u, v)))
    nv, clauses = _colouring_cnf(n, sorted(edges), k)
    return nv, clauses, None


# ----------------------------------------------------------------------------
# Parity (XOR) constraints
# ----------------------------------------------------------------------------


def xor_clauses(variables, rhs):
    """CNF of x_1 xor ... xor x_k = rhs: the 2^(k-1) clauses that each forbid
    one assignment of the wrong parity."""
    k = len(variables)
    out = []
    for signs in itertools.product([False, True], repeat=k):
        # the clause with literal polarity `signs` is falsified exactly by the
        # assignment x_i = not signs_i; forbid it if its parity is wrong
        ones = sum(1 for s in signs if not s)
        if ones % 2 != rhs:
            out.append([v if s else -v for v, s in zip(variables, signs)])
    return out


def xor_planted(n, m, k, seed):
    """m random k-variable parity constraints consistent with a hidden assignment."""
    rng = random.Random(seed)
    hidden = [None] + [rng.randrange(2) for _ in range(n)]
    clauses = []
    for _ in range(m):
        vs = rng.sample(range(1, n + 1), k)
        clauses += xor_clauses(vs, sum(hidden[v] for v in vs) % 2)
    return n, clauses, "SAT"


def _parity_chain(order, rhs, first_aux):
    """x_{o1} xor ... xor x_{ok} = rhs, cut into 3-variable XORs with auxiliary
    variables first_aux, first_aux+1, ...  Returns (clauses, next free variable)."""
    clauses = []
    aux = first_aux
    acc = order[0]
    for i in range(1, len(order)):
        if i == len(order) - 1:
            clauses += xor_clauses([acc, order[i]], rhs)
        else:
            clauses += xor_clauses([acc, order[i], aux], 0)  # aux = acc xor x
            acc = aux
            aux += 1
    return clauses, aux


def parity_contradiction(n, seed, shuffle):
    """The parity of x_1..x_n is asserted to be 0 and to be 1, each through its
    own chain of auxiliary variables.  Unsatisfiable.  With shuffle=False both
    chains use the same variable order (easy for clause learning, exponential
    for plain DPLL); with shuffle=True the second chain uses a random order,
    which is known to be hard for resolution-based solvers already for small n."""
    rng = random.Random(seed)
    order = list(range(1, n + 1))
    c1, nxt = _parity_chain(order, 0, n + 1)
    if shuffle:
        rng.shuffle(order)
    c2, nxt = _parity_chain(order, 1, nxt)
    return nxt - 1, c1 + c2, "UNSAT"


def tseitin_grid(w, h, seed):
    """Tseitin formula on a w x h torus grid: one variable per edge, each vertex
    requires the XOR of its four edges to equal its charge.  Summing all
    constraints counts every edge twice, so an odd total charge is impossible."""
    rng = random.Random(seed)
    def hedge(x, y):  # edge from (x,y) to (x+1,y)
        return 1 + 2 * (y * w + x)
    def vedge(x, y):  # edge from (x,y) to (x,y+1)
        return 2 + 2 * (y * w + x)
    charges = [rng.randrange(2) for _ in range(w * h)]
    if sum(charges) % 2 == 0:
        charges[0] ^= 1
    clauses = []
    for y in range(h):
        for x in range(w):
            es = [hedge(x, y), hedge((x - 1) % w, y), vedge(x, y), vedge(x, (y - 1) % h)]
            clauses += xor_clauses(es, charges[y * w + x])
    return 2 * w * h, clauses, "UNSAT"


# ----------------------------------------------------------------------------
# Propagation chains, ordering principle, duplicates
# ----------------------------------------------------------------------------


def chain(n, unsat, seed):
    """x_1, x_1 -> x_2, ..., x_{n-1} -> x_n (shuffled); with unsat also ¬x_n.
    Solved by unit propagation alone; tests long propagation chains."""
    rng = random.Random(seed)
    clauses = [[1]] + [[-i, i + 1] for i in range(1, n)]
    if unsat:
        clauses.append([-n])
    rng.shuffle(clauses)
    return n, clauses, "UNSAT" if unsat else "SAT"


def ladder(n, unsat):
    """Two implication chains a_1 -> ... -> a_n and b_1 -> ... -> b_n with
    (a_1 ∨ b_1); with unsat additionally (¬a_n ∨ ¬b_n is replaced by) ¬a_n, ¬b_n.
    No unit clause at the start: one decision triggers a propagation of length n."""
    a = lambda i: i
    b = lambda i: n + i
    clauses = [[a(1), b(1)]]
    for i in range(1, n):
        clauses.append([-a(i), a(i + 1)])
        clauses.append([-b(i), b(i + 1)])
    if unsat:
        clauses.append([-a(n), -b(n)])
        clauses.append([-a(1), b(n)])   # a_1 -> b_n
        clauses.append([-b(1), a(n)])   # b_1 -> a_n
    return 2 * n, clauses, "UNSAT" if unsat else "SAT"


def ordering(n):
    """Ordering principle: a strict total order on n elements with no minimum.
    Unsatisfiable; has short resolution refutations that clause learning finds,
    but tree-like search (plain DPLL) needs exponential time."""
    idx = {}
    for i in range(n):
        for j in range(n):
            if i != j:
                idx[(i, j)] = len(idx) + 1   # x_{i,j}: i < j
    clauses = []
    for i in range(n):
        for j in range(i + 1, n):
            clauses.append([idx[(i, j)], idx[(j, i)]])        # total
            clauses.append([-idx[(i, j)], -idx[(j, i)]])      # antisymmetric
    for i, j, k in itertools.permutations(range(n), 3):
        clauses.append([-idx[(i, j)], -idx[(j, k)], idx[(i, k)]])  # transitive
    for j in range(n):
        clauses.append([idx[(i, j)] for i in range(n) if i != j])  # j is not minimal
    return len(idx), clauses, "UNSAT"


def duplicates(n, m, copies, seed):
    """A planted 3-SAT formula in which every clause is repeated `copies` times
    with shuffled literals, some repeated literals and interleaved tautologies."""
    rng = random.Random(seed)
    _, base, _ = planted_ksat(n, m, 3, seed)
    clauses = []
    for cl in base:
        for _ in range(copies):
            c = list(cl)
            if rng.random() < 0.3:
                c.append(rng.choice(c))
            rng.shuffle(c)
            clauses.append(c)
        if rng.random() < 0.2:
            v = rng.randint(1, n)
            clauses.append([v, -v, rng.randint(1, n)])
    rng.shuffle(clauses)
    return n, clauses, "SAT"


def with_core(n, m, k, holes, seed):
    """A large satisfiable planted k-SAT formula on variables 1..n plus a small
    pigeonhole formula on fresh variables: unsatisfiable, but only because of a
    tiny part of the input.  Tests that the solver does not drown in the rest."""
    _, clauses, _ = planted_ksat(n, m, k, seed)
    nv, php, _ = pigeonhole(holes + 1, holes)
    clauses += [[(l + n if l > 0 else l - n) for l in cl] for cl in php]
    random.Random(seed).shuffle(clauses)
    return n + nv, clauses, "UNSAT"


# ----------------------------------------------------------------------------
# Command line
# ----------------------------------------------------------------------------

FAMILIES = {
    "random": (random_ksat, "n m k seed", [int, int, int, int]),
    "planted": (planted_ksat, "n m k seed", [int, int, int, int]),
    "fuzz": (fuzz, "n m seed", [int, int, int]),
    "php": (pigeonhole, "pigeons holes", [int, int]),
    "color-planted": (colouring_planted, "vertices edges colours seed", [int, int, int, int]),
    "color-clique": (colouring_clique, "vertices edges colours seed", [int, int, int, int]),
    "color-random": (colouring_random, "vertices edges colours seed", [int, int, int, int]),
    "xor-planted": (xor_planted, "n constraints k seed", [int, int, int, int]),
    "parity": (parity_contradiction, "n seed shuffle(0|1)", [int, int, int]),
    "tseitin": (tseitin_grid, "width height seed", [int, int, int]),
    "chain": (chain, "n unsat(0|1) seed", [int, int, int]),
    "ladder": (ladder, "n unsat(0|1)", [int, int]),
    "ordering": (ordering, "n", [int]),
    "duplicates": (duplicates, "n m copies seed", [int, int, int, int]),
    "with-core": (with_core, "n m k holes seed", [int, int, int, int, int]),
}


def generate(family, *args):
    return FAMILIES[family][0](*args)


def main(argv):
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from cnf import write_cnf
    if len(argv) < 2 or argv[1] not in FAMILIES:
        print(__doc__)
        for name, (_, usage, _) in FAMILIES.items():
            print("  %-14s %s" % (name, usage))
        return 2
    fn, usage, types = FAMILIES[argv[1]]
    args = argv[2:]
    out = None
    if "-o" in args:
        i = args.index("-o")
        out = args[i + 1]
        args = args[:i] + args[i + 2:]
    if len(args) != len(types) or out is None:
        print("usage: gen.py %s %s -o out.cnf" % (argv[1], usage))
        return 2
    n, clauses, expected = fn(*[t(a) for t, a in zip(types, args)])
    comments = ["family %s %s" % (argv[1], " ".join(args))]
    if expected:
        comments.append("expected %s" % expected)
    write_cnf(out, n, clauses, comments)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
