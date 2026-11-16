// CDCL SAT solver: implementation.  See docs/DESIGN.md for the reasoning behind
// each component and docs/CORRECTNESS.md for the invariants referred to below.
#include "solver.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <stdexcept>

namespace sat {

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// The Luby sequence 1,1,2,1,1,2,4,1,1,2,1,1,2,4,8,...  (i >= 1)
static uint64_t luby(uint64_t i) {
    for (;;) {
        uint64_t k = 1;
        while (((1ull << k) - 1) < i) k++;
        if (i == (1ull << k) - 1) return 1ull << (k - 1);
        i -= (1ull << (k - 1)) - 1;
    }
}

// ============================================================================
// Proof stream
// ============================================================================

void ProofWriter::putChar(char c) {
    if (pos_ == buf_.size()) flush();
    buf_[pos_++] = c;
    bytes++;
}

void ProofWriter::putInt(int x) {
    char tmp[12];
    int n = 0;
    unsigned u = x < 0 ? 0u - unsigned(x) : unsigned(x);
    if (x < 0) putChar('-');
    do {
        tmp[n++] = char('0' + u % 10);
        u /= 10;
    } while (u);
    while (n) putChar(tmp[--n]);
}

void ProofWriter::putClause(const Lit* lits, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        putInt(toDimacs(lits[i]));
        putChar(' ');
    }
    putChar('0');
    putChar('\n');
}

void ProofWriter::add(const Lit* lits, uint32_t n) {
    if (!file_) return;
    added++;
    putClause(lits, n);
}

void ProofWriter::del(const Lit* lits, uint32_t n) {
    if (!file_ || !deletions_) return;
    deleted++;
    putChar('d');
    putChar(' ');
    putClause(lits, n);
}

void ProofWriter::flush() {
    if (!file_) return;
    if (pos_ && fwrite(buf_.data(), 1, pos_, file_) != pos_) {
        // A truncated proof must never be mistaken for a complete one.
        fprintf(stderr, "error: failed to write proof file\n");
        exit(1);
    }
    pos_ = 0;
    fflush(file_);
}

// ============================================================================
// Construction
// ============================================================================

Solver::Solver(const Options& opt) : opt_(opt) {}

void Solver::init(uint32_t n) {
    numVars_ = n;
    watches_.assign(2 * size_t(n), {});
    vals_.assign(2 * size_t(n), 0);
    level_.assign(n, 0);
    reason_.assign(n, CREF_UNDEF);
    trail_.assign(n, 0);
    activity_.assign(n, 0.0);
    phase_.assign(n, opt_.initialPhase ? 0 : 1);
    targetPhase_.assign(n, 2);
    bestPhase_.assign(n, 2);
    seen_.assign(n, 0);
    lbdStamp_.assign(size_t(n) + 1, 0);
    // All activities are equal, so 0,1,...,n-1 already is a valid heap.
    heap_.resize(n);
    heapPos_.resize(n);
    for (Var v = 0; v < n; v++) {
        heap_[v] = v;
        heapPos_[v] = int(v);
    }
    restartLimit_ = opt_.restart == RestartPolicy::LUBY ? 100 : 1024;
    lubyIndex_ = 1;
}

void Solver::refute() {
    if (!ok_) return;
    ok_ = false;
    proof_.add(nullptr, 0);  // the empty clause
}

void Solver::addClause(std::vector<Lit>& lits) {
    assert(!started_);
    if (!ok_) return;
    // Normalise: sorting puts l and ¬l next to each other (codes 2v and 2v+1).
    std::sort(lits.begin(), lits.end());
    size_t j = 0;
    for (size_t i = 0; i < lits.size(); i++) {
        if (j > 0 && lits[i] == lits[j - 1]) continue;       // duplicate literal: x ∨ x ≡ x
        if (j > 0 && lits[i] == neg(lits[j - 1])) return;    // tautology: always true, drop it
        lits[j++] = lits[i];
    }
    lits.resize(j);
    if (j == 0) {
        refute();
    } else if (j == 1) {
        Lit p = lits[0];
        if (value(p) < 0) refute();
        else if (value(p) == 0) enqueue(p, CREF_UNDEF);
    } else {
        CRef r = allocClause(lits.data(), uint32_t(j), false, 0);
        clauses_.push_back(r);
        attachClause(r);
    }
}

