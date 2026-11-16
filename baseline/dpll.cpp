// Baseline solver: plain DPLL.
//
//   dpll [--time-limit sec] formula.cnf
//
// Unit propagation (two watched literals) + chronological backtracking.
// There is no clause learning, no restart and no adaptive heuristic: variables
// are tried in a fixed order (most occurrences first, more frequent polarity
// first).  It exists to answer one question in the benchmarks: how much of the
// final solver's speed comes from learning from conflicts?
//
// Output format and exit codes are those of sat_solver (no proof support).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/dimacs.hpp"

namespace {

using Lit = uint32_t;  // 2*var + (negative ? 1 : 0)

struct Dpll {
    uint32_t n = 0;
    std::vector<uint32_t> arena;  // [size][lits...]
    std::vector<std::vector<uint32_t>> watches;
    std::vector<int8_t> val;
    std::vector<Lit> trail;
    size_t qhead = 0;
    bool trivialUnsat = false;
    unsigned long long decisions = 0, conflicts = 0, propagations = 0;

    struct Frame {
        size_t trailPos;   // trail size before the decision
        uint32_t orderIdx; // position of the decision variable in `order`
        Lit lit;
        bool flipped;
    };
    std::vector<Frame> stack;
    std::vector<uint32_t> order;
    std::vector<uint8_t> firstNeg;

    void assign(Lit l) {
        val[l] = 1;
        val[l ^ 1] = -1;
        trail.push_back(l);
    }

    void addClause(std::vector<Lit>& c) {
        std::sort(c.begin(), c.end());
        c.erase(std::unique(c.begin(), c.end()), c.end());
        for (size_t i = 1; i < c.size(); i++)
            if (c[i] == (c[i - 1] ^ 1)) return;  // tautology
        if (c.empty()) {
            trivialUnsat = true;
        } else if (c.size() == 1) {
            if (val[c[0]] < 0) trivialUnsat = true;
            else if (val[c[0]] == 0) assign(c[0]);
        } else {
            const uint32_t ref = uint32_t(arena.size());
            arena.push_back(uint32_t(c.size()));
            arena.insert(arena.end(), c.begin(), c.end());
            watches[c[0]].push_back(ref);
            watches[c[1]].push_back(ref);
        }
    }

    bool propagate() {  // false on conflict
        while (qhead < trail.size()) {
            const Lit fl = trail[qhead++] ^ 1;
            propagations++;
            auto& ws = watches[fl];
            size_t i = 0, j = 0;
            while (i < ws.size()) {
                const uint32_t ref = ws[i++];
                uint32_t* lits = &arena[ref + 1];
                const uint32_t size = arena[ref];
                if (lits[0] == fl) std::swap(lits[0], lits[1]);
                if (val[lits[0]] > 0) {
                    ws[j++] = ref;
                    continue;
                }
                uint32_t k = 2;
                while (k < size && val[lits[k]] < 0) k++;
                if (k < size) {
                    std::swap(lits[1], lits[k]);
                    watches[lits[1]].push_back(ref);
                    continue;
                }
                ws[j++] = ref;
                if (val[lits[0]] < 0) {
                    while (i < ws.size()) ws[j++] = ws[i++];
                    ws.resize(j);
                    return false;
                }
                assign(lits[0]);
            }
            ws.resize(j);
        }
        return true;
    }

    void undoTo(size_t pos) {
        while (trail.size() > pos) {
            const Lit l = trail.back();
            trail.pop_back();
            val[l] = val[l ^ 1] = 0;
        }
        qhead = pos;
    }

    // 1 = SAT, 0 = UNSAT, -1 = time limit
    int solve(double timeLimit) {
        using clock = std::chrono::steady_clock;
        const auto start = clock::now();
        if (trivialUnsat) return 0;
        // Static branching order.
        std::vector<uint32_t> occ(2 * size_t(n), 0);
        for (size_t r = 0; r < arena.size(); r += arena[r] + 1)
            for (uint32_t k = 0; k < arena[r]; k++) occ[arena[r + 1 + k]]++;
        order.resize(n);
        firstNeg.resize(n);
        for (uint32_t v = 0; v < n; v++) {
            order[v] = v;
            firstNeg[v] = occ[2 * v + 1] > occ[2 * v];
        }
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return occ[2 * a] + occ[2 * a + 1] > occ[2 * b] + occ[2 * b + 1];
        });

        uint32_t scan = 0;
        for (;;) {
            if (!propagate()) {
                conflicts++;
                // Chronological backtracking: flip the most recent decision not yet flipped.
                while (!stack.empty() && stack.back().flipped) stack.pop_back();
                if (stack.empty()) return 0;
                Frame& f = stack.back();
                undoTo(f.trailPos);
                f.flipped = true;
                f.lit ^= 1;
                assign(f.lit);
                scan = f.orderIdx + 1;
                if ((conflicts & 1023) == 0 && timeLimit > 0 &&
                    std::chrono::duration<double>(clock::now() - start).count() > timeLimit)
                    return -1;
                continue;
            }
            while (scan < n && val[2 * order[scan]] != 0) scan++;
            if (scan == n) return 1;
            const uint32_t v = order[scan];
            const Lit l = 2 * v + firstNeg[v];
            stack.push_back(Frame{trail.size(), scan, l, false});
            decisions++;
            assign(l);
            scan++;
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    double timeLimit = 0;
    const char* input = nullptr;
    bool quiet = false, showModel = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--time-limit") && i + 1 < argc) timeLimit = atof(argv[++i]);
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet")) quiet = true;
        else if (!strcmp(argv[i], "--no-model")) showModel = false;
        else input = argv[i];
    }
    if (!input) {
        fprintf(stderr, "usage: dpll [--time-limit sec] [-q] [--no-model] <formula.cnf>\n");
        return 1;
    }
    FILE* in = fopen(input, "r");
    if (!in) {
        fprintf(stderr, "error: cannot open %s\n", input);
        return 1;
    }
    sat::DimacsReader reader(in);
    if (!reader.readHeader()) {
        fprintf(stderr, "error: %s\n", reader.error().c_str());
        return 1;
    }
    Dpll s;
    s.n = uint32_t(reader.numVars);
    s.watches.resize(2 * size_t(s.n));
    s.val.assign(2 * size_t(s.n), 0);
    std::vector<int> clause;
    std::vector<Lit> lits;
    while (reader.readClause(clause)) {
        lits.clear();
        for (int d : clause) lits.push_back(d > 0 ? 2u * uint32_t(d - 1) : 2u * uint32_t(-d - 1) + 1u);
        s.addClause(lits);
    }
    fclose(in);
    if (!reader.error().empty()) {
        fprintf(stderr, "error: %s\n", reader.error().c_str());
        return 1;
    }
    const int r = s.solve(timeLimit);
    if (!quiet) {
        printf("c decisions    %llu\n", s.decisions);
        printf("c conflicts    %llu\n", s.conflicts);
        printf("c propagations %llu\n", s.propagations);
    }
    if (r < 0) {
        puts("s UNKNOWN");
        return 0;
    }
    if (r == 0) {
        puts("s UNSATISFIABLE");
        return 20;
    }
    puts("s SATISFIABLE");
    if (showModel) {
        std::string line = "v";
        for (uint32_t v = 0; v < s.n; v++) {
            const std::string t = " " + std::to_string(s.val[2 * v] > 0 ? int(v + 1) : -int(v + 1));
            if (line.size() + t.size() > 78) {
                puts(line.c_str());
                line = "v";
            }
            line += t;
        }
        puts((line + " 0").c_str());
    }
    return 10;
}
