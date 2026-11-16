// Generators of legal operation sequences, shared by the fuzzer (small sizes)
// and the benchmark (n, q ~ 200000). Everything is deterministic in the seed.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <numeric>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/problem.hpp"

namespace dce::testing {

using Rng = std::mt19937_64;

inline std::int64_t uniform(Rng& rng, std::int64_t lo, std::int64_t hi) {
    return std::uniform_int_distribution<std::int64_t>(lo, hi)(rng);
}
inline bool chance(Rng& rng, double probability) {
    return std::bernoulli_distribution(probability)(rng);
}

inline Operation make_add(std::int64_t id, int u, int v, std::int64_t d) {
    return {Operation::Kind::Add, id, u, v, d};
}
inline Operation make_remove(std::int64_t id) { return {Operation::Kind::Remove, id, 0, 0, 0}; }
inline Operation make_query(int u, int v) { return {Operation::Kind::Query, 0, u, v, 0}; }

// A hidden assignment of all variables. Constraints read off it agree with
// each other, so they form consistent cycles; a "lie" breaks that on purpose.
class Truth {
public:
    // Values are drawn from [0, spread): a small spread makes coincidences
    // (equal gaps, zero gaps) common.
    Truth(Rng& rng, int n, std::int64_t spread = kModulus) : x_(static_cast<std::size_t>(n) + 1) {
        for (auto& value : x_) value = uniform(rng, 0, spread - 1);
    }

