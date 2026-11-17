// CDCL SAT solver with DRUP proof logging and assumption-based incremental solving.
//
// The public interface is deliberately small:
//
//     Solver s(options);
//     s.init(numVars);
//     s.addClause(lits);  ...          // the formula F (fixed once solving starts)
//     s.solve(assumptions);            // may be called repeatedly
//     s.model() / s.failedAssumptions()
//
// Every clause the solver ever adds to its database beyond F is written to the
// proof stream *before* it is used, so an independent checker can replay the
// run knowing nothing about how the clause was found (see docs/PROOF_FORMAT.md).
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace sat {

// ----------------------------------------------------------------------------
// Literals.  Variable v (0-based) has the positive literal 2v and the negative
// literal 2v+1, so negation is "xor 1" and a literal indexes arrays directly.
// ----------------------------------------------------------------------------
using Lit = uint32_t;
using Var = uint32_t;
constexpr Lit LIT_UNDEF = 0xFFFFFFFFu;

inline constexpr Lit mkLit(Var v, bool negative) { return 2 * v + (negative ? 1u : 0u); }
inline constexpr Lit neg(Lit l) { return l ^ 1u; }
inline constexpr Var var(Lit l) { return l >> 1; }
inline constexpr bool isNeg(Lit l) { return (l & 1u) != 0; }
inline constexpr Lit fromDimacs(int d) { return d > 0 ? mkLit(Var(d - 1), false) : mkLit(Var(-d - 1), true); }
inline constexpr int toDimacs(Lit l) { return isNeg(l) ? -int(var(l) + 1) : int(var(l) + 1); }

enum class Result {
    SAT,                // model() satisfies F and all assumptions
    UNSAT,              // F itself is unsatisfiable (proof ends with the empty clause)
    UNSAT_ASSUMPTIONS,  // F is not refuted, but F ∧ failedAssumptions() is unsatisfiable
    UNKNOWN             // resource limit hit
};

enum class RestartPolicy { NONE, LUBY, EMA };

struct Options {
    // --- features that can be switched off for ablation experiments -----------
    bool minimize = true;        // recursive learnt-clause minimisation
    bool phaseSaving = true;     // remember the last value of each variable
    bool reduceDB = true;        // periodically delete low-quality learnt clauses
    bool blocker = true;         // blocking literal in watchers (skip satisfied clauses)
    bool inlineBinary = true;    // propagate binary clauses from the watch list alone
    bool vsids = true;           // activity-based branching (false: fixed variable order)
    bool simplify = true;        // remove satisfied clauses / false literals at level 0
    bool stable = true;          // alternate focused / stable search modes
    bool targetPhase = true;     // in stable mode, steer towards the best assignment seen
    RestartPolicy restart = RestartPolicy::EMA;
    bool initialPhase = false;   // value tried first for a never-assigned variable
    bool occurrencePhase = false; // ... unless the variable occurs more often with the other sign
    bool trace = false;          // print every decision, propagation, conflict and learnt clause
    // --- resource limits (0 = unlimited) -----------------------------------------
    double timeLimitSec = 0;
    uint64_t conflictLimit = 0;
    uint64_t memLimitMB = 1024;  // soft budget for the clause arena
};

struct Stats {
    uint64_t conflicts = 0, decisions = 0, propagations = 0, restarts = 0;
    uint64_t watchVisits = 0, binaryVisits = 0, blockerSkips = 0;  // work done inside propagate()
    uint64_t reductions = 0, simplifications = 0, garbageCollections = 0;
    uint64_t learntClauses = 0, learntUnits = 0, learntBinaries = 0;
    uint64_t learntLitsBeforeMin = 0, learntLitsAfterMin = 0;
    uint64_t deletedLearnts = 0, removedSatisfied = 0, strengthened = 0;
    uint64_t rephases = 0, modeSwitches = 0;
    uint64_t proofAdded = 0, proofDeleted = 0, proofBytes = 0;
    uint64_t maxArenaWords = 0;
};

// Buffered text writer for the proof stream.
class ProofWriter {
public:
    void open(FILE* f, bool writeDeletions) { file_ = f; deletions_ = writeDeletions; buf_.resize(1 << 16); }
    bool enabled() const { return file_ != nullptr; }
    void add(const Lit* lits, uint32_t n);
    void del(const Lit* lits, uint32_t n);
    void flush();
    uint64_t added = 0, deleted = 0, bytes = 0;

private:
    void putClause(const Lit* lits, uint32_t n);
    void putInt(int x);
    void putChar(char c);
    FILE* file_ = nullptr;
    bool deletions_ = true;
    std::vector<char> buf_;
    size_t pos_ = 0;
};

class Solver {
public:
    explicit Solver(const Options& opt = Options());

    // Declare the number of variables.  Must be called once, before addClause.
    void init(uint32_t numVars);

    // Add a clause of the input formula.  Duplicate literals are merged and
    // tautologies dropped (both are equivalence-preserving).  The vector is
    // used as scratch space.  Only allowed before the first call to solve().
    void addClause(std::vector<Lit>& lits);

    // Proof stream (optional).  Must be set before the first clause is added.
    void setProof(FILE* f, bool writeDeletions = true) { proof_.open(f, writeDeletions); }
    void flushProof() { proof_.flush(); syncProofStats(); }

    // Decide satisfiability of F ∧ assumptions.
    Result solve(const std::vector<Lit>& assumptions = {});

    // After SAT: value of every variable (+1 true / -1 false).
    const std::vector<int8_t>& model() const { return model_; }
    // After UNSAT_ASSUMPTIONS: a subset A' of the assumptions with F ∧ A' unsatisfiable.
    const std::vector<Lit>& failedAssumptions() const { return core_; }

    uint32_t numVars() const { return numVars_; }
    const Stats& stats() const { return stats_; }
    size_t numOriginalClauses() const { return clauses_.size(); }
    size_t numLearntClauses() const { return learnts_.size(); }

private:
    using CRef = uint32_t;                       // offset of a clause in arena_
    static constexpr CRef CREF_UNDEF = 0xFFFFFFFFu;
    static constexpr uint32_t BIN_TAG = 0x80000000u;

    // A watcher for literal l lives in watches_[l] and is visited when l becomes
    // false.  `blocker` is some other literal of the clause: if it is true the
    // clause is satisfied and need not be touched.  For binary clauses the
    // blocker is *the* other literal and ref carries BIN_TAG.
    struct Watch {
        Lit blocker;
        uint32_t ref;
    };

    // Clause memory layout in arena_:  [size][flags | lbd << 8][lit 0][lit 1]...
    // Invariant for clauses of size >= 3: the two watched literals are lit 0, lit 1.
    static constexpr uint32_t HEADER = 2;
    static constexpr uint32_t F_LEARNT = 1, F_GARBAGE = 2, F_RELOC = 4;
    static constexpr uint32_t USED_SHIFT = 3, USED_MASK = 3u << USED_SHIFT, LBD_SHIFT = 8;

    uint32_t& csize(CRef r) { return arena_[r]; }
    uint32_t& cflags(CRef r) { return arena_[r + 1]; }
    Lit* clits(CRef r) { return &arena_[r + HEADER]; }
    static uint32_t lbdOf(uint32_t flags) { return flags >> LBD_SHIFT; }

    CRef allocClause(const Lit* lits, uint32_t n, bool learnt, uint32_t lbd);
    void attachClause(CRef r);
    void markGarbage(CRef r);

    int decisionLevel() const { return int(trailLim_.size()); }
    int8_t value(Lit l) const { return vals_[l]; }
    void enqueue(Lit p, CRef from);
    void newDecisionLevel() { trailLim_.push_back(trailSize_); }
    void cancelUntil(int level);
    CRef propagate();

    void analyze(CRef confl, int& btLevel, uint32_t& lbd);
    bool litRedundant(Lit p, uint32_t abstractLevels);
    void analyzeFinal(Lit p);
    uint32_t computeLBD(const Lit* lits, uint32_t n);
    uint32_t abstractLevel(Var v) const { return 1u << (level_[v] & 31); }

    Result search();
    Lit pickBranchLit();
    void topLevelBookkeeping();
    void simplify();
    void reduceDB();
    void rebuildWatches();
    void cleanWatches();
    void garbageCollectIfNeeded();
    bool restartDue();
    void restart();
    void rephase();
    void switchMode();
    void updateTarget();
    void refute();  // conflict at level 0: emit the empty clause
    bool limitReached();
    void syncProofStats();
    void traceLits(const char* what, const Lit* lits, size_t n) const;
    Lit* reasonLits(CRef r, Lit implied, uint32_t& size);

    // activity heap
    void bumpVar(Var v);
    void heapInsert(Var v);
    Var heapPop();
    void heapUp(uint32_t i);
    void heapDown(uint32_t i);
    bool heapLess(Var a, Var b) const { return activity_[a] > activity_[b] || (activity_[a] == activity_[b] && a < b); }

    Options opt_;
    Stats stats_;
    ProofWriter proof_;

    uint32_t numVars_ = 0;
    bool ok_ = true;        // false once F has been refuted
    bool started_ = false;  // solve() has been called

    std::vector<uint32_t> arena_;
    uint64_t wasted_ = 0;
    std::vector<CRef> clauses_, learnts_;
    std::vector<std::vector<Watch>> watches_;

    std::vector<int8_t> vals_;   // per literal: +1 true, -1 false, 0 unassigned
    std::vector<int> level_;     // per variable
    std::vector<CRef> reason_;   // per variable; CREF_UNDEF for decisions / units
    std::vector<Lit> trail_;
    uint32_t trailSize_ = 0, qhead_ = 0;
    std::vector<uint32_t> trailLim_;
    uint32_t proofUnits_ = 0;    // level-0 trail prefix already handled by topLevelBookkeeping

    std::vector<double> activity_;
    double varInc_ = 1.0;
    std::vector<Var> heap_;
    std::vector<int> heapPos_;
    std::vector<uint8_t> phase_;        // saved phase: 1 = assign the negative literal
    std::vector<uint8_t> origPhase_;    // phase used for a variable that was never assigned
    std::vector<uint8_t> targetPhase_;  // 0/1 as phase_, 2 = none
    std::vector<uint8_t> bestPhase_;
    uint32_t targetAssigned_ = 0, bestAssigned_ = 0;

    // conflict analysis scratch
    std::vector<uint8_t> seen_;
    std::vector<Lit> learnt_;
    std::vector<Var> toClear_;
    struct Frame { Lit lit; uint32_t next; };
    std::vector<Frame> minStack_;
    std::vector<uint64_t> lbdStamp_;
    uint64_t lbdCounter_ = 0;

    // restart / reduce / mode scheduling
    struct Ema {
        double alpha, biased = 0, correction = 1;
        explicit Ema(double a) : alpha(a) {}
        void update(double x) { biased += alpha * (x - biased); correction *= 1 - alpha; }
        double value() const { return correction < 1 ? biased / (1 - correction) : 0; }
    };
    Ema lbdFast_{3e-2}, lbdSlow_{1e-5};
    uint64_t conflictsAtRestart_ = 0;
    uint64_t lubyIndex_ = 0, restartLimit_ = 0;
    uint64_t nextReduce_ = 2000, reduceInterval_ = 2000;
    bool stableMode_ = false;
    uint64_t nextModeSwitch_ = 1000, modePhaseLength_ = 1000;
    uint64_t nextRephase_ = 1000, rephaseCount_ = 0;
    uint64_t rngState_ = 0x9E3779B97F4A7C15ull;
    uint32_t simplifyTrail_ = 0;        // trail size at the last simplify()
    uint64_t nextSimplifyProps_ = 0;    // amortisation: no simplify() before this many propagations
    uint64_t conflictBudgetEnd_ = 0;
    double startTime_ = 0;

    std::vector<Lit> assumptions_;
    std::vector<int8_t> model_;
    std::vector<Lit> core_;
};

}  // namespace sat
