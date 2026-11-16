// The problem's vocabulary and its text format. No solving logic lives here:
// operations are passed on exactly as written, and every implementation
// (solver, rollback, brute) interprets them on its own.
#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dce {

inline constexpr std::int64_t kModulus = 1'000'000'007;

struct Operation {
    enum class Kind : char { Add = '+', Remove = '-', Query = '?' };

    Kind kind{};
    std::int64_t id = 0;  // Add, Remove
    int u = 0, v = 0;     // Add, Query: variables, 1-based
    std::int64_t d = 0;   // Add: x_v - x_u = d (mod p); raw, possibly negative
};

struct Problem {
    int n = 0;
    std::vector<Operation> ops;
};

// The answer to a query: the residue of x_v - x_u in [0, p), or one of these.
using Answer = std::int64_t;
inline constexpr Answer kUnknown = -1;
inline constexpr Answer kContradiction = -2;

inline Problem read_problem(std::istream& in) {
    Problem problem;
    std::int64_t q = 0;
    if (!(in >> problem.n >> q) || problem.n < 1 || q < 0)
        throw std::runtime_error("expected a header line 'n q' with n >= 1, q >= 0");

    const auto fail = [](std::int64_t index, const std::string& why) {
        throw std::runtime_error("operation " + std::to_string(index + 1) + ": " + why);
    };
    const auto check_variable = [&](std::int64_t index, int x) {
        if (x < 1 || x > problem.n) fail(index, "variable " + std::to_string(x) + " is out of range");
    };

    problem.ops.reserve(static_cast<std::size_t>(q));
    for (std::int64_t i = 0; i < q; ++i) {
        char symbol = 0;
        Operation op;
        if (!(in >> symbol)) fail(i, "unexpected end of input");
        switch (symbol) {
            case '+':
                op.kind = Operation::Kind::Add;
                if (!(in >> op.id >> op.u >> op.v >> op.d)) fail(i, "expected '+ id u v d'");
                check_variable(i, op.u);
                check_variable(i, op.v);
                break;
            case '-':
                op.kind = Operation::Kind::Remove;
                if (!(in >> op.id)) fail(i, "expected '- id'");
                break;
            case '?':
                op.kind = Operation::Kind::Query;
                if (!(in >> op.u >> op.v)) fail(i, "expected '? u v'");
                check_variable(i, op.u);
                check_variable(i, op.v);
                break;
            default:
                fail(i, std::string("unknown operation '") + symbol + "'");
        }
        problem.ops.push_back(op);
    }
    return problem;
}

inline void write_answers(std::ostream& out, const std::vector<Answer>& answers) {
    std::string text;
    for (const Answer answer : answers) {
        if (answer == kContradiction) text += "CONTRADICTION";
        else if (answer == kUnknown) text += "UNKNOWN";
        else text += std::to_string(answer);
        text += '\n';
    }
    out << text;
}

}  // namespace dce