Solver::CRef Solver::allocClause(const Lit* lits, uint32_t n, bool learnt, uint32_t lbd) {
    if (arena_.size() + HEADER + n >= BIN_TAG) throw std::runtime_error("clause arena exceeds 2^31 words");
    CRef r = CRef(arena_.size());
    arena_.push_back(n);
    arena_.push_back((learnt ? F_LEARNT : 0u) | (lbd << LBD_SHIFT));
    arena_.insert(arena_.end(), lits, lits + n);
    if (arena_.size() > stats_.maxArenaWords) stats_.maxArenaWords = arena_.size();
    return r;
}

void Solver::attachClause(CRef r) {
    const Lit* l = clits(r);
    const uint32_t tag = (csize(r) == 2 && opt_.inlineBinary) ? BIN_TAG : 0u;
    watches_[l[0]].push_back(Watch{l[1], r | tag});
    watches_[l[1]].push_back(Watch{l[0], r | tag});
}

void Solver::markGarbage(CRef r) {
    cflags(r) |= F_GARBAGE;
    wasted_ += HEADER + csize(r);
}

// ============================================================================
// Assignment trail
// ============================================================================

inline void Solver::enqueue(Lit p, CRef from) {
    vals_[p] = 1;
    vals_[p ^ 1] = -1;
    const Var v = var(p);
    level_[v] = decisionLevel();
    reason_[v] = from;
    trail_[trailSize_++] = p;
}

// Undo every assignment above `level`.  Because learnt clauses are never
// retracted here, nothing that was *derived* is lost: only guesses are.
void Solver::cancelUntil(int level) {
    if (decisionLevel() <= level) return;
    const uint32_t lim = trailLim_[level];
    for (uint32_t i = trailSize_; i-- > lim;) {
        const Lit p = trail_[i];
        const Var v = var(p);
        vals_[p] = 0;
        vals_[p ^ 1] = 0;
        if (opt_.phaseSaving) phase_[v] = isNeg(p);
        if (heapPos_[v] < 0) heapInsert(v);
    }
    trailSize_ = lim;
    qhead_ = lim;
    trailLim_.resize(level);
}

// ============================================================================
// Unit propagation with two watched literals.
//
// Invariant (W): for every clause of size >= 2, if a watched literal is false
// then either the other watched literal is true, or the false literal's
// assignment is still waiting in the queue trail_[qhead_..).
// Consequently, when the queue is empty no clause is falsified and no clause
// is unit without its remaining literal being assigned true.
// ============================================================================

Solver::CRef Solver::propagate() {
    CRef confl = CREF_UNDEF;
    const bool useBlocker = opt_.blocker;
    const int8_t* vals = vals_.data();
    while (qhead_ < trailSize_) {
        const Lit p = trail_[qhead_++];
        const Lit fl = p ^ 1;  // the literal that just became false
        std::vector<Watch>& ws = watches_[fl];
        Watch* i = ws.data();
        Watch* j = i;
        Watch* const end = i + ws.size();
        stats_.propagations++;
        while (i != end) {
            const Watch w = *i++;
            const int8_t bv = vals[w.blocker];
            if (w.ref & BIN_TAG) {
                // Binary clause (fl ∨ blocker): decided without touching clause memory.
                *j++ = w;
                if (bv > 0) continue;
                if (bv < 0) {
                    confl = w.ref & ~BIN_TAG;
                    break;
                }
                enqueue(w.blocker, w.ref & ~BIN_TAG);
                continue;
            }
            if (useBlocker && bv > 0) {  // clause already satisfied
                *j++ = w;
                continue;
            }
            uint32_t* c = &arena_[w.ref];
            Lit* lits = c + HEADER;
            const Lit other = lits[0] ^ lits[1] ^ fl;  // the other watched literal
            const int8_t ov = vals[other];
            if (ov > 0) {
                *j++ = Watch{other, w.ref};
                continue;
            }
            // Look for a non-false literal to watch instead of fl.
            const uint32_t size = c[0];
            uint32_t k = 2;
            while (k < size && vals[lits[k]] < 0) k++;
            if (k < size) {
                lits[0] = other;
                lits[1] = lits[k];
                lits[k] = fl;
                watches_[lits[1]].push_back(Watch{other, w.ref});
                continue;  // this watcher leaves the list of fl
            }
            // Every literal except `other` is false: the clause is unit or conflicting.
            lits[0] = other;
            lits[1] = fl;
            *j++ = Watch{other, w.ref};
            if (ov < 0) {
                confl = w.ref;
                break;
            }
            enqueue(other, w.ref);
        }
        if (confl != CREF_UNDEF) {
            while (i != end) *j++ = *i++;
            ws.resize(size_t(j - ws.data()));
            qhead_ = trailSize_;
            break;
        }
        ws.resize(size_t(j - ws.data()));
    }
    return confl;
}

