"""Shared plumbing for the test suite.

The central function is `certify`: run the solver on a formula and refuse to
believe the answer until an independent program has checked the certificate.

    SAT    -> the model is substituted into the formula by checker/model_check
              (C++) and again by tools/cnf.py (Python);
    UNSAT  -> the proof is replayed by checker/rup_check (C++), and on request
              by checker/rup_check.py (naive reference) and drat-trim (third party).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "checker"))

import cnf  # noqa: E402
import rup_check as pycheck  # noqa: E402


class Failure(Exception):
    """A test observed something that must never happen."""


class Context:
    def __init__(self, bin_dir=None, quick=False, jobs=None, keep=False):
        self.bin_dir = os.path.abspath(bin_dir or os.path.join(ROOT, "build"))
        self.quick = quick
        self.jobs = jobs or min(8, os.cpu_count() or 2)
        os.makedirs(os.path.join(ROOT, "build", "tmp"), exist_ok=True)
        self.tmp = tempfile.mkdtemp(prefix="sat-tests-", dir=os.path.join(ROOT, "build", "tmp"))
        self.keep = keep
        self._counter = 0
        self._lock = threading.Lock()
        self.solver = os.path.join(self.bin_dir, "sat_solver")
        self.rup_check = os.path.join(self.bin_dir, "rup_check")
        self.model_check = os.path.join(self.bin_dir, "model_check")
        self.brute = os.path.join(self.bin_dir, "brute")
        self.dpll = os.path.join(self.bin_dir, "dpll")
        dt = os.path.join(ROOT, "build", "third_party", "drat-trim")
        self.drat_trim = dt if os.path.exists(dt) else None
        # Sanitizer builds are 5-20x slower; scale every timeout accordingly.
        self.slow = float(os.environ.get("SAT_TEST_SLOWDOWN", "10" if "san" in self.bin_dir else "1"))
        for b in (self.solver, self.rup_check, self.model_check, self.brute, self.dpll):
            if not os.path.exists(b):
                raise SystemExit("missing binary %s (run `make` first)" % b)

    def path(self, suffix):
        with self._lock:
            self._counter += 1
            return os.path.join(self.tmp, "f%07d%s" % (self._counter, suffix))

    def cleanup(self):
        if not self.keep:
            shutil.rmtree(self.tmp, ignore_errors=True)

    def map(self, fn, items):
        """Run fn over items on a thread pool (the work is in subprocesses)."""
        with ThreadPoolExecutor(self.jobs) as pool:
            return list(pool.map(fn, items))


def run(cmd, timeout=None, stdout_path=None):
    """Returns (exit_code, stdout_text, seconds, peak_rss_mb).  exit_code is
    None on timeout and negative when the process died from a signal."""
    start = time.time()
    out = open(stdout_path, "w+") if stdout_path else tempfile.TemporaryFile("w+")
    try:
        proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.PIPE, text=True)
        timer = None
        timed_out = []
        if timeout:
            def kill():
                timed_out.append(True)
                proc.kill()
            timer = threading.Timer(timeout, kill)
            timer.start()
        err = proc.stderr.read()
        _, status, usage = os.wait4(proc.pid, 0)
        proc.returncode = os.waitstatus_to_exitcode(status)
        if timer:
            timer.cancel()
        out.seek(0)
        text = out.read()
        rc = None if timed_out else proc.returncode
        return rc, text + (("\n[stderr] " + err) if err.strip() else ""), time.time() - start, usage.ru_maxrss / 1024.0
    finally:
        out.close()


def write_formula(ctx, num_vars, clauses, expected=None):
    path = ctx.path(".cnf")
    cnf.write_cnf(path, num_vars, clauses, ["expected " + expected] if expected else [])
    return path


def brute_status(ctx, path, assume=None):
    cmd = [ctx.brute, path] + (["--assume", " ".join(map(str, assume))] if assume else [])
    rc, text, _, _ = run(cmd, timeout=600 * ctx.slow)
    if rc == 10:
        return "SAT"
    if rc == 20:
        return "UNSAT"
    raise Failure("brute force failed on %s: rc=%s %s" % (path, rc, text[-300:]))


def check_proof(ctx, formula, proof, assume=None, ignore_deletions=False, timeout=3600):
    """C++ checker verdict: (verified, output, seconds, rss)."""
    cmd = [ctx.rup_check]
    if ignore_deletions:
        cmd.append("--ignore-deletions")
    if assume is not None:
        cmd += ["--assume", " ".join(map(str, assume))]
    rc, text, secs, rss = run(cmd + [formula, proof], timeout=timeout * ctx.slow)
    if rc not in (0, 1):
        raise Failure("rup_check crashed or timed out (rc=%s) on %s %s: %s" % (rc, formula, proof, text[-300:]))
    if (rc == 0) != ("s VERIFIED" in text):
        raise Failure("rup_check exit code and output disagree on %s" % formula)
    return rc == 0, text, secs, rss


def check_proof_reference(formula, proof, assume=(), ignore_deletions=False):
    """Naive Python checker verdict."""
    n, clauses = cnf.read_cnf(formula)
    ok, _ = pycheck.check(n, clauses, proof, assume, ignore_deletions)
    return ok


def check_proof_drat_trim(ctx, formula, proof):
    """Third-party verdict (None if drat-trim is not installed).  -U: RUP-only
    forward mode is not needed; default backward mode accepts every DRUP proof."""
    if not ctx.drat_trim:
        return None
    rc, text, _, _ = run([ctx.drat_trim, formula, proof, "-w"], timeout=3600 * ctx.slow)
    if "s VERIFIED" in text:
        return True
    if "s NOT VERIFIED" in text or "s DERIVATION" in text or rc is None:
        return False
    return False


def check_model(ctx, formula, output_path, assume=None):
    cmd = [ctx.model_check] + (["--assume", " ".join(map(str, assume))] if assume else [])
    rc, text, _, _ = run(cmd + [formula, output_path], timeout=600 * ctx.slow)
    if rc not in (0, 1):
        raise Failure("model_check crashed (rc=%s) on %s: %s" % (rc, formula, text[-300:]))
    return rc == 0, text


class Verdict:
    def __init__(self):
        self.status = None        # SAT / UNSAT / UNKNOWN
        self.solve_time = 0.0
        self.check_time = 0.0
        self.solver_rss = 0.0
        self.checker_rss = 0.0
        self.proof_bytes = 0
        self.stats = {}
        self.output = ""


def parse_stats(text):
    stats = {}
    for line in text.splitlines():
        if line.startswith("c ") and len(line) > 20:
            key = line[2:19].strip()
            val = line[19:].split()
            if key and val:
                try:
                    stats[key] = float(val[0])
                except ValueError:
                    pass
    return stats


def certify(ctx, formula, expected=None, options=(), timeout=300, reference=False, drat_trim=False,
            clauses=None, num_vars=None, keep_proof=False):
    """Solve `formula`, verify the certificate independently, and compare with
    `expected` if the status is known by construction.  Raises Failure on any
    discrepancy.  Returns a Verdict (status UNKNOWN on timeout)."""
    v = Verdict()
    out_path = ctx.path(".out")
    proof_path = ctx.path(".rup")
    rc, text, v.solve_time, v.solver_rss = run(
        [ctx.solver, "--proof", proof_path, *options, formula], timeout=timeout * ctx.slow, stdout_path=out_path)
    v.output = text
    v.stats = parse_stats(text)
    what = "%s (options %s)" % (formula, " ".join(options) or "none")
    try:
        if rc is None:
            v.status = "UNKNOWN"
            return v
        results = cnf.parse_solver_output(text)
        if len(results) != 1:
            raise Failure("solver printed %d status lines on %s; rc=%s; output tail: %s"
                          % (len(results), what, rc, text[-400:]))
        res = results[0]
        v.status = res["status"]
        if v.status == "UNKNOWN":
            if rc != 0:
                raise Failure("exit code %s with s UNKNOWN on %s" % (rc, what))
            return v
        if (v.status, rc) not in (("SAT", 10), ("UNSAT", 20)):
            raise Failure("status %s with exit code %s on %s: %s" % (v.status, rc, what, text[-400:]))
        if expected and v.status != expected:
            raise Failure("WRONG ANSWER: solver says %s, instance is %s by construction: %s"
                          % (v.status, expected, what))
        if v.status == "SAT":
            ok, msg = check_model(ctx, formula, out_path)
            if not ok:
                raise Failure("INVALID MODEL (model_check) on %s: %s" % (what, msg[-300:]))
            if clauses is None and os.path.getsize(formula) < 4_000_000:
                num_vars, clauses = cnf.read_cnf(formula)
            if clauses is not None:
                why = cnf.model_satisfies(num_vars, clauses, res["model"] or []) if res["model_terminated"] \
                    else "model not terminated"
                if why:
                    raise Failure("INVALID MODEL (python) on %s: %s" % (what, why))
        else:
            v.proof_bytes = os.path.getsize(proof_path)
            ok, msg, v.check_time, v.checker_rss = check_proof(ctx, formula, proof_path)
            if not ok:
                raise Failure("INVALID PROOF on %s: %s" % (what, msg[-300:]))
            if reference and not check_proof_reference(formula, proof_path):
                raise Failure("reference checker rejects a proof accepted by rup_check: %s" % what)
            if drat_trim and ctx.drat_trim and not check_proof_drat_trim(ctx, formula, proof_path):
                raise Failure("drat-trim rejects a proof accepted by rup_check: %s" % what)
        return v
    finally:
        if keep_proof:
            v.proof_path = proof_path
            v.out_path = out_path
        else:
            for p in (out_path, proof_path):
                if os.path.exists(p):
                    os.remove(p)


class Report:
    """Collects counters and failures of one test group."""

    def __init__(self, name):
        self.name = name
        self.counters = {}
        self.failures = []
        self.notes = []
        self.rows = []   # optional per-instance table
        self.seconds = 0.0
        self._lock = threading.Lock()

    def count(self, key, n=1):
        with self._lock:
            self.counters[key] = self.counters.get(key, 0) + n

    def fail(self, msg):
        with self._lock:
            self.failures.append(str(msg))

    def guard(self, fn, *args):
        """Run fn; a Failure (or any crash in the test itself) is recorded, not raised."""
        try:
            return fn(*args)
        except Failure as e:
            self.fail(e)
        except Exception as e:  # noqa: BLE001 - a broken test must not look like a pass
            self.fail("test error: %r" % (e,))
        return None

    def tally(self, verdict):
        if verdict is None:
            return
        self.count("instances")
        self.count({"SAT": "sat (model verified)", "UNSAT": "unsat (proof verified)"}.get(verdict.status, "unknown"))
