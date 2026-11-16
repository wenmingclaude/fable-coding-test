#!/usr/bin/env python3
"""Mutation testing: do the tests notice a broken editor?

Each mutant is the real source with one small, plausible bug planted in it
(a textual replacement that must match exactly once).  For every mutant the
three test programs are built and run:

    unit   tests/unit.cpp
    fuzz   tests/fuzz.cpp  (white-box: invariants + frozen-pool audit)
    diff   tests/diff.sh   (black-box: real binary vs. naive verifier)

A mutant is "killed" by a test that fails, crashes or hangs.  The run fails
if any mutant survives all three.  Mutants that only waste time or memory
without changing any answer are deliberately not in the list: they are not
bugs these tests are meant to find.

    tests/mutation.py [-j JOBS] [NAME_SUBSTRING]
"""
import concurrent.futures
import os
import resource
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ED = "src/editor.hpp"
HS = "src/hash.hpp"

# (name, file, original text, replacement)
MUTANTS = [
    # --- orientation: the "reversal" machinery -------------------------------
    ("flipped node shows unflipped children", ED,
     "View{{flip(n.right), rsize}, {flip(n.left), n.lsize}, n.key}",
     "View{{n.right, rsize}, {n.left, n.lsize}, n.key}"),
    ("flipped node does not swap children", ED,
     "View{{flip(n.right), rsize}, {flip(n.left), n.lsize}, n.key}",
     "View{{flip(n.left), n.lsize}, {flip(n.right), rsize}, n.key}"),
    ("flipped node reports unreversed hash", ED,
     "const Hash& forward(Ref t) const { return (t & 1) ? pool_[t >> 1].b : pool_[t >> 1].f; }",
     "const Hash& forward(Ref t) const { return pool_[t >> 1].f; }"),
    ("summary of flipped node not swapped", ED,
     "return (t.ref & 1) ? Summary{t.size, n.b, n.f} : Summary{t.size, n.f, n.b};",
     "return Summary{t.size, n.f, n.b};"),
    ("reverse forgets to flip", ED,
     "return publish(merge(merge(a, flip(mid)), b));",
     "return publish(merge(merge(a, mid), b));"),
    ("reverse skips ranges of length 2", ED,
     "if (r - l < 2) return publish(t);",
     "if (r - l < 3) return publish(t);"),
    # --- persistence: writing to shared nodes --------------------------------
    ("with() rewrites frozen nodes in place", ED,
     "if (!in_nursery(t.ref)) return make(left, key(t.ref), right);",
     "if (t.ref < 2) return make(left, key(t.ref), right);"),
    ("reversal done by swapping a shared node's children", ED,
     "return publish(merge(merge(a, flip(mid)), b));",
     "{ Node& n = pool_[mid.ref >> 1]; std::swap(n.left, n.right); n.lsize = mid.size - 1 - n.lsize; }\n"
     "        return publish(merge(merge(a, mid), b));"),
    ("nursery boundary off by one", ED,
     "bool in_nursery(Ref t) const { return (t >> 1) >= mark_; }",
     "bool in_nursery(Ref t) const { return (t >> 1) > mark_; }"),
    ("in-place rewrite ignores orientation", ED,
     "        if (t.ref & 1) {\n            n.left = flip(right.ref);",
     "        if (false) {\n            n.left = flip(right.ref);"),
    ("in-place rewrite of flipped node keeps stale left size", ED,
     "            n.right = flip(left.ref);\n            n.lsize = right.size;",
     "            n.right = flip(left.ref);"),
    ("seal skips right subtree", ED,
     "        if (in_nursery(r.ref)) seal(r);\n",
     ""),
    # --- index arithmetic ----------------------------------------------------
    ("split boundary: <= becomes <", ED,
     "        if (k <= v.left.size) {\n            auto [a, b] = split(v.left, k);",
     "        if (k < v.left.size) {\n            auto [a, b] = split(v.left, k);"),
    ("split forgets the root character", ED,
     "auto [a, b] = split(v.right, k - v.left.size - 1);",
     "auto [a, b] = split(v.right, k - v.left.size);"),
    ("take boundary: <= becomes <", ED,
     "if (k <= v.left.size) return take(v.left, k);",
     "if (k < v.left.size) return take(v.left, k);"),
    ("move target taken in original coordinates", ED,
     "auto [x, y] = split(merge(a, b), p);",
     "auto [x, y] = split(merge(a, b), p > l ? std::min(p + (r - l), t.size - (r - l)) : p);"),
    ("move no-op test uses r instead of l", ED,
     "if (l == r || p == l) return publish(t);",
     "if (l == r || p == r) return publish(t);"),
    ("move re-inserts before the cut is closed", ED,
     "auto [x, y] = split(merge(a, b), p);\n        return publish(merge(merge(x, mid), y));",
     "auto [x, y] = split(merge(a, b), p);\n        return publish(merge(merge(mid, x), y));"),
    ("erase keeps the wrong suffix", ED,
     "flip(take(flip(t), t.size - r))",
     "flip(take(flip(t), t.size - l))"),
    ("erase forgets to mirror the suffix back", ED,
     "return publish(merge(take(t, l), flip(take(flip(t), t.size - r))));",
     "return publish(merge(take(t, l), take(flip(t), t.size - r)));"),
    ("insert puts the new text first", ED,
     "return publish(merge(merge(a, build(s)), b));",
     "return publish(merge(merge(build(s), a), b));"),
    ("build miscounts the left subtree", ED,
     "pool_[spine.back()].lsize + 1 + below.size};",
     "pool_[spine.back()].lsize + below.size};"),
    # --- balance -------------------------------------------------------------
    ("merge compares priorities the wrong way", ED,
     "if (key(a.ref) >= key(b.ref)) {",
     "if (key(a.ref) <= key(b.ref)) {"),
    ("build produces a min-heap", ED,
     "while (!spine.empty() && pool_[spine.back()].key < key) {",
     "while (!spine.empty() && pool_[spine.back()].key > key) {"),
    # --- hashing -------------------------------------------------------------
    ("mul_mod folds at the wrong bit", HS,
     "u64 s = (static_cast<u64>(t) & kP) + static_cast<u64>(t >> kExponent);",
     "u64 s = (static_cast<u64>(t) & kP) + static_cast<u64>(t >> (kExponent + 1));"),
    ("sub_mod wraps instead of borrowing p", HS,
     "constexpr u64 sub_mod(u64 a, u64 b) { return a >= b ? a - b : a + kP - b; }",
     "constexpr u64 sub_mod(u64 a, u64 b) { return a >= b ? a - b : b - a; }"),
    ("node hash uses the wrong power", HS,
     "return {l.len + 1 + r.len, l.f + pw[l.len] * (c + pw.x() * r.f),",
     "return {l.len + 1 + r.len, l.f + pw[r.len] * (c + pw.x() * r.f),"),
    ("backward hash built like the forward one", HS,
     "r.b + pw[r.len] * (c + pw.x() * l.b)};",
     "l.b + pw[l.len] * (c + pw.x() * r.b)};"),
    ("head() subtracts with the wrong power", ED,
     "return forward(t) - pw_[n.left.size + 1] * forward(n.right.ref);",
     "return forward(t) - pw_[n.left.size] * forward(n.right.ref);"),
    # --- queries -------------------------------------------------------------
    ("equal ignores differing lengths", ED,
     "        if (r1 - l1 != r2 - l2) return false;\n",
     ""),
    ("same_text scales by the wrong offsets", ED,
     "return (prefix_hash(a, ra) - prefix_hash(a, la)) * pw_[lb] ==\n"
     "               (prefix_hash(b, rb) - prefix_hash(b, lb)) * pw_[la];",
     "return (prefix_hash(a, ra) - prefix_hash(a, la)) * pw_[la] ==\n"
     "               (prefix_hash(b, rb) - prefix_hash(b, lb)) * pw_[lb];"),
    ("same-reference shortcut ignores the offsets", ED,
     "if (la == ra || (a.ref == b.ref && la == lb)) return true;",
     "if (la == ra || a.ref == b.ref) return true;"),
    ("palindrome mirrors the wrong end", ED,
     "return same_text(t, l, r, flip(t), t.size - r);",
     "return same_text(t, l, r, flip(t), t.size - l);"),
    ("palindrome compares the text with itself", ED,
     "return same_text(t, l, r, flip(t), t.size - r);",
     "return same_text(t, l, r, t, l);"),
    ("walk goes left one position too late", ED,
     "            if (q <= lo + n.left.size) {\n                t = n.left;",
     "            if (q < lo + n.left.size) {\n                t = n.left;"),
    ("lcp scales by the wrong offsets", ED,
     "const Hash wa = pw_[p2], wb = pw_[p1];",
     "const Hash wa = pw_[p1], wb = pw_[p2];"),
    ("lcp limit off by one", ED,
     "const u32 end = p1 + limit;  // and a match cannot extend past `end`",
     "const u32 end = p1 + limit - 1;"),
    ("lcp trusts the limit without testing it", ED,
     "if (matches(end, walk(fr, end))) return limit;",
     "return limit;"),
    ("lcp descent tests the wrong position", ED,
     "if (m < end && matches(m + 1, through)) {",
     "if (m < end && matches(m, through)) {"),
    ("lcp descent advances one too few", ED,
     "known = lo = m + 1;",
     "known = lo = m;"),
    ("lcp climb accepts without testing", ED,
     "if (!matches(e, fr.acc + pw_[fr.lo] * forward(fr.t.ref))) break;",
     "if (false) break;"),
    ("lcp same-version shortcut ignores positions", ED,
     "if (limit == 0 || (ta.ref == tb.ref && p1 == p2)) return limit;",
     "if (limit == 0 || ta.ref == tb.ref) return limit;"),
    ("seek goes left one position too late", ED,
     "            if (q <= fr.lo + n.left.size) {\n                path.push_back({n.left, fr.lo, fr.acc});",
     "            if (q < fr.lo + n.left.size) {\n                path.push_back({n.left, fr.lo, fr.acc});"),
    ("seek pops too little", ED,
     "while (q < path.back().lo || q > path.back().lo + path.back().t.size) path.pop_back();",
     "while (q < path.back().lo) path.pop_back();"),
]

