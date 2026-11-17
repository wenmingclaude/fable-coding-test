// Independent UNSAT proof checker (forward RUP / DRUP).
//
//   rup_check [--ignore-deletions] [--assume "l1 l2 ..."] formula.cnf proof
//
// The checker understands exactly two things: unit propagation, and the rule
//
//     a clause C may be added if assuming the negation of every literal of C
//     and running unit propagation on the current clause set yields a conflict.
//
// It shares no code with the solver and knows nothing about decisions,
// learning, restarts or heuristics.  State:
//
//     DB  a multiset of clauses            T  a set of literals ("top-level units")
//
// Invariant: every clause in DB and every literal in T is a logical
// consequence of the input formula F (plus --assume literals, if given).
//
//   start        DB = F,  T = closure of DB under unit propagation
//   "C 0"        require: unit propagation on DB from T ∪ {¬l : l ∈ C} hits a
//                conflict.  Then C is a consequence of F; add it to DB and
//                extend T by unit propagation.
//   "d C 0"      remove one copy of C from DB if present (T is not shrunk;
//                both are harmless because the invariant only needs "implied by F").
//   accept       iff at some point unit propagation from T alone hits a
//                conflict, i.e. the empty clause has been derived.
//
// Exit status: 0 = proof verified, 1 = proof rejected, 2 = usage / input error.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cnf.hpp"

namespace {

using chk::Formula;

inline uint32_t code(int dimacsLit) {  // 2*(v-1) for v, 2*(v-1)+1 for -v
    return dimacsLit > 0 ? 2u * uint32_t(dimacsLit - 1) : 2u * uint32_t(-dimacsLit - 1) + 1u;
}

class Checker {
public:
    explicit Checker(int numVars)
        : val_(2 * size_t(numVars), 0), mark_(2 * size_t(numVars), 0), watches_(2 * size_t(numVars)) {
        buckets_.assign(1 << 16, NONE);
    }

    bool refuted() const { return refuted_; }

    // Sorts, removes duplicate literals; returns false if the clause is a tautology.
    static bool normalise(std::vector<uint32_t>& c) {
        std::sort(c.begin(), c.end());
        c.erase(std::unique(c.begin(), c.end()), c.end());
        for (size_t i = 1; i < c.size(); i++)
            if (c[i] == (c[i - 1] ^ 1u)) return false;
        return true;
    }

    // Adds a (normalised, non-tautological) clause to DB and extends T.
    void addClause(std::vector<uint32_t>& c) {
        if (refuted_) return;
        size_t unassigned = 0;
        for (size_t i = 0; i < c.size(); i++) {
            if (val_[c[i]] > 0) return;  // contains a literal of T: satisfied forever, never needed
            if (val_[c[i]] == 0) std::swap(c[unassigned++], c[i]);
        }
        if (unassigned == 0) {  // every literal is false under T (includes the empty clause)
            refuted_ = true;
            return;
        }
        if (unassigned == 1) {  // unit under T
            assign(c[0]);
            if (!propagate()) refuted_ = true;
            return;
        }
        const uint32_t ref = uint32_t(arena_.size());
        arena_.push_back(uint32_t(c.size()));
        arena_.push_back(NONE);
        arena_.insert(arena_.end(), c.begin(), c.end());
        watches_[c[0]].push_back(Watch{ref, c[1]});
        watches_[c[1]].push_back(Watch{ref, c[0]});
        hashInsert(ref);
        liveClauses_++;
    }

    // The RUP test for a (normalised, non-tautological) clause.
    bool hasRup(const std::vector<uint32_t>& c) {
        if (refuted_) return true;
        const size_t top = trail_.size();
        bool conflict = false;
        for (uint32_t l : c)
            if (val_[l] > 0) conflict = true;  // ¬l contradicts T directly
        if (!conflict) {
            for (uint32_t l : c)
                if (val_[l] == 0) assign(l ^ 1u);
            conflict = !propagate();
        }
        while (trail_.size() > top) {  // undo the temporary assumptions
            const uint32_t l = trail_.back();
            trail_.pop_back();
            val_[l] = val_[l ^ 1u] = 0;
        }
        qhead_ = top;
        return conflict;
    }

