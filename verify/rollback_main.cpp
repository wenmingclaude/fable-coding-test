#include "../src/cli.hpp"
#include "rollback.hpp"

int main() { return dce::run_cli(dce::verify::solve_rollback); }