// ============================================================================
// Conflict analysis
// ============================================================================

// Literals of the clause that forced `implied`; the implied literal is moved to
// position 0 (binary clauses are propagated without reordering them).
inline Lit* Solver::reasonLits(CRef r, Lit implied, uint32_t& size) {
    Lit* l = clits(r);
    size = csize(r);
    if (size == 2 && l[0] != implied) std::swap(l[0], l[1]);
    assert(l[0] == implied);
    return l;
}

uint32_t Solver::computeLBD(const Lit* lits, uint32_t n) {
    lbdCounter_++;
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        const int lv = level_[var(lits[i])];
        if (lv > 0 && lbdStamp_[lv] != lbdCounter_) {
            lbdStamp_[lv] = lbdCounter_;
            count++;
        }
    }
    return count;
}

// First-UIP learning.  Starting from the falsified clause, repeatedly resolve
// with the reason of the most recently assigned literal of the current
// decision level until exactly one literal of that level is left.
// Result: learnt_ (asserting literal first), backtrack level, LBD.
void Solver::analyze(CRef confl, int& btLevel, uint32_t& lbd) {
    learnt_.clear();
    learnt_.push_back(LIT_UNDEF);  // slot for the asserting literal
    const int curLevel = decisionLevel();
    int open = 0;  // literals of the current level still to be resolved
    Lit p = LIT_UNDEF;
    uint32_t idx = trailSize_;
    for (;;) {
        uint32_t size;
        Lit* lits;
        if (p == LIT_UNDEF) {
            lits = clits(confl);
            size = csize(confl);
        } else {
            lits = reasonLits(confl, p, size);
        }
        uint32_t& flags = cflags(confl);
        if (flags & F_LEARNT) {
            // This clause was useful: protect it from the next reductions and
            // refresh its LBD (it can only be kept if it got smaller).
            uint32_t oldLbd = lbdOf(flags);
            if (oldLbd > 2) {
                uint32_t nl = computeLBD(lits, size);
                if (nl < oldLbd) oldLbd = nl;
            }
            const uint32_t used = oldLbd <= 6 ? 2u : 1u;
            flags = (flags & (F_LEARNT | F_GARBAGE | F_RELOC)) | (used << USED_SHIFT) | (oldLbd << LBD_SHIFT);
        }
        for (uint32_t k = (p == LIT_UNDEF ? 0 : 1); k < size; k++) {
            const Lit q = lits[k];
            const Var v = var(q);
            if (seen_[v] || level_[v] == 0) continue;  // level-0 literals are false for good
            seen_[v] = 1;
            if (opt_.vsids) bumpVar(v);
            if (level_[v] >= curLevel) open++;
            else learnt_.push_back(q);
        }
        // Next literal of the current level to resolve on.
        do {
            idx--;
        } while (!seen_[var(trail_[idx])]);
        p = trail_[idx];
        confl = reason_[var(p)];
        seen_[var(p)] = 0;
        if (--open == 0) break;
    }
    learnt_[0] = neg(p);
    stats_.learntLitsBeforeMin += learnt_.size();

    // Minimisation: drop literals implied by the remaining ones.  toClear_
    // collects every variable whose seen_ mark must be reset afterwards
    // (the clause is compacted in place, so it cannot serve as that record).
    toClear_.clear();
    for (size_t i = 1; i < learnt_.size(); i++) toClear_.push_back(var(learnt_[i]));
    size_t keep = learnt_.size();
    if (opt_.minimize && learnt_.size() > 1) {
        uint32_t abstractLevels = 0;
        for (size_t i = 1; i < learnt_.size(); i++) abstractLevels |= abstractLevel(var(learnt_[i]));
        keep = 1;
        for (size_t i = 1; i < learnt_.size(); i++) {
            const Lit q = learnt_[i];
            if (reason_[var(q)] == CREF_UNDEF || !litRedundant(q, abstractLevels)) learnt_[keep++] = q;
        }
    }
    for (Var v : toClear_) seen_[v] = 0;
    learnt_.resize(keep);
    stats_.learntLitsAfterMin += keep;

    // Backtrack to the second-highest level in the clause; keep a literal of
    // that level in position 1 so that both watches are the last to be undone.
    btLevel = 0;
    if (learnt_.size() > 1) {
        size_t maxI = 1;
        for (size_t i = 2; i < learnt_.size(); i++)
            if (level_[var(learnt_[i])] > level_[var(learnt_[maxI])]) maxI = i;
        std::swap(learnt_[1], learnt_[maxI]);
        btLevel = level_[var(learnt_[1])];
    }
    lbd = computeLBD(learnt_.data(), uint32_t(learnt_.size()));
}

