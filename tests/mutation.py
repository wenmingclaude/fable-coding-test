#!/usr/bin/env python3
"""Mutation testing: does the test suite notice when the code is wrong?

    tests/mutation.py [--only ID[,ID...]] [--out FILE.md] [--list]

Each mutant is one deliberate bug: a single textual replacement in a copy of
the sources (the replacement must match exactly once, so a mutant can never
silently stop applying).  The mutated program is compiled and the quick test
suite is run against it with --fail-fast.  A mutant is

    killed     the suite reports a failure (wrong answer, invalid model,
               invalid proof, checker disagreement, crash, hang);
    survived   every group passes.

Survivors are either bugs the tests cannot see (a gap worth closing) or
changes that do not alter any answer (equivalent mutants); each one has to be
examined by hand -- see docs/RESULTS.md for the verdicts.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLVER, HPP, MAIN = "src/solver.cpp", "src/solver.hpp", "src/main.cpp"
CHECK, MODEL = "checker/rup_check.cpp", "checker/model_check.cpp"

# (id, category, file, description, old text, new text)
MUTANTS = [
    # ---- wrong literal negation ------------------------------------------------
    ("neg-asserting", "wrong negation", SOLVER, "learnt clause gets the UIP literal with the wrong sign",
     "    learnt_[0] = neg(p);", "    learnt_[0] = p;"),
    ("neg-enqueue", "wrong negation", SOLVER, "assigning p does not make ¬p false",
     "    vals_[p ^ 1] = -1;", "    vals_[p ^ 1] = 1;"),
    ("neg-parse", "wrong negation", HPP, "negative input literals are read as positive",
     "mkLit(Var(-d - 1), true); }", "mkLit(Var(-d - 1), false); }"),
    ("neg-tautology", "wrong negation", SOLVER, "x ∨ ¬x is 'simplified' by dropping ¬x instead of the clause",
     "        if (j > 0 && lits[i] == neg(lits[j - 1])) return;    // tautology: always true, drop it",
     "        if (j > 0 && lits[i] == neg(lits[j - 1])) continue;"),
    ("neg-final", "wrong negation", SOLVER, "failed-assumption core is returned without negating back",
     "    for (Lit l : learnt_) core_.push_back(neg(l));", "    for (Lit l : learnt_) core_.push_back(l);"),
    # ---- missed propagation ----------------------------------------------------
    ("prop-lost-watch", "missed propagation", SOLVER, "a moved watch is not registered with the new literal",
     "                watches_[lits[1]].push_back(Watch{other, w.ref});\n", ""),
    ("prop-qhead", "missed propagation", SOLVER, "after backtracking the propagation queue is not rewound",
     "    trailSize_ = lim;\n    qhead_ = lim;", "    trailSize_ = lim;"),
    ("prop-skip-unit-long", "missed propagation", SOLVER, "a unit long clause does not force its last literal",
     "            enqueue(other, w.ref);\n        }", "        }"),
    ("prop-skip-unit-binary", "missed propagation", SOLVER, "a unit binary clause does not force its other literal",
     "                enqueue(w.blocker, w.ref & ~BIN_TAG);\n", ""),
    ("prop-learnt-watch", "missed propagation", SOLVER, "learnt clause does not watch its highest-level literal",
     "        std::swap(learnt_[1], learnt_[maxI]);\n", ""),
    ("prop-blocker", "missed propagation", SOLVER, "clause skipped when its blocker is merely assigned",
     "            if (useBlocker && bv > 0) {  // clause already satisfied",
     "            if (useBlocker && bv != 0) {"),
    # ---- empty clause / trivial cases ------------------------------------------
    ("empty-input", "empty clause", SOLVER, "an empty clause in the input is ignored",
     "    if (j == 0) {\n        refute();", "    if (j == 0) {\n        return;"),
    ("empty-unit-conflict", "empty clause", SOLVER, "contradictory unit clauses in the input are ignored",
     "        if (value(p) < 0) refute();\n        else if (value(p) == 0) enqueue(p, CREF_UNDEF);",
     "        if (value(p) == 0) enqueue(p, CREF_UNDEF);"),
    ("empty-proof", "empty clause", SOLVER, "the final empty clause is not written to the proof",
     "    proof_.add(nullptr, 0);  // the empty clause", ""),
    # ---- state restoration -----------------------------------------------------
    ("undo-value", "state restoration", SOLVER, "backtracking leaves the negated literal assigned",
     "        vals_[p] = 0;\n        vals_[p ^ 1] = 0;", "        vals_[p] = 0;"),
    ("undo-heap", "state restoration", SOLVER, "unassigned variables are not returned to the decision heap",
     "        if (heapPos_[v] < 0) heapInsert(v);\n", ""),
    ("undo-level", "state restoration", SOLVER, "backtracking keeps one decision level too many",
     "    trailLim_.resize(level);", "    trailLim_.resize(level + 1);"),
    ("undo-query", "state restoration", SOLVER, "assignments of one incremental query leak into the next",
     "        r = search();\n        cancelUntil(0);", "        r = search();"),
    ("undo-seen", "state restoration", SOLVER, "analysis marks of minimised-away literals are not cleared "
     "(the bug that was really made during development)",
     "    for (size_t i = 1; i < learnt_.size(); i++) toClear_.push_back(var(learnt_[i]));\n", ""),
    # ---- ignored conflicts -----------------------------------------------------
    ("confl-binary", "ignored conflict", SOLVER, "a falsified binary clause is not reported",
     "                if (bv < 0) {\n                    confl = w.ref & ~BIN_TAG;\n                    break;\n                }",
     "                if (bv < 0) continue;"),
    ("confl-long", "ignored conflict", SOLVER, "a falsified long clause is not reported",
     "            if (ov < 0) {\n                confl = w.ref;\n                break;\n            }\n", ""),
    ("confl-assumption", "ignored conflict", SOLVER, "an assumption that is already false is silently accepted",
     "                analyzeFinal(neg(a));\n                return Result::UNSAT_ASSUMPTIONS;",
     "                newDecisionLevel();"),
    # ---- conflict analysis / learning ------------------------------------------
    ("learn-uip", "wrong learning", SOLVER, "analysis stops one resolution step early",
     "        if (--open == 0) break;", "        if (--open <= 1) break;"),
    ("learn-backjump", "wrong learning", SOLVER, "always backjumps to level 0 while keeping the clause as reason",
     "        btLevel = level_[var(learnt_[1])];", "        btLevel = 0;"),
    ("learn-minimize", "wrong learning", SOLVER, "minimisation treats unexplored literals as redundant",
     "            if (level_[v] == 0 || seen_[v] == 1 || seen_[v] == 2) continue;",
     "            if (level_[v] == 0 || seen_[v] != 3) continue;"),
    ("learn-level0", "wrong learning", SOLVER, "literals of lower levels are dropped from the learnt clause",
     "            else learnt_.push_back(q);", "            else if (level_[v] + 1 >= curLevel) learnt_.push_back(q);"),
    ("learn-core", "wrong learning", SOLVER, "failed-assumption analysis keeps at most one further assumption",
     "                learnt_.push_back(neg(t));  // an assumption",
     "                if (learnt_.size() < 2) learnt_.push_back(neg(t));"),
    # ---- clause database -------------------------------------------------------
    ("db-locked", "clause database", SOLVER, "reduction may delete a clause that is the reason of an assignment",
     "        if (value(l0) > 0 && reason_[var(l0)] == r) continue;  // locked\n", ""),
    ("db-satisfied", "clause database", SOLVER, "simplification deletes clauses that merely contain a false literal",
     "                if (v > 0) {\n                    satisfied = true;", "                if (v != 0) {\n                    satisfied = true;"),
    ("db-strengthen", "clause database", SOLVER, "simplification drops an unassigned literal",
     "                if (v == 0) kept.push_back(l[k]);", "                if (v == 0 && k > 0) kept.push_back(l[k]);"),
    ("db-reloc", "clause database", SOLVER, "garbage collection does not update reason references",
     "            assert(arena_[r + 1] & F_RELOC);\n            r = arena_[r];", "            assert(arena_[r + 1] & F_RELOC);"),
    # ---- proof logging ---------------------------------------------------------
    ("proof-learnt", "proof logging", SOLVER, "learnt clauses are not written to the proof",
     "            proof_.add(learnt_.data(), uint32_t(learnt_.size()));  // logged before it is ever used\n", ""),
    ("proof-order", "proof logging", SOLVER, "a strengthened clause is logged after its parent is deleted",
     "                proof_.add(kept.data(), uint32_t(kept.size()));  // RUP: the dropped literals are false units\n"
     "                proof_.del(l, n);",
     "                proof_.del(l, n);\n                proof_.add(kept.data(), uint32_t(kept.size()));"),
    ("proof-units", "proof logging", SOLVER, "level-0 implied literals are not logged as unit lemmas",
     "            proof_.add(&p, 1);\n", ""),
    ("proof-core", "proof logging", SOLVER, "the clause justifying a failed-assumption core is not logged",
     "    proof_.add(learnt_.data(), uint32_t(learnt_.size()));\n    core_.clear();", "    core_.clear();"),
    # ---- output ----------------------------------------------------------------
    ("out-model", "output", MAIN, "the model is printed with every variable true",
     'snprintf(tmp, sizeof tmp, " %d", m[v] > 0 ? int(v + 1) : -int(v + 1));',
     'snprintf(tmp, sizeof tmp, " %d", int(v + 1));'),
    ("out-status", "output", MAIN, "resource limit is reported as UNSATISFIABLE",
     '            } else {\n                puts("s UNKNOWN");\n            }\n        } else {',
     '            } else {\n                puts("s UNSATISFIABLE");\n                exitCode = 20;\n            }\n        } else {'),
    # ---- proof checker accepts illegal steps -----------------------------------
    ("chk-always", "checker accepts illegal step", CHECK, "the RUP test always succeeds",
     "        qhead_ = top;\n        return conflict;", "        qhead_ = top;\n        return true;"),
    ("chk-no-undo", "checker accepts illegal step", CHECK, "temporary assumptions of a RUP test are not undone",
     "            val_[l] = val_[l ^ 1u] = 0;\n", ""),
    ("chk-unit-is-conflict", "checker accepts illegal step", CHECK, "a unit clause is mistaken for a falsified one",
     "                if (val_[other] < 0) {  // all literals false", "                if (val_[other] <= 0) {"),
    ("chk-assigned", "checker accepts illegal step", CHECK, "a lemma is accepted if any of its literals is assigned",
     "            if (val_[l] > 0) conflict = true;  // ¬l contradicts T directly",
     "            if (val_[l] != 0) conflict = true;"),
    ("chk-unit-refutes", "checker accepts illegal step", CHECK, "adding a unit clause counts as deriving the empty clause",
     "        if (unassigned == 0) {  // every literal is false under T (includes the empty clause)",
     "        if (unassigned <= 1) {"),
    ("chk-no-empty", "checker accepts illegal step", CHECK, "a proof that never derives the empty clause is accepted",
     '    if (checker.refuted()) {\n        puts("s VERIFIED");',
     '    if (checker.refuted() || failure[0] == \'p\') {\n        puts("s VERIFIED");'),
    ("chk-deleted", "checker accepts illegal step", CHECK, "deleted clauses keep taking part in propagation",
     "                if (c[0] & DELETED) continue;  // drop watchers of deleted clauses lazily\n", ""),
    ("chk-sign", "checker accepts illegal step", CHECK, "the checker reads negative literals as positive",
     "2u * uint32_t(-dimacsLit - 1) + 1u;", "2u * uint32_t(-dimacsLit - 1);"),
    ("chk-watch", "checker accepts illegal step", CHECK, "the checker forgets a moved watch (misses conflicts: too strict)",
     "                    watches_[lits[1]].push_back(Watch{w.ref, other});\n", ""),
    # ---- model checker ---------------------------------------------------------
    ("mdl-last-clause", "model checker", MODEL, "the last clause is not checked",
     "    for (size_t i = 0; i < f.numClauses(); i++) {\n        bool sat = false;",
     "    for (size_t i = 0; i + 1 < f.numClauses(); i++) {\n        bool sat = false;"),
    ("mdl-missing", "model checker", MODEL, "variables without a value are tolerated",
     '        if (value[size_t(v)] == 0) return reject("variable " + std::to_string(v) + " has no value");', "        ;"),
    ("mdl-sign", "model checker", MODEL, "negative literals are evaluated like positive ones",
     "return lit > 0 ? value[size_t(lit)] > 0 : value[size_t(-lit)] < 0;",
     "return lit > 0 ? value[size_t(lit)] > 0 : value[size_t(-lit)] > 0;"),
    ("mdl-status", "model checker", MODEL, "the status line is not required to say SATISFIABLE",
     '    if (!sawStatus || !satisfiable) return reject("output does not claim SATISFIABLE");', ""),
]

GROUPS = "cli,fuzz,checker,incremental,metamorphic,constructive,scale,families"
BUILD = {
    "sat_solver": ["src/main.cpp", "src/solver.cpp"],
    "rup_check": ["checker/rup_check.cpp"],
    "model_check": ["checker/model_check.cpp"],
}
TARGET = {SOLVER: "sat_solver", HPP: "sat_solver", MAIN: "sat_solver", CHECK: "rup_check", MODEL: "model_check"}


def build_variant(work, mutant):
    """Copies the sources, applies the mutant (or nothing), compiles the affected
    program.  Returns the bin directory, or raises on a mutant that no longer applies."""
    if os.path.exists(work):
        shutil.rmtree(work)
    os.makedirs(os.path.join(work, "bin"))
    for d in ("src", "checker", "tools", "baseline"):
        shutil.copytree(os.path.join(ROOT, d), os.path.join(work, d),
                        ignore=shutil.ignore_patterns("__pycache__", "*.py", "*.sh"))
    targets = set(BUILD) if mutant is None else {TARGET[mutant[2]]}
    if mutant is not None:
        _, _, path, _, old, new = mutant
        full = os.path.join(work, path)
        text = open(full).read()
        if text.count(old) != 1:
            raise RuntimeError("mutant %s: pattern occurs %d times in %s" % (mutant[0], text.count(old), path))
        open(full, "w").write(text.replace(old, new))
    for name in ("sat_solver", "rup_check", "model_check", "brute", "dpll"):
        dst = os.path.join(work, "bin", name)
        if name in targets:
            cmd = ["g++", "-std=c++20", "-O2", "-o", dst] + [os.path.join(work, s) for s in BUILD[name]]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError("mutant does not compile: %s" % r.stderr[-400:])
        else:
            os.symlink(os.path.join(ROOT, "build", name), dst)
    return os.path.join(work, "bin")


def run_suite(bin_dir):
    """Returns (passed, name of the failing group, first failure message, seconds)."""
    env = dict(os.environ, SAT_TEST_TIMEOUT_CAP="30")
    t = time.time()
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tests", "run_all.py"), "--quick", "--fail-fast",
                        "--bin-dir", bin_dir, "--only", GROUPS], capture_output=True, text=True, env=env)
    group, message = "", ""
    for line in r.stdout.splitlines():
        if line.startswith("== ") and "FAILED" in line:
            group = line[3:].split(":")[0]
        if "FAILURE:" in line and not message:
            message = line.split("FAILURE:", 1)[1].strip()
    return r.returncode == 0, group, message, time.time() - t


def classify(message):
    m = message.lower()
    for key, label in (("unsound", "checker accepted a bogus proof"), ("wrong answer", "wrong answer"),
                       ("invalid model", "invalid model"), ("invalid proof", "invalid proof"),
                       ("invalid core", "invalid core"), ("core", "invalid core"),
                       ("timed out", "hang / timeout"), ("timeout", "hang / timeout"),
                       ("rc=-", "crash"), ("crash", "crash"), ("disagree", "disagreement"),
                       ("hand-written", "hand-written case"), ("not rejected", "malformed input accepted"),
                       ("session proof", "invalid proof"), ("valid proof not accepted", "valid proof rejected"),
                       ("does not refute", "invalid proof"), ("exit code", "wrong exit code"),
                       ("status lines", "wrong output"), ("unknown", "wrong output"),
                       ("brute force failed", "tool failure")):
        if key in m:
            return label
    return "other failure"


def short(message, n=110):
    """First part of a failure message, without paths, for the Markdown table."""
    import re
    m = re.sub(r"/\S*/(f\d+\.\w+)", r"\1", message)
    m = m.replace("|", "/").replace("\n", " ")
    return m[:n] + ("…" if len(m) > n else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--out", help="write a Markdown table here")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    mutants = MUTANTS
    if args.only:
        mutants = [m for m in MUTANTS if m[0] in args.only.split(",")]
    if args.list:
        for m in mutants:
            print("%-22s %-30s %s" % (m[0], m[1], m[3]))
        return 0

    work = os.path.join(ROOT, "build", "mutants")
    print("control run: unmodified sources built the same way must pass the suite ...")
    ok, group, message, secs = run_suite(build_variant(os.path.join(work, "control"), None))
    print("  control: %s (%.0f s)" % ("passes" if ok else "FAILS in %s: %s" % (group, message), secs))
    if not ok:
        print("the suite fails without any mutation; mutation results would be meaningless")
        return 1

    rows = []
    for m in mutants:
        try:
            bin_dir = build_variant(os.path.join(work, m[0]), m)
        except RuntimeError as e:
            print("  %-22s NOT APPLIED: %s" % (m[0], e))
            rows.append((m, "not applied", "", str(e), 0.0))
            continue
        ok, group, message, secs = run_suite(bin_dir)
        verdict = "SURVIVED" if ok else "killed"
        rows.append((m, verdict, group, message, secs))
        print("  %-22s %-8s %-34s %s  [%.0f s]" % (m[0], verdict, classify(message) if not ok else "",
                                                  ("in " + group) if group else "", secs))
        sys.stdout.flush()
        shutil.rmtree(os.path.join(work, m[0]), ignore_errors=True)

    killed = sum(1 for r in rows if r[1] == "killed")
    survived = [r[0][0] for r in rows if r[1] == "SURVIVED"]
    print("\n%d mutants: %d killed, %d survived%s" % (len(rows), killed, len(survived),
                                                      (" (" + ", ".join(survived) + ")") if survived else ""))
    if args.out:
        with open(args.out, "w") as f:
            f.write("| mutant | category | injected bug | result | detected as | by test group | first failure reported |\n"
                    "|---|---|---|---|---|---|---|\n")
            for m, verdict, group, message, _ in rows:
                f.write("| `%s` | %s | %s | %s | %s | %s | %s |\n" % (
                    m[0], m[1], m[3], verdict, classify(message) if verdict == "killed" else "",
                    group.split(" (")[0], short(message) if verdict == "killed" else ""))
            f.write("\n%d mutants: %d killed, %d survived.\n" % (len(rows), killed, len(survived)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
