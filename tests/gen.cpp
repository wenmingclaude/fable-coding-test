// Writes one of the named benchmark workloads to stdout.
// usage: gen <workload> <n> <q> <seed>
#include <cstdlib>
#include <exception>
#include <iostream>

#include "workloads.hpp"

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: gen <workload> <n> <q> <seed>\n"
                     "workloads: mixed window stack grow chain-churn chain-queries storm deep-merges\n";
        return 2;
    }
    try {
        const auto problem = dce::testing::workload(argv[1], std::atoi(argv[2]), std::atoi(argv[3]),
                                                    std::strtoull(argv[4], nullptr, 10));
        std::ios::sync_with_stdio(false);
        dce::testing::write_problem(std::cout, problem);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