// Is the (false) literal p of the learnt clause implied by the other literals
// of the clause?  Depth-first search through the implication graph.
// seen_ values: 1 = literal of the learnt clause, 2 = proven redundant,
// 3 = proven not redundant.
bool Solver::litRedundant(Lit p, uint32_t abstractLevels) {
    minStack_.clear();
    minStack_.push_back(Frame{p, 1});
    while (!minStack_.empty()) {
        Frame& f = minStack_.back();
        uint32_t size;
        const Lit* lits = reasonLits(reason_[var(f.lit)], neg(f.lit), size);
        if (f.next < size) {
            const Lit q = lits[f.next++];
            const Var v = var(q);
            if (level_[v] == 0 || seen_[v] == 1 || seen_[v] == 2) continue;
            if (reason_[v] == CREF_UNDEF || seen_[v] == 3 || (abstractLevel(v) & abstractLevels) == 0) {
                // q is not implied by the clause, so neither is anything on the stack.
                for (const Frame& g : minStack_) {
                    const Var u = var(g.lit);
                    if (seen_[u] == 0) {
                        seen_[u] = 3;
                        toClear_.push_back(u);
                    }
                }
                return false;
            }
            minStack_.push_back(Frame{q, 1});
        } else {
            const Var v = var(f.lit);
            if (seen_[v] == 0) {
                seen_[v] = 2;
                toClear_.push_back(v);
            }
            minStack_.pop_back();
        }
    }
    return true;
}

// Called when assumption a = ¬p is already false.  Walks the implication graph
// backwards from p and collects the assumptions (decisions) it depends on.
// The clause (p ∨ ¬a_1 ∨ ... ∨ ¬a_k) is implied by F alone and is RUP.
void Solver::analyzeFinal(Lit p) {
    learnt_.clear();
    learnt_.push_back(p);
    if (decisionLevel() > 0 && level_[var(p)] > 0) {
        seen_[var(p)] = 1;
        for (uint32_t i = trailSize_; i-- > trailLim_[0];) {
            const Lit t = trail_[i];
            const Var v = var(t);
            if (!seen_[v]) continue;
            seen_[v] = 0;
            if (reason_[v] == CREF_UNDEF) {
                learnt_.push_back(neg(t));  // an assumption
            } else {
                uint32_t size;
                const Lit* lits = reasonLits(reason_[v], t, size);
                for (uint32_t k = 1; k < size; k++)
                    if (level_[var(lits[k])] > 0) seen_[var(lits[k])] = 1;
            }
        }
    }
    proof_.add(learnt_.data(), uint32_t(learnt_.size()));
    core_.clear();
    for (Lit l : learnt_) core_.push_back(neg(l));
}

