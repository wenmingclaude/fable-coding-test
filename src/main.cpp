// Command-line front end of the CDCL solver.
//
//   sat_solver [options] formula.cnf
//
// Exit status follows the SAT-competition convention:
//   10 = SATISFIABLE, 20 = UNSATISFIABLE, 0 = UNKNOWN / incremental mode, 1 = error.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include <sys/resource.h>

#include "dimacs.hpp"
#include "solver.hpp"

using namespace sat;

static const char* USAGE =
    "usage: sat_solver [options] <formula.cnf>\n"
    "\n"
    "output\n"
    "  --proof <file>         write an UNSAT proof (RUP clause additions + 'd' deletion hints)\n"
    "  --proof-no-deletions   write clause additions only\n"
    "  --no-model             do not print the 'v' lines\n"
    "  -q, --quiet            do not print statistics\n"
    "\n"
    "incremental solving\n"
    "  --queries <file>       each query is a list of assumption literals terminated by 0;\n"
    "                         all queries are answered on the same solver instance\n"
    "\n"
    "limits (per query)\n"
    "  --time-limit <sec>     give up with 's UNKNOWN' after this many seconds\n"
    "  --conflict-limit <n>   give up after this many conflicts\n"
    "  --mem-limit <MB>       soft memory budget for clauses (default 1024)\n"
    "\n"
    "ablation switches (for benchmarks; all features are on by default)\n"
    "  --no-minimize --no-phase-saving --no-reduce --no-blocker --no-inline-binary\n"
    "  --no-vsids --no-simplify --no-stable --no-target-phase\n"
    "  --restart=ema|luby|none   --phase=0|1\n";

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static double peakMemMB() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss / 1024.0;
}

static void printModel(const Solver& s) {
    const auto& m = s.model();
    std::string line = "v";
    for (uint32_t v = 0; v < s.numVars(); v++) {
        char tmp[16];
        snprintf(tmp, sizeof tmp, " %d", m[v] > 0 ? int(v + 1) : -int(v + 1));
        if (line.size() + strlen(tmp) > 78) {
            puts(line.c_str());
            line = "v";
        }
        line += tmp;
    }
    line += " 0";
    puts(line.c_str());
}

static void printStats(const Solver& s, double parseTime, double solveTime) {
    const Stats& st = s.stats();
    printf("c conflicts        %llu\n", (unsigned long long)st.conflicts);
    printf("c decisions        %llu\n", (unsigned long long)st.decisions);
    printf("c propagations     %llu\n", (unsigned long long)st.propagations);
    printf("c restarts         %llu\n", (unsigned long long)st.restarts);
    printf("c reductions       %llu (deleted %llu learnt clauses)\n", (unsigned long long)st.reductions,
           (unsigned long long)st.deletedLearnts);
    printf("c simplifications  %llu (removed %llu satisfied, strengthened %llu)\n",
           (unsigned long long)st.simplifications, (unsigned long long)st.removedSatisfied,
           (unsigned long long)st.strengthened);
    printf("c learnt           %llu clauses (%llu units, %llu binary)\n", (unsigned long long)st.learntClauses,
           (unsigned long long)st.learntUnits, (unsigned long long)st.learntBinaries);
    printf("c learnt literals  %llu before / %llu after minimisation\n", (unsigned long long)st.learntLitsBeforeMin,
           (unsigned long long)st.learntLitsAfterMin);
    printf("c mode switches    %llu, rephases %llu\n", (unsigned long long)st.modeSwitches,
           (unsigned long long)st.rephases);
    printf("c proof            %llu additions, %llu deletions, %llu bytes\n", (unsigned long long)st.proofAdded,
           (unsigned long long)st.proofDeleted, (unsigned long long)st.proofBytes);
    printf("c parse time       %.3f s\n", parseTime);
    printf("c solve time       %.3f s\n", solveTime);
    printf("c peak memory      %.1f MB\n", peakMemMB());
}

static int die(const std::string& msg) {
    fprintf(stderr, "error: %s\n", msg.c_str());
    return 1;
}