    // Removes one copy of the clause from DB; returns false if there is none.
    bool deleteClause(const std::vector<uint32_t>& c) {
        for (uint32_t l : c) mark_[l] = 1;
        const size_t b = hashOf(c.data(), c.size()) & (buckets_.size() - 1);
        bool found = false;
        uint32_t prev = NONE;
        for (uint32_t ref = buckets_[b]; ref != NONE; prev = ref, ref = arena_[ref + 1]) {
            if (arena_[ref] != c.size()) continue;
            bool same = true;
            for (size_t i = 0; i < c.size() && same; i++) same = mark_[arena_[ref + 2 + i]] != 0;
            if (!same) continue;
            (prev == NONE ? buckets_[b] : arena_[prev + 1]) = arena_[ref + 1];
            arena_[ref] |= DELETED;
            garbage_ += c.size() + 2;
            liveClauses_--;
            found = true;
            break;
        }
        for (uint32_t l : c) mark_[l] = 0;
        if (garbage_ > (1u << 20) && garbage_ * 2 > arena_.size()) compact();
        return found;
    }

private:
    struct Watch {
        uint32_t ref, blocker;
    };
    static constexpr uint32_t NONE = 0xFFFFFFFFu, DELETED = 0x80000000u;

    void assign(uint32_t l) {
        val_[l] = 1;
        val_[l ^ 1u] = -1;
        trail_.push_back(l);
    }

    // Unit propagation with two watched literals.  Returns false on conflict.
    // Stored clauses always watch their first two literals; outside of this
    // function both are non-false (see addClause / the undo in hasRup).
    bool propagate() {
        while (qhead_ < trail_.size()) {
            const uint32_t falseLit = trail_[qhead_++] ^ 1u;
            std::vector<Watch>& ws = watches_[falseLit];
            size_t i = 0, j = 0;
            while (i < ws.size()) {
                const Watch w = ws[i++];
                if (val_[w.blocker] > 0) {  // clause satisfied
                    ws[j++] = w;
                    continue;
                }
                uint32_t* c = &arena_[w.ref];
                if (c[0] & DELETED) continue;  // drop watchers of deleted clauses lazily
                const uint32_t size = c[0];
                uint32_t* lits = c + 2;
                if (lits[0] == falseLit) std::swap(lits[0], lits[1]);
                const uint32_t other = lits[0];
                if (val_[other] > 0) {
                    ws[j++] = Watch{w.ref, other};
                    continue;
                }
                uint32_t k = 2;
                while (k < size && val_[lits[k]] < 0) k++;
                if (k < size) {  // found another non-false literal to watch
                    std::swap(lits[1], lits[k]);
                    watches_[lits[1]].push_back(Watch{w.ref, other});
                    continue;
                }
                ws[j++] = w;
                if (val_[other] < 0) {  // all literals false
                    while (i < ws.size()) ws[j++] = ws[i++];
                    ws.resize(j);
                    return false;
                }
                assign(other);  // all but one false: forced
            }
            ws.resize(j);
        }
        return true;
    }

    // Order-independent hash, so stored clauses may be permuted by propagate().
    static size_t hashOf(const uint32_t* lits, size_t n) {
        uint64_t h = n;
        for (size_t i = 0; i < n; i++) {
            uint64_t x = (uint64_t(lits[i]) + 1) * 0x9E3779B97F4A7C15ull;
            h += x ^ (x >> 29);
        }
        return size_t(h ^ (h >> 32));
    }

    void hashInsert(uint32_t ref) {
        if (liveClauses_ >= buckets_.size()) {
            buckets_.assign(buckets_.size() * 4, NONE);
            for (uint32_t r = 0; r < ref; r += (arena_[r] & ~DELETED) + 2)
                if (!(arena_[r] & DELETED)) link(r);
        }
        link(ref);
    }
    void link(uint32_t ref) {
        const size_t b = hashOf(&arena_[ref + 2], arena_[ref]) & (buckets_.size() - 1);
        arena_[ref + 1] = buckets_[b];
        buckets_[b] = ref;
    }

    // Reclaims the memory of deleted clauses.  Only called between proof steps,
    // when no temporary assumption is active and T is closed under propagation.
    void compact() {
        std::vector<uint32_t> old;
        old.swap(arena_);
        for (auto& ws : watches_) ws.clear();
        std::fill(buckets_.begin(), buckets_.end(), NONE);
        liveClauses_ = 0;
        garbage_ = 0;
        std::vector<uint32_t> c;
        for (size_t r = 0; r < old.size(); r += (old[r] & ~DELETED) + 2) {
            if (old[r] & DELETED) continue;
            c.assign(old.begin() + r + 2, old.begin() + r + 2 + old[r]);
            addClause(c);  // re-selects watches; drops clauses satisfied by T
        }
    }