// ============================================================================
// Branching heuristic: exponential VSIDS on a binary heap
// ============================================================================

void Solver::bumpVar(Var v) {
    if ((activity_[v] += varInc_) > 1e100) {
        for (double& a : activity_) a *= 1e-100;
        varInc_ *= 1e-100;
    }
    if (heapPos_[v] >= 0) heapUp(uint32_t(heapPos_[v]));
}

void Solver::heapUp(uint32_t i) {
    const Var x = heap_[i];
    while (i > 0) {
        const uint32_t parent = (i - 1) >> 1;
        if (!heapLess(x, heap_[parent])) break;
        heap_[i] = heap_[parent];
        heapPos_[heap_[i]] = int(i);
        i = parent;
    }
    heap_[i] = x;
    heapPos_[x] = int(i);
}

void Solver::heapDown(uint32_t i) {
    const Var x = heap_[i];
    const uint32_t n = uint32_t(heap_.size());
    for (;;) {
        uint32_t child = 2 * i + 1;
        if (child >= n) break;
        if (child + 1 < n && heapLess(heap_[child + 1], heap_[child])) child++;
        if (!heapLess(heap_[child], x)) break;
        heap_[i] = heap_[child];
        heapPos_[heap_[i]] = int(i);
        i = child;
    }
    heap_[i] = x;
    heapPos_[x] = int(i);
}

void Solver::heapInsert(Var v) {
    heapPos_[v] = int(heap_.size());
    heap_.push_back(v);
    heapUp(uint32_t(heap_.size() - 1));
}

Var Solver::heapPop() {
    const Var top = heap_[0];
    heapPos_[top] = -1;
    const Var last = heap_.back();
    heap_.pop_back();
    if (!heap_.empty()) {
        heap_[0] = last;
        heapPos_[last] = 0;
        heapDown(0);
    }
    return top;
}

Lit Solver::pickBranchLit() {
    for (;;) {
        if (heap_.empty()) return LIT_UNDEF;
        const Var v = heapPop();
        if (vals_[2 * v] != 0) continue;
        uint8_t ph = phase_[v];
        if (stableMode_ && opt_.targetPhase && targetPhase_[v] != 2) ph = targetPhase_[v];
        return mkLit(v, ph != 0);
    }
}

// ============================================================================
// Clause database maintenance
// ============================================================================

// Runs whenever propagation at level 0 has finished without conflict.
void Solver::topLevelBookkeeping() {
    // Every literal fixed at level 0 becomes an explicit unit lemma, so that the
    // proof does not depend on its reason clause surviving in the checker.
    for (; proofUnits_ < trailSize_; proofUnits_++) {
        const Lit p = trail_[proofUnits_];
        const Var v = var(p);
        if (reason_[v] != CREF_UNDEF) {
            proof_.add(&p, 1);
            reason_[v] = CREF_UNDEF;
        }
    }
    if (opt_.simplify && trailSize_ > simplifyTrail_ && stats_.propagations >= nextSimplifyProps_) simplify();
}

