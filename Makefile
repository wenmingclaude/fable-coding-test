CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wshadow
SANFLAGS  = -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
            -fno-sanitize-recover=all -D_GLIBCXX_ASSERTIONS -Wall -Wextra -Wshadow
PYTHON   ?= python3

B  = build
BS = build/san

BINS     = $(B)/sat_solver $(B)/rup_check $(B)/model_check $(B)/brute $(B)/dpll
SAN_BINS = $(BS)/sat_solver $(BS)/rup_check $(BS)/model_check $(BS)/brute $(BS)/dpll

SOLVER_SRC = src/main.cpp src/solver.cpp
SOLVER_HDR = src/solver.hpp src/dimacs.hpp

all: $(BINS)

$(B)/sat_solver: $(SOLVER_SRC) $(SOLVER_HDR)
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -o $@ $(SOLVER_SRC)
$(B)/rup_check: checker/rup_check.cpp checker/cnf.hpp
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -o $@ $<
$(B)/model_check: checker/model_check.cpp checker/cnf.hpp
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -o $@ $<
$(B)/brute: tools/brute.cpp checker/cnf.hpp
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -o $@ $<
$(B)/dpll: baseline/dpll.cpp src/dimacs.hpp
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Experiment E1 (docs/RESULTS.md): software prefetch of watch lists.  Not part of `all`.
$(B)/sat_solver_prefetch: $(SOLVER_SRC) $(SOLVER_HDR)
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -DSAT_PREFETCH -o $@ $(SOLVER_SRC)

# The same programs under AddressSanitizer + UndefinedBehaviorSanitizer + libstdc++ assertions.
sanitize-build: $(SAN_BINS)
$(BS)/sat_solver: $(SOLVER_SRC) $(SOLVER_HDR)
	@mkdir -p $(BS)
	$(CXX) $(SANFLAGS) -o $@ $(SOLVER_SRC)
$(BS)/rup_check: checker/rup_check.cpp checker/cnf.hpp
	@mkdir -p $(BS)
	$(CXX) $(SANFLAGS) -o $@ $<
$(BS)/model_check: checker/model_check.cpp checker/cnf.hpp
	@mkdir -p $(BS)
	$(CXX) $(SANFLAGS) -o $@ $<
$(BS)/brute: tools/brute.cpp checker/cnf.hpp
	@mkdir -p $(BS)
	$(CXX) $(SANFLAGS) -o $@ $<
$(BS)/dpll: baseline/dpll.cpp src/dimacs.hpp
	@mkdir -p $(BS)
	$(CXX) $(SANFLAGS) -o $@ $<

# ---- tests (see tests/README.md) -------------------------------------------
test: all                 ## quick suite, about a minute
	$(PYTHON) tests/run_all.py --quick
test-full: all            ## everything, including the large-scale instances
	$(PYTHON) tests/run_all.py
sanitize: sanitize-build  ## quick suite on the sanitizer builds
	$(PYTHON) tests/run_all.py --quick --bin-dir $(BS)
mutation: all             ## inject bugs into solver / checker and see whether tests notice
	$(PYTHON) tests/mutation.py
bench: all                ## baseline vs. final solver, ablations
	$(PYTHON) bench/run_bench.py

clean:
	rm -rf $(B)

.PHONY: all sanitize-build test test-full sanitize mutation bench clean
