// Hand-written cases with hand-computed answers. Every case is run through
// the text parser and through all three implementations.
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/problem.hpp"
#include "../src/solver.hpp"
#include "../src/spanning_forest.hpp"
#include "../verify/brute.hpp"
#include "../verify/rollback.hpp"

namespace {

using Solve = std::function<std::vector<dce::Answer>(int, const std::vector<dce::Operation>&)>;

struct Case {
    std::string name;
    std::string input;
    std::string expected;
};

// p = 1000000007. Useful residues: -1 = 1000000006, -10^12 = 7000.
const std::vector<Case> kCases = {
    {"example from the problem statement",
     "4 12\n+ 10 1 2 3\n+ 20 2 3 5\n? 1 3\n? 1 4\n+ 30 1 3 9\n? 1 3\n- 30\n? 1 3\n"
     "+ 40 4 4 1\n? 4 4\n- 40\n? 4 4\n",
     "8\nUNKNOWN\nCONTRADICTION\n8\nCONTRADICTION\n0\n"},

    {"no constraints at all",
     "3 3\n? 1 1\n? 1 2\n? 3 2\n",
     "0\nUNKNOWN\nUNKNOWN\n"},

    {"single variable",
     "1 4\n? 1 1\n+ 5 1 1 0\n? 1 1\n? 1 1\n",
     "0\n0\n0\n"},

    {"direction: x_v - x_u = d, and the reverse query negates",
     "2 3\n+ 1 1 2 3\n? 1 2\n? 2 1\n",
     "3\n1000000004\n"},

    {"negative and huge d are reduced mod p",
     "3 6\n+ 1 1 2 -1\n? 1 2\n? 2 1\n+ 2 2 3 -1000000000000\n? 2 3\n? 3 1\n",
     "1000000006\n1\n7000\n999993008\n"},

    {"d = 10^12 and sums that wrap around p",
     "3 4\n+ 1 1 2 1000000000000\n+ 2 2 3 1000000000000\n? 1 3\n? 3 1\n",
     "999986007\n14000\n"},

    {"self-constraint with d = 0 mod p is harmless",
     "2 5\n+ 1 1 1 1000000007\n+ 2 2 2 -1000000007\n+ 3 1 1 0\n? 1 1\n? 1 2\n",
     "0\nUNKNOWN\n"},

    {"self-constraint with d != 0 poisons everything until removed",
     "3 6\n+ 1 1 2 5\n+ 2 3 3 1\n? 1 2\n? 1 3\n- 2\n? 1 2\n",
     "CONTRADICTION\nCONTRADICTION\n5\n"},

    {"duplicate constraints: either copy alone keeps the information",
     "2 7\n+ 1 1 2 5\n+ 2 1 2 5\n? 1 2\n- 1\n? 1 2\n- 2\n? 1 2\n",
     "5\n5\nUNKNOWN\n"},

    {"duplicate, removed in the other order",
     "2 7\n+ 1 1 2 5\n+ 2 1 2 5\n? 1 2\n- 2\n? 1 2\n- 1\n? 1 2\n",
     "5\n5\nUNKNOWN\n"},

    {"the same fact stated in both directions",
     "2 4\n+ 1 1 2 5\n+ 2 2 1 -5\n? 1 2\n? 2 1\n",
     "5\n1000000002\n"},

    {"three mutually contradicting copies, removed one by one",
     "2 8\n+ 1 1 2 1\n+ 2 1 2 2\n+ 3 1 2 3\n? 1 2\n- 2\n? 1 2\n- 1\n? 1 2\n",
     "CONTRADICTION\nCONTRADICTION\n3\n"},

    {"consistent triangle survives the loss of any one side, not two",
     "3 9\n+ 1 1 2 2\n+ 2 2 3 3\n+ 3 3 1 -5\n? 1 3\n- 1\n? 1 2\n? 3 2\n- 3\n? 1 2\n",
     "5\n2\n1000000004\nUNKNOWN\n"},

    {"contradictory 4-cycle: removing any side heals it",
     "4 11\n+ 1 1 2 1\n+ 2 2 3 1\n+ 3 3 4 1\n+ 4 4 1 1\n? 1 3\n- 2\n? 1 3\n? 2 4\n"
     "+ 2 2 3 -3\n? 1 3\n? 2 3\n",
     "CONTRADICTION\n1000000005\n1000000005\n1000000005\n1000000004\n"},

    {"removing a bridge disconnects",
     "3 6\n+ 1 1 2 4\n+ 2 2 3 6\n? 1 3\n- 2\n? 1 3\n? 1 2\n",
     "10\nUNKNOWN\n4\n"},

    {"an id is reused for a different constraint",
     "3 6\n+ 7 1 2 4\n- 7\n+ 7 2 3 6\n? 1 2\n? 2 3\n? 1 3\n",
     "UNKNOWN\n6\nUNKNOWN\n"},

    {"an id is reused: contradiction, repair, contradiction again",
     "2 9\n+ 1 1 2 1\n+ 2 1 2 2\n? 1 2\n- 2\n? 1 2\n+ 2 2 1 1\n? 1 2\n- 1\n? 1 2\n",
     "CONTRADICTION\n1\nCONTRADICTION\n1000000006\n"},

    {"ids may be large or negative",
     "2 5\n+ -9000000000000000000 1 2 1\n+ 9000000000000000000 1 2 1\n- -9000000000000000000\n? 1 2\n? 2 2\n",
     "1\n0\n"},

    {"a contradiction far away blocks unrelated queries",
     "6 7\n+ 1 1 2 1\n+ 2 5 6 1\n+ 3 6 5 1\n? 1 2\n? 3 4\n- 2\n? 1 2\n",
     "CONTRADICTION\nCONTRADICTION\n1\n"},

    {"a short-lived constraint is replaced by a longer-lived parallel path",
     // 1-2 and 2-3 die early; 1-3 arrives later and must take over.
     "3 9\n+ 1 1 2 1\n+ 2 2 3 1\n+ 3 1 3 2\n- 1\n? 1 3\n? 1 2\n- 2\n? 1 3\n? 2 3\n",
     "2\n1\n2\nUNKNOWN\n"},

    {"overlapping contradictions with staggered lifetimes",
     "3 12\n+ 1 1 2 1\n+ 2 2 3 1\n+ 3 1 3 5\n+ 4 3 1 7\n? 1 2\n- 3\n? 1 2\n- 4\n? 1 3\n"
     "+ 5 1 1 3\n? 2 3\n? 1 1\n",
     "CONTRADICTION\nCONTRADICTION\n2\nCONTRADICTION\nCONTRADICTION\n"},

    {"a short contradiction that begins and ends inside a long one",
     "2 9\n+ 1 1 2 1\n+ 2 1 2 2\n+ 3 1 1 5\n? 1 2\n- 3\n? 1 2\n- 2\n? 1 2\n? 1 1\n",
     "CONTRADICTION\nCONTRADICTION\n1\n0\n"},

    {"whitespace and line layout do not matter",
     "2   3\n+   1  1 2\n 5\n\n?\n1 2 ? 2 1\n",
     "5\n1000000002\n"},
};

const std::vector<Case> kRejected = {
    {"missing header", "", ""},
    {"n must be positive", "0 0\n", ""},
    {"unknown operation", "2 1\n* 1 2\n", ""},
    {"variable out of range", "2 1\n? 1 3\n", ""},
    {"variable zero", "2 1\n+ 1 0 1 5\n", ""},
    {"truncated input", "2 2\n? 1 2\n", ""},
    {"id added twice", "2 2\n+ 1 1 2 3\n+ 1 1 2 3\n", ""},
    {"unknown id removed", "2 1\n- 1\n", ""},
    {"id removed twice", "2 3\n+ 1 1 2 3\n- 1\n- 1\n", ""},
};

int failures = 0;

void expect(bool condition, const std::string& what) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL: " << what << '\n';
}

