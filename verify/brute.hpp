// Brute-force reference: plain linear algebra, straight from the definition.
//
// For every query the current constraints are written out as a dense linear
// system A x = b over F_p and row-reduced from scratch. Nothing here knows
// that the equations come from a graph, so it shares no idea (and no code)
// with the fast solvers. Roughly O(m * n * min(m, n)) per query: only for
// tiny inputs.
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "../src/problem.hpp"

namespace dce::verify {

namespace brute_detail {

using Row = std::vector<std::int64_t>;  // n coefficients, then the right-hand side

inline std::int64_t mod(std::int64_t x) {
    x %= kModulus;
    return x < 0 ? x + kModulus : x;
}

inline std::int64_t inverse(std::int64_t a) {  // Fermat: a^(p-2)
    std::int64_t result = 1;
    for (std::int64_t e = kModulus - 2; e > 0; e >>= 1) {
        if (e & 1) result = result * a % kModulus;
        a = a * a % kModulus;
    }
    return result;
}

inline Answer decide(int n, const std::map<std::int64_t, Operation>& live, int u, int v) {
    const auto columns = static_cast<std::size_t>(n);

    std::vector<Row> rows;
    for (const auto& [id, c] : live) {
        Row row(columns + 1, 0);
        row[static_cast<std::size_t>(c.v - 1)] += 1;  // x_v - x_u = d; a self-loop gives 0 = d
        row[static_cast<std::size_t>(c.u - 1)] -= 1;
        row[columns] = c.d;
        for (auto& entry : row) entry = mod(entry);
        rows.push_back(row);
    }

    // Reduced row echelon form.
    std::vector<std::size_t> pivot_columns;
    std::size_t rank = 0;
    for (std::size_t col = 0; col < columns && rank < rows.size(); ++col) {
        std::size_t pivot = rank;
        while (pivot < rows.size() && rows[pivot][col] == 0) ++pivot;
        if (pivot == rows.size()) continue;
        std::swap(rows[rank], rows[pivot]);
        const std::int64_t scale = inverse(rows[rank][col]);
        for (auto& entry : rows[rank]) entry = entry * scale % kModulus;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (r == rank || rows[r][col] == 0) continue;
            const std::int64_t factor = rows[r][col];
            for (std::size_t k = 0; k <= columns; ++k)
                rows[r][k] = mod(rows[r][k] - factor * rows[rank][k]);
        }
        pivot_columns.push_back(col);
        ++rank;
    }

    // Rows below the rank read "0 = rhs".
    for (std::size_t r = rank; r < rows.size(); ++r)
        if (rows[r][columns] != 0) return kContradiction;

    // x_v - x_u is determined iff the functional e_v - e_u is a combination
    // of the rows of A; the same combination of b is then its value.
    Row target(columns, 0);
    target[static_cast<std::size_t>(v - 1)] += 1;
    target[static_cast<std::size_t>(u - 1)] -= 1;
    for (auto& entry : target) entry = mod(entry);
    std::int64_t value = 0;
    for (std::size_t r = 0; r < rank; ++r) {
        const std::int64_t factor = target[pivot_columns[r]];
        if (factor == 0) continue;
        for (std::size_t k = 0; k < columns; ++k)
            target[k] = mod(target[k] - factor * rows[r][k]);
        value = mod(value + factor * rows[r][columns]);
    }
    for (const std::int64_t entry : target)
        if (entry != 0) return kUnknown;
    return value;
}

}  // namespace brute_detail

inline std::vector<Answer> solve_brute(int n, const std::vector<Operation>& ops) {
    std::map<std::int64_t, Operation> live;
    std::vector<Answer> answers;
    for (const Operation& op : ops) {
        switch (op.kind) {
            case Operation::Kind::Add: live[op.id] = op; break;
            case Operation::Kind::Remove: live.erase(op.id); break;
            case Operation::Kind::Query:
                answers.push_back(brute_detail::decide(n, live, op.u, op.v));
                break;
        }
    }
    return answers;
}

}  // namespace dce::verify
