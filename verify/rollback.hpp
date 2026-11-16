// Second fast implementation, used to cross-check the solver at sizes the
// brute force cannot reach. It is the textbook answer to "union-find cannot
// delete": never delete, only undo.
//
// Each constraint is alive during an interval of time. Recursing over time
// (halving the interval each level), a constraint is applied at the largest
// time ranges it covers completely, and undone when the recursion leaves that
// range. The state is a union-find that stores, for every vertex, its offset
// from its parent; it uses union by size and no path compression, so every
// change can be rolled back exactly.
//
// O(q log q log n) time, O(n + q) space. Shares no code with src/solver.hpp.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../src/problem.hpp"

namespace dce::verify {

namespace rollback_detail {

inline std::int64_t mod(std::int64_t x) {
    x %= kModulus;
    return x < 0 ? x + kModulus : x;
}

class OffsetUnionFind {
public:
    explicit OffsetUnionFind(int n)
        : parent_(static_cast<std::size_t>(n) + 1), size_(parent_.size(), 1), offset_(parent_.size(), 0) {
        for (std::size_t i = 0; i < parent_.size(); ++i) parent_[i] = static_cast<int>(i);
    }

    struct Position {
        int root;
        std::int64_t offset;  // x_v - x_root
    };

    Position find(int v) const {
        std::int64_t offset = 0;
        while (parent_[idx(v)] != v) {
            offset += offset_[idx(v)];
            v = parent_[idx(v)];
        }
        return {v, mod(offset)};
    }

    // Imposes x_v - x_u = d.
    void constrain(int u, int v, std::int64_t d) {
        const Position pu = find(u), pv = find(v);
        if (pu.root == pv.root) {
            if (mod(pv.offset - pu.offset - d) != 0) {
                ++conflicts_;
                history_.push_back(kConflict);
            }
            return;
        }
        // x_rv - x_ru = (x_v - offset_v) - (x_u - offset_u) = d + offset_u - offset_v
        std::int64_t root_gap = mod(d + pu.offset - pv.offset);
        int top = pu.root, bottom = pv.root;
        if (size_[idx(top)] < size_[idx(bottom)]) {
            std::swap(top, bottom);
            root_gap = mod(-root_gap);
        }
        parent_[idx(bottom)] = top;
        offset_[idx(bottom)] = root_gap;  // x_bottom - x_top
        size_[idx(top)] += size_[idx(bottom)];
        history_.push_back(bottom);
    }

    bool contradictory() const { return conflicts_ > 0; }

    std::size_t checkpoint() const { return history_.size(); }

    void rollback(std::size_t checkpoint) {
        while (history_.size() > checkpoint) {
            const int entry = history_.back();
            history_.pop_back();
            if (entry == kConflict) {
                --conflicts_;
                continue;
            }
            const int top = parent_[idx(entry)];
            size_[idx(top)] -= size_[idx(entry)];
            parent_[idx(entry)] = entry;
            offset_[idx(entry)] = 0;
        }
    }

private:
    static constexpr int kConflict = -1;
    static std::size_t idx(int v) { return static_cast<std::size_t>(v); }

    std::vector<int> parent_, size_;
    std::vector<std::int64_t> offset_;  // x_v - x_parent(v)
    std::vector<int> history_;          // attached roots, or kConflict
    int conflicts_ = 0;
};

struct Interval {
    int begin, end;  // alive for times t with begin <= t < end
    int u, v;
    std::int64_t d;
};

struct Recursion {
    const std::vector<Operation>& ops;
    const std::vector<Interval>& intervals;
    OffsetUnionFind state;
    std::vector<Answer> answers;

    // `pending`: intervals meeting [lo, hi) that no enclosing range has applied.
    void run(int lo, int hi, const std::vector<int>& pending) {
        const std::size_t checkpoint = state.checkpoint();
        std::vector<int> partial;
        for (const int i : pending) {
            const Interval& c = intervals[static_cast<std::size_t>(i)];
            if (c.end <= lo || hi <= c.begin) continue;
            if (c.begin <= lo && hi <= c.end) state.constrain(c.u, c.v, c.d);
            else partial.push_back(i);
        }
        if (hi - lo == 1) {
            const Operation& op = ops[static_cast<std::size_t>(lo)];
            if (op.kind == Operation::Kind::Query) answers.push_back(answer(op.u, op.v));
        } else {
            const int mid = lo + (hi - lo) / 2;
            run(lo, mid, partial);
            run(mid, hi, partial);
        }
        state.rollback(checkpoint);
    }

    Answer answer(int u, int v) const {
        if (state.contradictory()) return kContradiction;
        const auto pu = state.find(u), pv = state.find(v);
        if (pu.root != pv.root) return kUnknown;
        return mod(pv.offset - pu.offset);
    }
};

}  // namespace rollback_detail

inline std::vector<Answer> solve_rollback(int n, const std::vector<Operation>& ops) {
    using namespace rollback_detail;
    const int q = static_cast<int>(ops.size());

    std::vector<Interval> intervals;
    std::unordered_map<std::int64_t, std::size_t> open;  // live id -> its interval
    for (int t = 0; t < q; ++t) {
        const Operation& op = ops[static_cast<std::size_t>(t)];
        if (op.kind == Operation::Kind::Add) {
            open[op.id] = intervals.size();
            intervals.push_back({t, q, op.u, op.v, op.d});
        } else if (op.kind == Operation::Kind::Remove) {
            const auto it = open.find(op.id);
            intervals[it->second].end = t;
            open.erase(it);
        }
    }

    Recursion recursion{ops, intervals, OffsetUnionFind(n), {}};
    if (q > 0) {
        std::vector<int> all(intervals.size());
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<int>(i);
        recursion.run(0, q, all);
    }
    return recursion.answers;
}

}  // namespace dce::verify