// Level-0 simplification.  Removing a clause containing a true literal and
// removing false literals from a clause both preserve logical equivalence
// given the unit clauses on the level-0 trail.
void Solver::simplify() {
    assert(decisionLevel() == 0 && qhead_ == trailSize_);
    stats_.simplifications++;
    std::vector<Lit> kept;
    for (std::vector<CRef>* list : {&clauses_, &learnts_}) {
        size_t j = 0;
        for (const CRef r : *list) {
            const uint32_t n = csize(r);
            Lit* l = clits(r);
            bool satisfied = false;
            kept.clear();
            for (uint32_t k = 0; k < n; k++) {
                const int8_t v = value(l[k]);
                if (v > 0) {
                    satisfied = true;
                    break;
                }
                if (v == 0) kept.push_back(l[k]);
            }
            if (satisfied) {
                proof_.del(l, n);
                markGarbage(r);
                stats_.removedSatisfied++;
                continue;
            }
            if (kept.size() < n) {
                proof_.add(kept.data(), uint32_t(kept.size()));  // RUP: the dropped literals are false units
                proof_.del(l, n);
                stats_.strengthened++;
                if (kept.size() < 2) {
                    // Cannot happen after complete propagation (invariant W); handled
                    // anyway so that a bug elsewhere cannot turn into a wrong answer.
                    markGarbage(r);
                    if (kept.empty()) refute();
                    else if (value(kept[0]) == 0) enqueue(kept[0], CREF_UNDEF);
                    continue;
                }
                std::copy(kept.begin(), kept.end(), l);
                csize(r) = uint32_t(kept.size());
                wasted_ += n - kept.size();
            }
            (*list)[j++] = r;
        }
        list->resize(j);
    }
    simplifyTrail_ = trailSize_;
    for (auto& ws : watches_) ws.clear();
    garbageCollectIfNeeded();
    rebuildWatches();
    nextSimplifyProps_ = stats_.propagations + (arena_.size() - wasted_);
}

void Solver::rebuildWatches() {
    for (auto& ws : watches_) ws.clear();
    for (const CRef r : clauses_) attachClause(r);
    for (const CRef r : learnts_) attachClause(r);
}

// Delete the less useful half of the learnt clauses.  Learnt clauses are
// consequences of F, so deleting any of them preserves equivalence; clauses
// that are currently the reason of an assignment are kept because conflict
// analysis may still need to read them.
void Solver::reduceDB() {
    stats_.reductions++;
    std::vector<CRef> candidates;
    for (const CRef r : learnts_) {
        const uint32_t flags = cflags(r);
        if (csize(r) == 2 || lbdOf(flags) <= 2) continue;  // cheap or very good: keep
        if (flags & USED_MASK) {                           // used recently: keep for now
            cflags(r) = flags - (1u << USED_SHIFT);
            continue;
        }
        const Lit l0 = clits(r)[0];
        if (value(l0) > 0 && reason_[var(l0)] == r) continue;  // locked
        candidates.push_back(r);
    }
    std::sort(candidates.begin(), candidates.end(), [&](CRef a, CRef b) {
        const uint32_t la = lbdOf(cflags(a)), lb = lbdOf(cflags(b));
        if (la != lb) return la > lb;
        if (csize(a) != csize(b)) return csize(a) > csize(b);
        return a < b;
    });
    const size_t toDelete = candidates.size() / 2;
    for (size_t i = 0; i < toDelete; i++) {
        proof_.del(clits(candidates[i]), csize(candidates[i]));
        markGarbage(candidates[i]);
    }
    stats_.deletedLearnts += toDelete;
    size_t j = 0;
    for (const CRef r : learnts_)
        if (!(cflags(r) & F_GARBAGE)) learnts_[j++] = r;
    learnts_.resize(j);
    cleanWatches();
    garbageCollectIfNeeded();
    reduceInterval_ += 300;
    nextReduce_ = stats_.conflicts + reduceInterval_;
}

void Solver::cleanWatches() {
    for (auto& ws : watches_) {
        size_t j = 0;
        for (const Watch& w : ws)
            if (!(arena_[(w.ref & ~BIN_TAG) + 1] & F_GARBAGE)) ws[j++] = w;
        ws.resize(j);
    }
}

// Compact the arena once a quarter of it is garbage.  No watcher may refer to
// a garbage clause when this is called.
void Solver::garbageCollectIfNeeded() {
    if (wasted_ * 4 < arena_.size()) return;
    stats_.garbageCollections++;
    std::vector<uint32_t> fresh;
    fresh.reserve(arena_.size() - wasted_);
    for (std::vector<CRef>* list : {&clauses_, &learnts_}) {
        for (CRef& r : *list) {
            const CRef moved = CRef(fresh.size());
            fresh.insert(fresh.end(), arena_.begin() + r, arena_.begin() + r + HEADER + csize(r));
            cflags(r) |= F_RELOC;
            arena_[r] = moved;  // forwarding address
            r = moved;
        }
    }
    for (auto& ws : watches_)
        for (Watch& w : ws) {
            const CRef old = w.ref & ~BIN_TAG;
            assert(arena_[old + 1] & F_RELOC);
            w.ref = arena_[old] | (w.ref & BIN_TAG);
        }
    for (uint32_t i = 0; i < trailSize_; i++) {
        CRef& r = reason_[var(trail_[i])];
        if (r != CREF_UNDEF) {
            assert(arena_[r + 1] & F_RELOC);
            r = arena_[r];
        }
    }
    arena_.swap(fresh);
    wasted_ = 0;
}

