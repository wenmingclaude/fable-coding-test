// Randomised differential testing.
//
//   tier 1  tiny cases     solver vs brute (ground truth) vs rollback
//   tier 2  small cases    solver vs brute vs rollback, longer histories
//   tier 3  large cases    solver vs rollback (the brute force is too slow)
//
// usage: fuzz [seed] [scale]     scale multiplies the number of cases
//
// On the first disagreement the offending input is saved and the run fails.
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/solver.hpp"
#include "../verify/brute.hpp"
#include "../verify/rollback.hpp"
#include "workloads.hpp"

namespace {

using namespace dce;
using namespace dce::testing;

struct Tally {
    long long cases = 0, operations = 0;
    long long values = 0, unknown = 0, contradiction = 0;

    void count(const Problem& problem, const std::vector<Answer>& answers) {
        ++cases;
        operations += static_cast<long long>(problem.ops.size());
        for (const Answer a : answers) ++(a == kContradiction ? contradiction : a == kUnknown ? unknown : values);
    }
};

[[noreturn]] void report_mismatch(const Problem& problem, const std::string& who,
                                  const std::vector<Answer>& expected, const std::vector<Answer>& actual) {
    const char* path = "fuzz-failure.in";
    std::ofstream file(path);
    write_problem(file, problem);
    std::cout << "MISMATCH: solver vs " << who << " (input saved to " << path << ")\n";
    for (std::size_t i = 0; i < std::max(expected.size(), actual.size()); ++i) {
        if (i < expected.size() && i < actual.size() && expected[i] == actual[i]) continue;
        std::cout << "  first difference at query #" << i + 1 << '\n';
        break;
    }
    std::exit(1);
}

void check(const Problem& problem, bool with_brute, Tally& tally) {
    const std::vector<Answer> fast = solve(problem.n, problem.ops);
    const std::vector<Answer> undo = verify::solve_rollback(problem.n, problem.ops);
    if (fast != undo) report_mismatch(problem, "rollback", undo, fast);
    if (with_brute) {
        const std::vector<Answer> truth = verify::solve_brute(problem.n, problem.ops);
        if (fast != truth) report_mismatch(problem, "brute", truth, fast);
    }
    tally.count(problem, fast);
}

template <typename T, std::size_t N>
T pick(Rng& rng, const std::array<T, N>& options) {
    return options[static_cast<std::size_t>(uniform(rng, 0, static_cast<std::int64_t>(N) - 1))];
}

// A random point in the space of mixes: every knob is varied independently.
Mix random_mix(Rng& rng, int max_n, int max_q) {
    Mix mix;
    mix.n = static_cast<int>(uniform(rng, 1, max_n));
    mix.q = static_cast<int>(uniform(rng, 1, max_q));
    mix.add = pick(rng, std::array{0.3, 0.45, 0.6, 0.8});
    mix.remove = pick(rng, std::array{0.0, 0.1, 0.2, 0.35});
    mix.lie = pick(rng, std::array{0.0, 0.02, 0.1, 0.3, 1.0});
    mix.loop = pick(rng, std::array{0.0, 0.05, 0.3});
    mix.extreme = pick(rng, std::array{0.0, 0.0, 0.1});
    mix.victim = pick(rng, std::array{Mix::Victim::Random, Mix::Victim::Oldest, Mix::Victim::Newest});
    mix.id_pool = static_cast<int>(pick(rng, std::array<std::int64_t, 4>{0, 0, 2, 5}));
    mix.spread = pick(rng, std::array<std::int64_t, 4>{1, 2, 4, kModulus});
    return mix;
}

void print(const std::string& tier, const Tally& t) {
    std::cout << tier << ": " << t.cases << " cases, " << t.operations << " operations; answers: "
              << t.values << " values, " << t.unknown << " UNKNOWN, " << t.contradiction
              << " CONTRADICTION\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20260101;
    const double scale = argc > 2 ? std::atof(argv[2]) : 1.0;
    const auto cases = [scale](int base) { return static_cast<int>(base * scale) + 1; };
    Rng rng(seed);
    std::cout << "fuzz: seed " << seed << ", scale " << scale << '\n';

    Tally tiny;
    for (int i = 0; i < cases(200000); ++i) check(mixed(rng, random_mix(rng, 6, 40)), true, tiny);
    for (int i = 0; i < cases(10000); ++i) {
        const int n = static_cast<int>(uniform(rng, 1, 7));
        const int q = static_cast<int>(uniform(rng, 1, 50));
        check(chain_churn(rng, n, q), true, tiny);
        check(chain_queries(rng, n, q), true, tiny);
        check(contradiction_storm(rng, n, q), true, tiny);
        check(deep_merges(rng, n, q), true, tiny);
    }
    print("tier 1 (vs brute + rollback)", tiny);

    Tally small;
    for (int i = 0; i < cases(3000); ++i) check(mixed(rng, random_mix(rng, 14, 160)), true, small);
    print("tier 2 (vs brute + rollback)", small);

    Tally large;
    for (int i = 0; i < cases(1500); ++i) {
        Mix mix = random_mix(rng, 1, 1);
        mix.n = static_cast<int>(uniform(rng, 2, pick(rng, std::array<std::int64_t, 3>{30, 300, 3000})));
        mix.q = static_cast<int>(uniform(rng, 100, 20000));
        mix.lie = pick(rng, std::array{0.0, 0.0, 0.001, 0.01});
        mix.spread = kModulus;
        check(mixed(rng, mix), false, large);
    }
    for (int i = 0; i < cases(100); ++i) {
        const int n = static_cast<int>(uniform(rng, 2, 4000));
        const int q = static_cast<int>(uniform(rng, 100, 20000));
        check(chain_churn(rng, n, q), false, large);
        check(chain_queries(rng, n, q), false, large);
        check(contradiction_storm(rng, n, q), false, large);
        check(deep_merges(rng, n, q), false, large);
    }
    print("tier 3 (vs rollback)", large);

    std::cout << "fuzz: no disagreement found\n";
    return 0;
}
