// Independent SAT certificate checker.
//
//   model_check [--assume "l1 l2 ..."] formula.cnf solver_output
//
// Reads the solver's output ("s SATISFIABLE" plus "v" lines), and checks that
//   1. the claimed status is SATISFIABLE,
//   2. the v-lines assign every variable 1..n exactly one value,
//   3. every clause of the formula contains a literal that is true,
//   4. every --assume literal is true.
// Nothing about the solver is trusted; the check is a direct substitution.
//
// Exit status: 0 = model verified, 1 = rejected, 2 = usage / input error.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cnf.hpp"

int main(int argc, char** argv) {
    std::vector<const char*> assumeArgs, files;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--assume") && i + 1 < argc) assumeArgs.push_back(argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1] == '-') files.clear(), i = argc;
        else files.push_back(argv[i]);
    }
    if (files.size() != 2) {
        fprintf(stderr, "usage: model_check [--assume \"l1 l2 ...\"] <formula.cnf> <solver-output>\n");
        return 2;
    }
    chk::Formula f;
    if (!chk::loadCnf(files[0], f)) return 2;
    FILE* out = fopen(files[1], "rb");
    if (!out) {
        fprintf(stderr, "error: cannot read %s\n", files[1]);
        return 2;
    }

    auto reject = [](const std::string& why) {
        printf("c %s\n", why.c_str());
        puts("s MODEL REJECTED");
        return 1;
    };

    // Parse the solver output line by line.
    std::vector<signed char> value(size_t(f.numVars) + 1, 0);
    bool sawStatus = false, satisfiable = false, terminated = false;
    chk::Tokens toks(out);
    std::string tok;
    char mode = 0;
    size_t lastLine = 0;
    while (toks.next(tok)) {
        if (toks.line() != lastLine) {  // first token of a line selects its kind
            lastLine = toks.line();
            mode = tok.size() == 1 ? tok[0] : 0;
            if (mode == 's') {
                std::string status;
                toks.next(status);
                if (sawStatus) return reject("more than one status line");
                sawStatus = true;
                satisfiable = status == "SATISFIABLE";
                toks.skipLine();
            } else if (mode != 'v') {
                toks.skipLine();  // comments and anything else
            }
            continue;
        }
        if (mode != 'v') continue;
        long long x;
        if (!chk::parseInt(tok, x) || x > f.numVars || -x > f.numVars) return reject("malformed value '" + tok + "'");
        if (terminated) return reject("values after the terminating 0");
        if (x == 0) {
            terminated = true;
            continue;
        }
        const size_t v = size_t(x > 0 ? x : -x);
        const signed char s = x > 0 ? 1 : -1;
        if (value[v] == -s) return reject("variable " + std::to_string(v) + " is assigned both values");
        value[v] = s;
    }
    fclose(out);
    if (!sawStatus || !satisfiable) return reject("output does not claim SATISFIABLE");
    if (!terminated) return reject("value list is not terminated by 0");
    for (int v = 1; v <= f.numVars; v++)
        if (value[size_t(v)] == 0) return reject("variable " + std::to_string(v) + " has no value");

    auto isTrue = [&](int lit) { return lit > 0 ? value[size_t(lit)] > 0 : value[size_t(-lit)] < 0; };
    for (size_t i = 0; i < f.numClauses(); i++) {
        bool sat = false;
        for (size_t k = f.start[i]; k < f.start[i + 1] && !sat; k++) sat = isTrue(f.lits[k]);
        if (!sat) return reject("clause " + std::to_string(i + 1) + " is falsified");
    }
    for (const char* a : assumeArgs) {
        std::vector<int> lits;
        if (!chk::parseLitList(a, f.numVars, lits)) {
            fprintf(stderr, "error: bad --assume list\n");
            return 2;
        }
        for (int l : lits)
            if (!isTrue(l)) return reject("assumption " + std::to_string(l) + " is false");
    }
    printf("c checked %zu clauses over %d variables\n", f.numClauses(), f.numVars);
    puts("s MODEL VERIFIED");
    return 0;
}