// ============================================================================
// Restarts, search modes, phases
// ============================================================================

bool Solver::restartDue() {
    if (opt_.restart == RestartPolicy::NONE) return false;
    const uint64_t since = stats_.conflicts - conflictsAtRestart_;
    if (opt_.restart == RestartPolicy::LUBY || stableMode_) return since >= restartLimit_;
    // Focused mode: restart when recent learnt clauses are worse (higher LBD)
    // than the long-term average, i.e. the search is in an unproductive region.
    return since >= 2 && lbdFast_.value() > 1.1 * lbdSlow_.value();
}

void Solver::restart() {
    stats_.restarts++;
    conflictsAtRestart_ = stats_.conflicts;
    if (opt_.restart == RestartPolicy::LUBY) restartLimit_ = 100 * luby(++lubyIndex_);
    else if (stableMode_) restartLimit_ = 1024 * luby(++lubyIndex_);
    cancelUntil(0);
    if (opt_.stable && opt_.targetPhase && opt_.phaseSaving && stats_.conflicts >= nextRephase_) rephase();
}

void Solver::switchMode() {
    stableMode_ = !stableMode_;
    stats_.modeSwitches++;
    if (stableMode_) {
        lubyIndex_ = 1;
        restartLimit_ = 1024;
    } else {
        modePhaseLength_ *= 2;
    }
    conflictsAtRestart_ = stats_.conflicts;
    nextModeSwitch_ = stats_.conflicts + modePhaseLength_;
}

// Remember the largest conflict-free partial assignment seen so far.
void Solver::updateTarget() {
    const uint32_t safe = trailLim_.back();  // everything below the conflicting level
    if (safe > targetAssigned_) {
        for (uint32_t i = 0; i < safe; i++) targetPhase_[var(trail_[i])] = isNeg(trail_[i]);
        targetAssigned_ = safe;
    }
    if (safe > bestAssigned_) {
        for (uint32_t i = 0; i < safe; i++) bestPhase_[var(trail_[i])] = isNeg(trail_[i]);
        bestAssigned_ = safe;
    }
}

// Periodically reset the saved phases (original, inverted, best, random,
// best, flipped, ...) to escape from an unlucky region of the search space.
void Solver::rephase() {
    stats_.rephases++;
    const uint64_t k = rephaseCount_++;
    const char kind = k == 0 ? 'O' : k == 1 ? 'I' : "BRBF"[(k - 2) % 4];
    const uint8_t original = opt_.initialPhase ? 0 : 1;
    for (Var v = 0; v < numVars_; v++) {
        switch (kind) {
            case 'O': phase_[v] = original; break;
            case 'I': phase_[v] = original ^ 1; break;
            case 'B': if (bestPhase_[v] != 2) phase_[v] = bestPhase_[v]; break;
            case 'R':
                rngState_ ^= rngState_ << 13;
                rngState_ ^= rngState_ >> 7;
                rngState_ ^= rngState_ << 17;
                phase_[v] = uint8_t(rngState_ & 1);
                break;
            default: phase_[v] ^= 1; break;
        }
        targetPhase_[v] = 2;
    }
    targetAssigned_ = 0;
    if (kind == 'B') bestAssigned_ = 0;
    nextRephase_ = stats_.conflicts + 1000 * (rephaseCount_ + 1);
}

bool Solver::limitReached() {
    if (opt_.conflictLimit && stats_.conflicts >= conflictBudgetEnd_) return true;
    if (opt_.timeLimitSec > 0 && nowSec() - startTime_ > opt_.timeLimitSec) return true;
    return false;
}

