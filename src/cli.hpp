// Shared command-line shell: reads a problem from stdin, writes answers to stdout.
#pragma once

#include <exception>
#include <iostream>
#include <vector>

#include "problem.hpp"

namespace dce {

template <typename Solve>
int run_cli(Solve&& solve) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    try {
        const Problem problem = read_problem(std::cin);
        write_answers(std::cout, solve(problem.n, problem.ops));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace dce