CXX = os.environ.get("CXX", "g++")
FLAGS = ["-std=c++20", "-O1", "-w"]


def limit_memory():
    # A mutant may loop while allocating; stop it at 2 GiB instead of
    # exhausting the machine.
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))


def run(cmd, cwd, timeout, limited=True):
    """True if the command succeeds; False if it fails, crashes or hangs."""
    try:
        return subprocess.run(cmd, cwd=cwd, timeout=timeout, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL,
                              preexec_fn=limit_memory if limited else None).returncode == 0
    except subprocess.TimeoutExpired:
        return False


def test_mutant(index):
    name, path, old, new = MUTANTS[index]
    work = tempfile.mkdtemp(prefix="tte-mutant-")
    try:
        for d in ("src", "tests"):
            shutil.copytree(os.path.join(ROOT, d), os.path.join(work, d))
        target = os.path.join(work, path)
        text = open(target).read()
        if text.count(old) != 1:
            return name, None, "pattern matches %d times (expected 1)" % text.count(old)
        open(target, "w").write(text.replace(old, new))
        for src, exe in (("tests/unit.cpp", "unit"), ("tests/fuzz.cpp", "fuzz"), ("src/main.cpp", "tte")):
            if not run([CXX, *FLAGS, src, "-o", exe], work, 300, limited=False):
                return name, None, "does not compile"
        killers = []
        if not run(["./unit"], work, 60):
            killers.append("unit")
        if not run(["./fuzz", "150", "1"], work, 60):
            killers.append("fuzz")
        # diff.sh uses the repository's generator and naive verifier.
        if not run(["tests/diff.sh", "2", "1", os.path.join(work, "tte")], ROOT, 90):
            killers.append("diff")
        return name, killers, ""
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    args = sys.argv[1:]
    jobs = os.cpu_count() or 1
    if args[:1] == ["-j"]:
        jobs = int(args[1])
        args = args[2:]
    chosen = [i for i, m in enumerate(MUTANTS) if not args or args[0] in m[0]]
    survived = broken = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for name, killers, problem in pool.map(test_mutant, chosen):
            if killers is None:
                broken += 1
                print("BROKEN    %-52s %s" % (name, problem))
            elif not killers:
                survived += 1
                print("SURVIVED  %s" % name)
            else:
                print("killed    %-52s by %s" % (name, ", ".join(killers)))
            sys.stdout.flush()
    print("mutation testing: %d mutants, %d killed, %d survived, %d broken" %
          (len(chosen), len(chosen) - survived - broken, survived, broken))
    sys.exit(1 if survived or broken else 0)


if __name__ == "__main__":
    main()