void Solver::syncProofStats() {
    stats_.proofAdded = proof_.added;
    stats_.proofDeleted = proof_.deleted;
    stats_.proofBytes = proof_.bytes;
}

// ============================================================================
// Main loop
// ============================================================================

Result Solver::search() {
    const uint64_t arenaBudget = opt_.memLimitMB * (1024 * 1024 / 4) / 2;  // words; half the budget
    for (;;) {
        const CRef confl = propagate();
        if (confl != CREF_UNDEF) {
            stats_.conflicts++;
            if (decisionLevel() == 0) {
                // A conflict that depends on no decision: F is unsatisfiable.
                refute();
                return Result::UNSAT;
            }
            if (opt_.stable && opt_.targetPhase) updateTarget();
            int btLevel;
            uint32_t lbd;
            analyze(confl, btLevel, lbd);
            cancelUntil(btLevel);
            proof_.add(learnt_.data(), uint32_t(learnt_.size()));  // logged before it is ever used
            stats_.learntClauses++;
            if (learnt_.size() == 1) {
                stats_.learntUnits++;
                enqueue(learnt_[0], CREF_UNDEF);
            } else {
                if (learnt_.size() == 2) stats_.learntBinaries++;
                const CRef r = allocClause(learnt_.data(), uint32_t(learnt_.size()), true, lbd);
                learnts_.push_back(r);
                attachClause(r);
                enqueue(learnt_[0], r);
            }
            varInc_ *= 1.0 / 0.95;
            lbdFast_.update(lbd);
            lbdSlow_.update(lbd);
            if ((stats_.conflicts & 255) == 0 && limitReached()) return Result::UNKNOWN;
            continue;
        }

        if (decisionLevel() == 0) {
            topLevelBookkeeping();
            if (!ok_) return Result::UNSAT;
            if (qhead_ < trailSize_) continue;
        }
        if (restartDue()) {
            restart();
            continue;
        }
        if (opt_.reduceDB && stats_.conflicts >= nextReduce_) {
            reduceDB();
            // Under memory pressure reduce more often instead of letting the interval grow.
            if (arena_.size() > arenaBudget) nextReduce_ = stats_.conflicts + 500;
        }
        if (arena_.size() > 2 * arenaBudget) throw std::runtime_error("memory limit exceeded");
        if (opt_.stable && opt_.restart == RestartPolicy::EMA && stats_.conflicts >= nextModeSwitch_) switchMode();

        // Assumptions occupy the first decision levels, one level each.
        Lit next = LIT_UNDEF;
        while (decisionLevel() < int(assumptions_.size())) {
            const Lit a = assumptions_[size_t(decisionLevel())];
            if (value(a) > 0) {
                newDecisionLevel();  // already true: empty level keeps the indexing simple
            } else if (value(a) < 0) {
                analyzeFinal(neg(a));
                return Result::UNSAT_ASSUMPTIONS;
            } else {
                next = a;
                break;
            }
        }
        if (next == LIT_UNDEF) {
            next = pickBranchLit();
            if (next == LIT_UNDEF) {
                // Every variable is assigned and propagation found no conflict:
                // by invariant (W) no clause is falsified.
                model_.resize(numVars_);
                for (Var v = 0; v < numVars_; v++) model_[v] = vals_[2 * v];
                return Result::SAT;
            }
            stats_.decisions++;
            if ((stats_.decisions & 4095) == 0 && limitReached()) return Result::UNKNOWN;
        }
        newDecisionLevel();
        enqueue(next, CREF_UNDEF);
    }
}

Result Solver::solve(const std::vector<Lit>& assumptions) {
    started_ = true;
    model_.clear();
    core_.clear();
    startTime_ = nowSec();
    conflictBudgetEnd_ = stats_.conflicts + opt_.conflictLimit;
    Result r = Result::UNSAT;
    if (ok_) {
        assumptions_ = assumptions;
        r = search();
        cancelUntil(0);
    }
    syncProofStats();
    return r;
}

}  // namespace sat