// Reads the queries file: whitespace-separated literals, each query terminated by 0.
static bool readQueries(const char* path, uint32_t numVars, std::vector<std::vector<Lit>>& out, std::string& err) {
    FILE* f = fopen(path, "r");
    if (!f) {
        err = std::string("cannot open ") + path;
        return false;
    }
    std::vector<Lit> cur;
    char tok[64];
    bool pending = false;
    while (fscanf(f, "%63s", tok) == 1) {
        if (tok[0] == 'c') {  // comment to end of line
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        char* end;
        long x = strtol(tok, &end, 10);
        if (*end || x > long(numVars) || -x > long(numVars)) {
            err = std::string("bad literal in queries file: ") + tok;
            fclose(f);
            return false;
        }
        if (x == 0) {
            out.push_back(cur);
            cur.clear();
            pending = false;
        } else {
            cur.push_back(fromDimacs(int(x)));
            pending = true;
        }
    }
    fclose(f);
    if (pending) {
        err = "last query is not terminated by 0";
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    Options opt;
    const char* input = nullptr;
    const char* proofPath = nullptr;
    const char* queriesPath = nullptr;
    bool proofDeletions = true, quiet = false, showModel = true;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto needArg = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s needs an argument\n", name);
                exit(1);
            }
            return argv[++i];
        };
        if (a == "--proof") proofPath = needArg("--proof");
        else if (a == "--proof-no-deletions") proofDeletions = false;
        else if (a == "--queries") queriesPath = needArg("--queries");
        else if (a == "--time-limit") opt.timeLimitSec = atof(needArg("--time-limit"));
        else if (a == "--conflict-limit") opt.conflictLimit = strtoull(needArg("--conflict-limit"), nullptr, 10);
        else if (a == "--mem-limit") opt.memLimitMB = strtoull(needArg("--mem-limit"), nullptr, 10);
        else if (a == "-q" || a == "--quiet") quiet = true;
        else if (a == "--no-model") showModel = false;
        else if (a == "--no-minimize") opt.minimize = false;
        else if (a == "--no-phase-saving") opt.phaseSaving = false;
        else if (a == "--no-reduce") opt.reduceDB = false;
        else if (a == "--no-blocker") opt.blocker = false;
        else if (a == "--no-inline-binary") opt.inlineBinary = false;
        else if (a == "--no-vsids") opt.vsids = false;
        else if (a == "--no-simplify") opt.simplify = false;
        else if (a == "--no-stable") opt.stable = false;
        else if (a == "--no-target-phase") opt.targetPhase = false;
        else if (a == "--restart=ema") opt.restart = RestartPolicy::EMA;
        else if (a == "--restart=luby") opt.restart = RestartPolicy::LUBY;
        else if (a == "--restart=none") opt.restart = RestartPolicy::NONE;
        else if (a == "--phase=0") opt.initialPhase = false;
        else if (a == "--phase=1") opt.initialPhase = true;
        else if (a == "-h" || a == "--help") {
            fputs(USAGE, stdout);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            fprintf(stderr, "error: unknown option %s\n\n%s", a.c_str(), USAGE);
            return 1;
        } else if (!input) {
            input = argv[i];
        } else {
            return die("more than one input file");
        }
    }
    if (!input) {
        fputs(USAGE, stderr);
        return 1;
    }

    try {
        const double t0 = nowSec();
        FILE* in = fopen(input, "r");
        if (!in) return die(std::string("cannot open ") + input);
        DimacsReader reader(in);
        if (!reader.readHeader()) return die(std::string(input) + ": " + reader.error());

        Solver solver(opt);
        solver.init(uint32_t(reader.numVars));
        FILE* proofFile = nullptr;
        if (proofPath) {
            proofFile = fopen(proofPath, "w");
            if (!proofFile) return die(std::string("cannot open ") + proofPath);
            solver.setProof(proofFile, proofDeletions);
        }

        std::vector<int> clause;
        std::vector<Lit> lits;
        while (reader.readClause(clause)) {
            lits.clear();
            for (int d : clause) lits.push_back(fromDimacs(d));
            solver.addClause(lits);
        }
        fclose(in);
        if (!reader.error().empty()) return die(std::string(input) + ": " + reader.error());
        const double t1 = nowSec();
        if (!quiet)
            printf("c parsed %d variables, %lld clauses in %.3f s\n", reader.numVars, reader.clausesRead, t1 - t0);

        int exitCode = 0;
        if (!queriesPath) {
            const Result r = solver.solve();
            solver.flushProof();
            const double t2 = nowSec();
            if (!quiet) printStats(solver, t1 - t0, t2 - t1);
            if (r == Result::SAT) {
                puts("s SATISFIABLE");
                if (showModel) printModel(solver);
                exitCode = 10;
            } else if (r == Result::UNSAT) {
                puts("s UNSATISFIABLE");
                exitCode = 20;
            } else {
                puts("s UNKNOWN");
            }
        } else {
            std::vector<std::vector<Lit>> queries;
            std::string err;
            if (!readQueries(queriesPath, solver.numVars(), queries, err)) return die(err);
            for (size_t q = 0; q < queries.size(); q++) {
                const double tq = nowSec();
                const Result r = solver.solve(queries[q]);
                solver.flushProof();
                printf("q %zu\n", q + 1);
                if (!quiet)
                    printf("c query %zu: %.3f s, total conflicts %llu\n", q + 1, nowSec() - tq,
                           (unsigned long long)solver.stats().conflicts);
                if (r == Result::SAT) {
                    puts("s SATISFIABLE");
                    if (showModel) printModel(solver);
                } else if (r == Result::UNSAT) {
                    // F itself is refuted: unsatisfiable under any assumptions.
                    puts("s UNSATISFIABLE");
                } else if (r == Result::UNSAT_ASSUMPTIONS) {
                    puts("s UNSATISFIABLE UNDER ASSUMPTIONS");
                    std::string line = "a";
                    for (Lit l : solver.failedAssumptions()) line += " " + std::to_string(toDimacs(l));
                    puts((line + " 0").c_str());
                } else {
                    puts("s UNKNOWN");
                }
                fflush(stdout);
            }
            if (!quiet) printStats(solver, t1 - t0, nowSec() - t1);
        }
        if (proofFile) fclose(proofFile);
        return exitCode;
    } catch (const std::exception& e) {
        // Out of memory or over budget: no answer is better than a wrong one.
        printf("c aborted: %s\n", e.what());
        puts("s UNKNOWN");
        return 0;
    }
}
