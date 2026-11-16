CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
BUILD    := build
HEADERS  := $(wildcard src/*.hpp verify/*.hpp tests/*.hpp)
BINARIES := solver brute rollback unit_tests fuzz gen measure

.PHONY: all test fuzz sanitize mutation bench clean

all: $(addprefix $(BUILD)/,$(BINARIES))

$(BUILD)/solver:     src/main.cpp
$(BUILD)/brute:      verify/brute_main.cpp
$(BUILD)/rollback:   verify/rollback_main.cpp
$(BUILD)/unit_tests: tests/unit_tests.cpp
$(BUILD)/fuzz:       tests/fuzz.cpp
$(BUILD)/gen:        tests/gen.cpp
$(BUILD)/measure:    bench/measure.cpp

$(BUILD)/%: $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@

# Unit tests, the sample through the real binaries, and a short fuzz run.
test: all
	$(BUILD)/unit_tests
	@for impl in solver rollback brute; do \
		$(BUILD)/$$impl < examples/sample.in | cmp -s - examples/sample.out \
			&& echo "sample via $$impl binary: ok" \
			|| { echo "sample via $$impl binary: FAILED"; exit 1; }; \
	done
	$(BUILD)/fuzz 1 0.1

# The full differential run (about a minute). Pass SEED=... to vary it.
SEED ?= 20260101
fuzz: all
	$(BUILD)/fuzz $(SEED) 1

# Unit tests and a fuzz run with bounds checks, AddressSanitizer and UBSan.
sanitize:
	@mkdir -p $(BUILD)/san
	$(CXX) -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		-D_GLIBCXX_ASSERTIONS tests/unit_tests.cpp -o $(BUILD)/san/unit_tests
	$(CXX) -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		-D_GLIBCXX_ASSERTIONS tests/fuzz.cpp -o $(BUILD)/san/fuzz
	$(BUILD)/san/unit_tests
	$(BUILD)/san/fuzz 7 0.05

# Plants bugs into a copy of the solver and checks that the tests notice.
mutation:
	tests/mutation.sh

# Large inputs (n, q up to 200000): time, memory, and a cross-check.
bench: all
	bench/bench.sh $(BUILD)

clean:
	rm -rf $(BUILD) fuzz-failure.in