std::string run(const Solve& solve, const std::string& input) {
    std::istringstream in(input);
    const dce::Problem problem = dce::read_problem(in);
    std::ostringstream out;
    dce::write_answers(out, solve(problem.n, problem.ops));
    return out.str();
}

void test_cases() {
    const std::vector<std::pair<std::string, Solve>> implementations = {
        {"solver", dce::solve},
        {"rollback", dce::verify::solve_rollback},
        {"brute", dce::verify::solve_brute},
    };
    for (const Case& c : kCases) {
        for (const auto& [name, solve] : implementations) {
            const std::string actual = run(solve, c.input);
            expect(actual == c.expected,
                   name + " on \"" + c.name + "\"\n--- expected ---\n" + c.expected + "--- actual ---\n" + actual);
        }
    }
}

void test_rejected_input() {
    for (const Case& c : kRejected) {
        bool rejected = false;
        try {
            run(dce::solve, c.input);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        expect(rejected, "malformed input should be rejected: " + c.name);
    }
}

// The forest's contract, checked directly.
void test_spanning_forest() {
    using dce::kNever;
    dce::SpanningForest forest(4);

    expect(!forest.path(1, 2), "fresh vertices are disconnected");
    expect(forest.path(3, 3) && forest.path(3, 3)->gain == 0 && forest.path(3, 3)->expiry == kNever,
           "the empty path gains nothing and never breaks");

    expect(!forest.insert(1, 2, 10, 50), "joining two trees closes no cycle");
    expect(!forest.insert(2, 3, 20, 40), "joining two trees closes no cycle");
    auto path = forest.path(1, 3);
    expect(path && path->gain == 30 && path->expiry == 40, "path 1~>3: gain 30, breaks at 40");
    path = forest.path(3, 1);
    expect(path && path->gain == dce::kP - 30 && path->expiry == 40, "path 3~>1 is the negative");

    // A weaker parallel edge: the cycle is reported, the forest is unchanged.
    auto cycle = forest.insert(1, 3, 30, 35);
    expect(cycle && cycle->gain == 0 && cycle->expiry == 35, "consistent cycle, first break at 35");
    path = forest.path(1, 3);
    expect(path && path->expiry == 40, "the weaker edge was not adopted");

    // A stronger, disagreeing edge evicts the weakest edge of the cycle (2-3).
    cycle = forest.insert(3, 1, 1, 60);  // says x_1 - x_3 = 1, the path says -30
    expect(cycle && cycle->gain == dce::kP - 31 && cycle->expiry == 40, "inconsistent cycle until 40");
    path = forest.path(1, 3);
    expect(path && path->gain == dce::kP - 1 && path->expiry == 60, "1~>3 now uses the new edge");
    path = forest.path(2, 3);
    expect(path && path->gain == dce::kP - 11 && path->expiry == 50, "2~>3 now goes round via 1");

    expect(!forest.path(1, 4) && !forest.path(4, 2), "vertex 4 is still on its own");
    cycle = forest.insert(4, 4, 7, 9);
    expect(cycle && cycle->gain == dce::kP - 7 && cycle->expiry == 9, "a self-loop is a cycle of one edge");
}

}  // namespace

int main() {
    test_cases();
    test_rejected_input();
    test_spanning_forest();
    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "unit tests: " << kCases.size() << " cases x 3 implementations, " << kRejected.size()
              << " rejected inputs, forest contract: all passed\n";
    return 0;
}
