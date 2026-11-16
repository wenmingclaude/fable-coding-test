// Brute-force reference: decides a small formula by evaluating it on all 2^n
// assignments.  No propagation, no search, no cleverness beyond evaluating 64
// assignments per machine word:
//
//   Fix variables 7..n by the block number; variables 1..6 take all 64
//   combinations inside one 64-bit word (variable 1 = pattern 0101..., variable
//   2 = 0011..., ...).  A clause is the OR of its literals' words, the formula
//   the AND of its clauses; a set bit in the result is a satisfying assignment.
//
//   brute [--assume "l1 l2 ..."] [--count] formula.cnf
//
// Exit status: 10 = SATISFIABLE, 20 = UNSATISFIABLE, 2 = error (as the solver).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../checker/cnf.hpp"

int main(int argc, char** argv) {
    bool count = false;
    std::vector<const char*> assumeArgs, files;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--count")) count = true;
        else if (!strcmp(argv[i], "--assume") && i + 1 < argc) assumeArgs.push_back(argv[++i]);
        else files.push_back(argv[i]);
    }
    if (files.size() != 1) {
        fprintf(stderr, "usage: brute [--assume \"l1 l2 ...\"] [--count] <formula.cnf>\n");
        return 2;
    }
    chk::Formula f;
    if (!chk::loadCnf(files[0], f)) return 2;
    if (f.numVars > 30) {
        fprintf(stderr, "error: brute force is limited to 30 variables\n");
        return 2;
    }
    for (const char* a : assumeArgs) {  // an assumption is just one more unit clause
        std::vector<int> lits;
        if (!chk::parseLitList(a, f.numVars, lits)) {
            fprintf(stderr, "error: bad --assume list\n");
            return 2;
        }
        for (int l : lits) {
            f.lits.push_back(l);
            f.start.push_back(f.lits.size());
        }
    }

    static const uint64_t PATTERN[6] = {0xAAAAAAAAAAAAAAAAull, 0xCCCCCCCCCCCCCCCCull, 0xF0F0F0F0F0F0F0F0ull,
                                        0xFF00FF00FF00FF00ull, 0xFFFF0000FFFF0000ull, 0xFFFFFFFF00000000ull};
    const int n = f.numVars;
    const int low = n < 6 ? n : 6;
    const uint64_t valid = low == 6 ? ~0ull : (1ull << (1u << low)) - 1;  // 2^low assignments per word
    const uint64_t blocks = 1ull << (n - low);
    std::vector<uint64_t> word(size_t(n) + 1);
    uint64_t models = 0;
    for (uint64_t b = 0; b < blocks; b++) {
        for (int v = 1; v <= n; v++) word[size_t(v)] = v <= 6 ? PATTERN[v - 1] : ((b >> (v - 7)) & 1 ? ~0ull : 0ull);
        uint64_t all = valid;
        for (size_t i = 0; i < f.numClauses() && all; i++) {
            uint64_t clause = 0;
            for (size_t k = f.start[i]; k < f.start[i + 1]; k++) {
                const int l = f.lits[k];
                clause |= l > 0 ? word[size_t(l)] : ~word[size_t(-l)];
            }
            all &= clause;
        }
        if (!all) continue;
        if (count) {
            models += uint64_t(__builtin_popcountll(all));
            continue;
        }
        const int bit = __builtin_ctzll(all);
        puts("s SATISFIABLE");
        std::string line = "v";
        for (int v = 1; v <= n; v++) {
            const bool val = v <= 6 ? (bit >> (v - 1)) & 1 : (b >> (v - 7)) & 1;
            line += " " + std::to_string(val ? v : -v);
        }
        puts((line + " 0").c_str());
        return 10;
    }
    if (count) printf("c models %llu\n", (unsigned long long)models);
    if (models) {
        puts("s SATISFIABLE");
        return 10;
    }
    puts("s UNSATISFIABLE");
    return 20;
}
