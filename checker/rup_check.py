#!/usr/bin/env python3
"""Reference proof checker: the specification of checker/rup_check.cpp, executable.

    rup_check.py [--ignore-deletions] [--assume "l1 l2 ..."] [--explain] formula.cnf proof

It is written for obviousness, not speed: unit propagation is done by
rescanning the whole clause list until nothing changes.  The fast C++ checker
must give the same verdict on every proof (tests/test_checker.py compares
them on thousands of valid and corrupted proofs).

State (same as the C++ checker):
    db  list of clauses, each implied by the input formula
    T   dict var -> bool, literals implied by the input formula

With --explain every accepted lemma is printed together with the chain of
unit propagations that justifies it, so a proof can be read by a human.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from cnf import read_cnf  # noqa: E402


def normalise(clause):
    """Sorted tuple without duplicate literals; None for a tautology."""
    s = set(clause)
    if any(-l in s for l in s):
        return None
    return tuple(sorted(s, key=lambda l: (abs(l), l < 0)))


class Checker:
    def __init__(self, clauses):
        self.db = []        # clauses as tuples; the original ones first
        self.origin = []    # label of each db entry, for --explain
        self.T = {}
        self.refuted = False
        self.top_trace = []
        for i, c in enumerate(clauses):
            c = normalise(c)
            if c is not None:
                self.db.append(c)
                self.origin.append("F%d" % (i + 1))
        self._close()

    def _propagate(self, assign, trace):
        """Extends `assign` by unit propagation.  Returns the falsified clause
        index, or None if a fixpoint is reached without conflict."""
        changed = True
        while changed:
            changed = False
            for idx, c in enumerate(self.db):
                free = None
                nfree = 0
                satisfied = False
                for l in c:
                    v = assign.get(abs(l))
                    if v is None:
                        nfree += 1
                        free = l
                    elif v == (l > 0):
                        satisfied = True
                        break
                if satisfied:
                    continue
                if nfree == 0:
                    return idx
                if nfree == 1:
                    assign[abs(free)] = free > 0
                    trace.append((free, idx))
                    changed = True
        return None

    def _close(self):
        """Extend T by unit propagation; a conflict here is the empty clause."""
        trace = []
        confl = self._propagate(self.T, trace)
        self.top_trace = trace
        if confl is not None:
            self.refuted = True
            self.final_conflict = confl

    def add(self, clause, label):
        """RUP-check and add.  Returns (accepted, explanation)."""
        c = normalise(clause)
        if c is None:
            return True, ["tautology, trivially true (not stored)"]
        assign = dict(self.T)
        why = []
        confl = None
        direct = [l for l in c if assign.get(abs(l)) == (l > 0)]
        if direct:
            why.append("contains %d, which is already known to be true" % direct[0])
        else:
            for l in c:
                assign.setdefault(abs(l), not (l > 0))
            trace = []
            confl = self._propagate(assign, trace)
            if confl is None:
                return False, ["assuming %s and propagating reaches no conflict" % fmt_assume(c)]
            if c:
                why.append("assume %s" % fmt_assume(c))
            for lit, idx in trace:
                why.append("  %-4s forces %d" % (self.origin[idx] + ":", lit) + "   " + fmt(self.db[idx]))
            why.append("  %-4s is falsified" % (self.origin[confl] + ":") + "    " + fmt(self.db[confl]))
        self.db.append(c)
        self.origin.append(label)
        self._close()
        return True, why

    def delete(self, clause):
        c = normalise(clause)
        if c is not None and c in self.db:
            i = self.db.index(c)
            del self.db[i]
            del self.origin[i]
            return True
        return False


def fmt(c):
    return "(" + " ∨ ".join(str(l) for l in c) + ")" if c else "⊥ (empty clause)"


def fmt_assume(c):
    return ", ".join(str(-l) for l in c)


def read_proof(path):
    """Yields (is_delete, literals, line_number); raises ValueError if malformed."""
    with open(path) as f:
        toks = []
        for no, line in enumerate(f, 1):
            for t in line.split():
                toks.append((t, no))
    i = 0
    while i < len(toks):
        line = toks[i][1]
        delete = toks[i][0] == "d"
        if delete:
            i += 1
        lits = []
        while True:
            if i >= len(toks):
                raise ValueError("line %d: clause not terminated by 0" % line)
            t = toks[i][0]
            i += 1
            try:
                x = int(t)
            except ValueError:
                raise ValueError("line %d: malformed proof line" % line)
            if not t.lstrip("+-").isdigit():
                raise ValueError("line %d: malformed proof line" % line)
            if x == 0:
                break
            lits.append(x)
        yield delete, lits, line


def check(num_vars, clauses, proof_path, assume=(), ignore_deletions=False, explain=None):
    """Returns (verified, message)."""
    ck = Checker(list(clauses) + [[l] for l in assume])
    if explain is not None and ck.top_trace:
        explain("Units implied by the formula itself: " + " ".join(str(l) for l, _ in ck.top_trace))
    lemmas = 0
    steps = read_proof(proof_path)
    try:
        while not ck.refuted:      # nothing after the refutation is read
            step = next(steps, None)
            if step is None:
                break
            delete, lits, line = step
            if any(abs(l) > num_vars for l in lits):
                return False, "line %d: malformed proof line" % line
            if delete:
                if not ignore_deletions:
                    ck.delete(lits)
                continue
            lemmas += 1
            label = "L%d" % lemmas
            ok, why = ck.add(lits, label)
            if explain is not None:
                explain("")
                explain("%s  %s" % (label, fmt(normalise(lits) or tuple(lits))))
                for w in why:
                    explain("    " + w)
            if not ok:
                return False, "line %d: lemma %d is not implied by unit propagation (RUP check failed)" % (line, lemmas)
    except ValueError as e:
        return False, str(e)
    if not ck.refuted:
        return False, "proof ends without deriving the empty clause"
    return True, "%d lemmas checked" % lemmas


def main(argv):
    ignore = explain = False
    assume = []
    files = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--ignore-deletions":
            ignore = True
        elif a == "--explain":
            explain = True
        elif a == "--assume" and i + 1 < len(argv):
            i += 1
            assume += [int(t) for t in argv[i].split()]
        elif a.startswith("--"):
            files = []
            break
        else:
            files.append(a)
        i += 1
    if len(files) != 2:
        sys.stderr.write(__doc__)
        return 2
    num_vars, clauses = read_cnf(files[0])
    ok, msg = check(num_vars, clauses, files[1], assume, ignore, print if explain else None)
    print("c " + msg)
    print("s VERIFIED" if ok else "s NOT VERIFIED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
