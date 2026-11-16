// The dynamic constraint engine.
//
// Picture every variable as a vertex and every constraint x_v - x_u = d as an
// edge u -> v with gain d. Then
//
//   * the system is solvable       <=>  the gains around every cycle sum to 0;
//   * x_v - x_u is determined      <=>  u and v are connected, and then it is
//                                       the gain along any path u ~> v.
//
// Deletions are removed from the picture before solving starts: the whole
// operation list is known, so each constraint is born knowing the time at
// which it will expire. What remains is insert-only. The solver keeps a
// maximum spanning forest by expiry (see spanning_forest.hpp) plus a single
// number, `contradictory_until`.
//
// Time is the index of the operation. O((n + q) log n) time, O(n + q) space.
#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "problem.hpp"
#include "spanning_forest.hpp"

namespace dce {

static_assert(kP == kModulus, "the forest must compute in the problem's field");

// For each Add, the time of the Remove that cancels it (kNever if none).
inline std::vector<int> expiry_times(const std::vector<Operation>& ops) {
    std::vector<int> expiry(ops.size(), kNever);
    std::unordered_map<std::int64_t, int> born;  // live id -> time of its Add
    for (int t = 0; t < static_cast<int>(ops.size()); ++t) {
        const Operation& op = ops[static_cast<std::size_t>(t)];
        if (op.kind == Operation::Kind::Add) {
            if (!born.emplace(op.id, t).second)
                throw std::runtime_error("operation " + std::to_string(t + 1) + ": id " +
                                         std::to_string(op.id) + " is already in use");
        } else if (op.kind == Operation::Kind::Remove) {
            const auto it = born.find(op.id);
            if (it == born.end())
                throw std::runtime_error("operation " + std::to_string(t + 1) + ": id " +
                                         std::to_string(op.id) + " is not in use");
            expiry[static_cast<std::size_t>(it->second)] = t;
            born.erase(it);
        }
    }
    return expiry;
}

inline std::vector<Answer> solve(int n, const std::vector<Operation>& ops) {
    if (ops.size() >= static_cast<std::size_t>(kNever))
        throw std::runtime_error("too many operations: times must stay below kNever");
    const std::vector<int> expiry = expiry_times(ops);
    SpanningForest forest(n);

    // The system is unsolvable at time t  <=>  t < contradictory_until.
    int contradictory_until = 0;

    std::vector<Answer> answers;
    for (int t = 0; t < static_cast<int>(ops.size()); ++t) {
        const Operation& op = ops[static_cast<std::size_t>(t)];
        switch (op.kind) {
            case Operation::Kind::Add: {
                // A cycle whose gains do not cancel is a contradiction for as
                // long as all of its edges are alive.
                const auto cycle = forest.insert(op.u, op.v, to_residue(op.d),
                                                 expiry[static_cast<std::size_t>(t)]);
                if (cycle && cycle->gain != 0)
                    contradictory_until = std::max(contradictory_until, cycle->expiry);
                break;
            }
            case Operation::Kind::Remove:
                break;  // nothing happens now: the constraint has just expired
            case Operation::Kind::Query: {
                if (t < contradictory_until) {
                    answers.push_back(kContradiction);
                    break;
                }
                const auto path = forest.path(op.u, op.v);
                const bool alive = path && path->expiry > t;
                answers.push_back(alive ? Answer{path->gain} : kUnknown);
                break;
            }
        }
    }
    return answers;
}

}  // namespace dce
