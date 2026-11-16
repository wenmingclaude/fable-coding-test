CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
BUILD    := build
HEADERS  := $(wildcard src/*.hpp)
BINARIES := tte naive gen unit fuzz stress bench measure

SAN_FLAGS := -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
             -D_GLIBCXX_ASSERTIONS -DTTE_POOL_STRESS

.PHONY: all test fuzz sanitize dual collide mutation stress bench clean

all: $(addprefix $(BUILD)/,$(BINARIES))

$(BUILD)/tte:    src/main.cpp
$(BUILD)/naive:  verify/naive.cpp
$(BUILD)/gen:    tests/gen.cpp
$(BUILD)/unit:   tests/unit.cpp
$(BUILD)/fuzz:   tests/fuzz.cpp
$(BUILD)/stress: tests/stress.cpp
$(BUILD)/bench:  bench/bench.cpp
$(BUILD)/measure: bench/measure.cpp

$(BUILD)/%: $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@

# Unit tests, the sample, a short white-box fuzz and a short black-box diff.
test: all
	$(BUILD)/unit
	@$(BUILD)/tte < examples/sample.in | cmp -s - examples/sample.out \
		&& echo "sample: ok" || { echo "sample: FAILED"; exit 1; }
	$(BUILD)/fuzz 300 $(or $(SEED),1)
	tests/diff.sh 5 $(or $(SEED),1)

# The long random runs.  SEED=... picks different inputs.
fuzz: all
	$(BUILD)/fuzz 5000 $(or $(SEED),1)
	tests/diff.sh 60 $(or $(SEED),1)

# Everything again under AddressSanitizer + UBSan, with a node pool that
# moves on every allocation (so a stale node reference is a reported error).
sanitize:
	@mkdir -p $(BUILD)/san
	$(CXX) $(SAN_FLAGS) tests/unit.cpp -o $(BUILD)/san/unit
	$(CXX) $(SAN_FLAGS) tests/fuzz.cpp -o $(BUILD)/san/fuzz
	$(CXX) $(SAN_FLAGS) src/main.cpp   -o $(BUILD)/san/tte
	$(BUILD)/san/unit
	$(BUILD)/san/fuzz 150 $(or $(SEED),1)
	tests/diff.sh 2 $(or $(SEED),1) $(BUILD)/san/tte

# Everything again with two independent hash evaluation points (48-byte nodes).
dual:
	@mkdir -p $(BUILD)/dual
	$(CXX) $(CXXFLAGS) -DTTE_HASH_BASES=2 tests/unit.cpp -o $(BUILD)/dual/unit
	$(CXX) $(CXXFLAGS) -DTTE_HASH_BASES=2 tests/fuzz.cpp -o $(BUILD)/dual/fuzz
	$(CXX) $(CXXFLAGS) -DTTE_HASH_BASES=2 src/main.cpp   -o $(BUILD)/dual/tte
	$(BUILD)/dual/unit
	$(BUILD)/dual/fuzz 300 $(or $(SEED),1)
	tests/diff.sh 3 $(or $(SEED),1) $(BUILD)/dual/tte

# The fuzz test with a 13-bit hash modulus: collisions now happen all the
# time.  Texts, invariants and the frozen-pool audit must still be exact, and
# every wrong query answer must be wrong in the permitted direction.
collide:
	@mkdir -p $(BUILD)/collide
	$(CXX) $(CXXFLAGS) -DTTE_MERSENNE_EXPONENT=13 tests/fuzz.cpp -o $(BUILD)/collide/fuzz
	$(BUILD)/collide/fuzz 1000 $(or $(SEED),1)

# Plants bugs in a copy of the editor and checks that the tests notice each one.
mutation: all
	python3 tests/mutation.py

# Full-size inputs (|S0| = q = 200000) checked against a string model.
stress: all
	$(BUILD)/stress

# Time and memory at full size.
bench: all
	bench/run.sh

clean:
	rm -rf $(BUILD)