    // Some d with d = x_v - x_u (+ lie) mod p, written as an arbitrary
    // representative: negative, or shifted by multiples of p, within |d| <= 1e12.
    std::int64_t gap(Rng& rng, int u, int v, std::int64_t lie = 0) const {
        const std::int64_t d = x_[static_cast<std::size_t>(v)] - x_[static_cast<std::size_t>(u)] + lie;
        return chance(rng, 0.5) ? d : d + kModulus * uniform(rng, -998, 998);
    }

private:
    std::vector<std::int64_t> x_;
};

// A random blend of the three operations.
struct Mix {
    int n = 5;
    int q = 30;
    double add = 0.5;
    double remove = 0.2;  // the rest of the probability goes to queries
    double lie = 0.1;     // chance that a new constraint disagrees with the truth
    double loop = 0.03;   // chance of a self-constraint x_u - x_u = d
    double extreme = 0.0; // chance that d is a boundary value instead
    enum class Victim { Random, Oldest, Newest } victim = Victim::Random;
    int id_pool = 0;      // > 0: ids come from [1, id_pool], so they get reused
    std::int64_t spread = kModulus;
};

inline Problem mixed(Rng& rng, const Mix& mix) {
    static constexpr std::int64_t kExtremes[] = {
        0, 1, -1, kModulus, -kModulus, kModulus - 1, 1 - kModulus,
        1'000'000'000'000, -1'000'000'000'000, 999'999'999'999, -999'999'999'999};

    Problem problem{mix.n, {}};
    const Truth truth(rng, mix.n, mix.spread);
    std::deque<std::int64_t> live;    // ids in use
    std::vector<std::int64_t> spare;  // ids of the pool not in use
    for (std::int64_t id = 1; id <= mix.id_pool; ++id) spare.push_back(id);
    std::int64_t fresh_id = 1;

    const auto take = [](auto& container, std::size_t index) {
        std::swap(container[index], container.back());
        const std::int64_t id = container.back();
        container.pop_back();
        return id;
    };
    const auto random_index = [&rng](const auto& container) {
        return static_cast<std::size_t>(uniform(rng, 0, static_cast<std::int64_t>(container.size()) - 1));
    };

    while (static_cast<int>(problem.ops.size()) < mix.q) {
        const double roll = std::uniform_real_distribution<double>(0, 1)(rng);
        if (roll < mix.add && (mix.id_pool == 0 || !spare.empty())) {
            const std::int64_t id = mix.id_pool == 0 ? fresh_id++ : take(spare, random_index(spare));
            const int u = static_cast<int>(uniform(rng, 1, mix.n));
            const int v = chance(rng, mix.loop) ? u : static_cast<int>(uniform(rng, 1, mix.n));
            const std::int64_t d = chance(rng, mix.extreme)
                ? kExtremes[uniform(rng, 0, static_cast<std::int64_t>(std::size(kExtremes)) - 1)]
                : truth.gap(rng, u, v, chance(rng, mix.lie) ? uniform(rng, 1, 3) : 0);
            problem.ops.push_back(make_add(id, u, v, d));
            live.push_back(id);
        } else if (roll < mix.add + mix.remove && !live.empty()) {
            std::int64_t id = 0;
            switch (mix.victim) {
                case Mix::Victim::Random: id = take(live, random_index(live)); break;
                case Mix::Victim::Newest: id = take(live, live.size() - 1); break;
                case Mix::Victim::Oldest: id = live.front(); live.pop_front(); break;
            }
            if (mix.id_pool > 0) spare.push_back(id);
            problem.ops.push_back(make_remove(id));
        } else {
            const int u = static_cast<int>(uniform(rng, 1, mix.n));
            const int v = chance(rng, 0.1) ? u : static_cast<int>(uniform(rng, 1, mix.n));
            problem.ops.push_back(make_query(u, v));
        }
    }
    return problem;
}

// A chain 1 - 2 - ... - n. Then, over and over: break a random link, ask
// across the gap, and restore the link under the same id.
inline Problem chain_churn(Rng& rng, int n, int q) {
    Problem problem{n, {}};
    const Truth truth(rng, n);
    const auto link = [&](int i) { return make_add(i, i, i + 1, truth.gap(rng, i, i + 1)); };
    for (int i = 1; i < n && static_cast<int>(problem.ops.size()) < q; ++i) problem.ops.push_back(link(i));
    while (n >= 2 && static_cast<int>(problem.ops.size()) + 4 <= q) {
        const int i = static_cast<int>(uniform(rng, 1, n - 1));
        problem.ops.push_back(make_remove(i));
        problem.ops.push_back(make_query(1, n));
        problem.ops.push_back(make_query(static_cast<int>(uniform(rng, 1, n)), static_cast<int>(uniform(rng, 1, n))));
        problem.ops.push_back(link(i));
    }
    while (static_cast<int>(problem.ops.size()) < q) problem.ops.push_back(make_query(1, n));
    return problem;
}

// A chain assembled in random order, then queried: random pairs, and sweeps
// that walk both ends inwards (long paths, sequential access).
inline Problem chain_queries(Rng& rng, int n, int q) {
    Problem problem{n, {}};
    const Truth truth(rng, n);
    std::vector<int> order(static_cast<std::size_t>(std::max(n - 1, 0)));
    std::iota(order.begin(), order.end(), 1);
    std::shuffle(order.begin(), order.end(), rng);
    for (const int i : order) {
        if (static_cast<int>(problem.ops.size()) >= q) break;
        problem.ops.push_back(make_add(i, i, i + 1, truth.gap(rng, i, i + 1)));
    }
    for (int k = 0; static_cast<int>(problem.ops.size()) < q; ++k) {
        if (k % 2 == 0) {
            const int i = k / 2 % n + 1;
            problem.ops.push_back(make_query(i, n + 1 - i));
        } else {
            problem.ops.push_back(make_query(static_cast<int>(uniform(rng, 1, n)), static_cast<int>(uniform(rng, 1, n))));
        }
    }
    return problem;
}

// A consistent random spanning tree as a backdrop. Lying constraints then
// come and go with overlapping lifetimes, so the system keeps flipping
// between contradictory and consistent.
inline Problem contradiction_storm(Rng& rng, int n, int q) {
    Problem problem{n, {}};
    const Truth truth(rng, n);
    std::int64_t id = 0;
    for (int v = 2; v <= n && static_cast<int>(problem.ops.size()) < q; ++v) {
        const int u = static_cast<int>(uniform(rng, 1, v - 1));
        problem.ops.push_back(make_add(++id, u, v, truth.gap(rng, u, v)));
    }
    std::vector<std::int64_t> liars;
    while (static_cast<int>(problem.ops.size()) < q) {
        const double roll = std::uniform_real_distribution<double>(0, 1)(rng);
        if (roll < 0.30) {
            const int u = static_cast<int>(uniform(rng, 1, n));
            const int v = static_cast<int>(uniform(rng, 1, n));
            problem.ops.push_back(make_add(++id, u, v, truth.gap(rng, u, v, uniform(rng, 1, 3))));
            liars.push_back(id);
        } else if (roll < 0.70 && !liars.empty()) {
            const auto index = static_cast<std::size_t>(uniform(rng, 0, static_cast<std::int64_t>(liars.size()) - 1));
            std::swap(liars[index], liars.back());
            problem.ops.push_back(make_remove(liars.back()));
            liars.pop_back();
        } else {
            problem.ops.push_back(make_query(static_cast<int>(uniform(rng, 1, n)), static_cast<int>(uniform(rng, 1, n))));
        }
    }
    return problem;
}

// Built to hurt the rollback implementation. Merging blocks of equal size
// makes union-by-size trees as deep as they can get (log n), and lifetimes
// nested like brackets make the recursion over time re-apply each of the
// later constraints at about log q levels.
inline Problem deep_merges(Rng& rng, int n, int q) {
    Problem problem{n, {}};
    const Truth truth(rng, n);
    const auto size = [&] { return static_cast<int>(problem.ops.size()); };
    const auto random_variable = [&] { return static_cast<int>(uniform(rng, 1, n)); };
    std::int64_t id = 0;
    for (int half = 1; half < n; half *= 2)
        for (int i = 1; i + half <= n && size() < q; i += 2 * half)
            problem.ops.push_back(make_add(++id, i, i + half, truth.gap(rng, i, i + half)));
    std::vector<std::int64_t> nested;
    for (int k = (q - size()) / 3; k > 0; --k) {
        const int u = random_variable(), v = random_variable();
        problem.ops.push_back(make_add(++id, u, v, truth.gap(rng, u, v)));
        nested.push_back(id);
    }
    for (; !nested.empty(); nested.pop_back()) {
        problem.ops.push_back(make_query(random_variable(), random_variable()));
        problem.ops.push_back(make_remove(nested.back()));
    }
    while (size() < q) problem.ops.push_back(make_query(random_variable(), random_variable()));
    return problem;
}

// The named workloads of the benchmark.
inline Problem workload(const std::string& name, int n, int q, std::uint64_t seed) {
    Rng rng(seed);
    Mix mix;
    mix.n = n;
    mix.q = q;
    mix.lie = 0;  // contradictions make queries cheaper; keep the heavy cases honest
    mix.loop = 0;
    if (name == "mixed") {  // random insert / delete / query blend
        mix.add = 0.45, mix.remove = 0.25;
        return mixed(rng, mix);
    }
    if (name == "window") {  // deletions in insertion order: every cycle evicts a forest edge
        mix.add = 0.42, mix.remove = 0.38, mix.victim = Mix::Victim::Oldest;
        return mixed(rng, mix);
    }
    if (name == "stack") {  // deletions in reverse order: no cycle ever evicts
        mix.add = 0.42, mix.remove = 0.38, mix.victim = Mix::Victim::Newest;
        return mixed(rng, mix);
    }
    if (name == "grow") {  // insertions and queries only
        mix.add = 0.6, mix.remove = 0;
        return mixed(rng, mix);
    }
    if (name == "chain-churn") return chain_churn(rng, n, q);
    if (name == "chain-queries") return chain_queries(rng, n, q);
    if (name == "storm") return contradiction_storm(rng, n, q);
    if (name == "deep-merges") return deep_merges(rng, n, q);
    throw std::invalid_argument("unknown workload '" + name + "'");
}

inline void write_problem(std::ostream& out, const Problem& problem) {
    std::string text = std::to_string(problem.n) + ' ' + std::to_string(problem.ops.size()) + '\n';
    for (const Operation& op : problem.ops) {
        switch (op.kind) {
            case Operation::Kind::Add:
                text += "+ " + std::to_string(op.id) + ' ' + std::to_string(op.u) + ' ' +
                        std::to_string(op.v) + ' ' + std::to_string(op.d) + '\n';
                break;
            case Operation::Kind::Remove: text += "- " + std::to_string(op.id) + '\n'; break;
            case Operation::Kind::Query:
                text += "? " + std::to_string(op.u) + ' ' + std::to_string(op.v) + '\n';
                break;
        }
    }
    out << text;
}

}  // namespace dce::testing