    std::vector<int8_t> val_;
    std::vector<uint8_t> mark_;
    std::vector<std::vector<Watch>> watches_;
    std::vector<uint32_t> arena_;  // clause = [size | DELETED][next in hash bucket][literals...]
    std::vector<uint32_t> buckets_;
    std::vector<uint32_t> trail_;
    size_t qhead_ = 0, liveClauses_ = 0, garbage_ = 0;
    bool refuted_ = false;
};

int usage() {
    fprintf(stderr,
            "usage: rup_check [--ignore-deletions] [--assume \"l1 l2 ...\"] <formula.cnf> <proof>\n"
            "  verifies that the proof derives the empty clause from the formula using RUP steps only\n"
            "  --assume            check a refutation of formula ∧ l1 ∧ l2 ∧ ... instead\n"
            "  --ignore-deletions  treat the proof as additions only\n");
    return 2;
}

// Peak resident memory of this process (VmHWM; see src/main.cpp for why not getrusage).
double peakMemMB() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "VmHWM: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb / 1024.0;
}

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char** argv) {
    bool ignoreDeletions = false;
    std::vector<const char*> assumeArgs, files;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ignore-deletions")) ignoreDeletions = true;
        else if (!strcmp(argv[i], "--assume") && i + 1 < argc) assumeArgs.push_back(argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1] == '-') return usage();
        else files.push_back(argv[i]);
    }
    if (files.size() != 2) return usage();

    const double t0 = nowSec();
    Formula f;
    if (!chk::loadCnf(files[0], f)) return 2;
    FILE* proofFile = fopen(files[1], "rb");
    if (!proofFile) {
        fprintf(stderr, "error: cannot read %s\n", files[1]);
        return 2;
    }

    Checker checker(f.numVars);
    std::vector<uint32_t> c;
    for (size_t i = 0; i < f.numClauses(); i++) {
        c.clear();
        for (size_t k = f.start[i]; k < f.start[i + 1]; k++) c.push_back(code(f.lits[k]));
        if (Checker::normalise(c)) checker.addClause(c);
    }
    for (const char* a : assumeArgs) {
        std::vector<int> lits;
        if (!chk::parseLitList(a, f.numVars, lits)) {
            fprintf(stderr, "error: bad --assume list\n");
            return 2;
        }
        for (int l : lits) {
            c.assign(1, code(l));
            checker.addClause(c);
        }
    }

    unsigned long long lemmas = 0, deletions = 0, ignored = 0;
    std::string failure;
    chk::Tokens toks(proofFile);
    std::string tok;
    while (!checker.refuted() && toks.next(tok)) {
        const size_t line = toks.line();
        const bool isDelete = tok == "d";
        if (isDelete && !toks.next(tok)) tok.clear();
        c.clear();
        bool terminated = false;
        do {
            long long x;
            if (!chk::parseInt(tok, x) || x > f.numVars || -x > f.numVars) {
                failure = "line " + std::to_string(line) + ": malformed proof line";
                break;
            }
            if (x == 0) {
                terminated = true;
                break;
            }
            c.push_back(code(int(x)));
        } while (toks.next(tok));
        if (failure.empty() && !terminated) failure = "line " + std::to_string(line) + ": clause not terminated by 0";
        if (!failure.empty()) break;

        const bool tautology = !Checker::normalise(c);
        if (isDelete) {
            if (ignoreDeletions || tautology || !checker.deleteClause(c)) ignored++;
            else deletions++;
            continue;
        }
        lemmas++;
        if (tautology) continue;  // always true; never useful for propagation
        if (!checker.hasRup(c)) {
            failure = "line " + std::to_string(line) + ": lemma " + std::to_string(lemmas) +
                      " is not implied by unit propagation (RUP check failed)";
            break;
        }
        checker.addClause(c);
    }
    if (failure.empty() && !checker.refuted()) failure = "proof ends without deriving the empty clause";

    printf("c formula: %d variables, %zu clauses\n", f.numVars, f.numClauses());
    printf("c lemmas checked: %llu, deletions applied: %llu, deletions ignored: %llu\n", lemmas, deletions, ignored);
    printf("c check time: %.3f s\n", nowSec() - t0);
    printf("c peak memory: %.1f MB\n", peakMemMB());
    if (checker.refuted()) {
        puts("s VERIFIED");
        return 0;
    }
    printf("c %s\n", failure.c_str());
    puts("s NOT VERIFIED");
    return 1;
}
