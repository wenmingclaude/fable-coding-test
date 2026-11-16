#!/usr/bin/env bash
# Performance test on large inputs.
#
# For each workload: generate the input file, run the real `solver` binary on
# it (stdin -> stdout, exactly as a user would), and record wall-clock time
# and peak memory as measured by the kernel. The independent `rollback`
# implementation runs on the same file and the two outputs must be identical.
#
# Times are the best of 3 runs and include reading and writing the text.
# usage: bench/bench.sh [build directory]
set -eu
build=${1:-build}
dir=$build/bench
mkdir -p "$dir"

run() {  # run <binary> <input> <output>  ->  "seconds MiB" of the fastest of 3 runs
    for _ in 1 2 3; do
        "$build/measure" "$build/$1" < "$2" 2>&1 > "$3"
    done | sort -n | head -n 1 | awk '{ printf "%.3f %.1f", $1, $3 / 1024 }'
}

echo "| workload | n | q | solver s | solver MiB | rollback s | rollback MiB | outputs | values / UNKNOWN / CONTRADICTION |"
echo "|---|---:|---:|---:|---:|---:|---:|---|---|"

bench() {  # bench <workload> <n> <q>
    local name=$1 n=$2 q=$3
    local in=$dir/$name-$n-$q.in fast=$dir/fast.out slow=$dir/slow.out
    "$build/gen" "$name" "$n" "$q" 12345 > "$in"
    read -r fast_s fast_mb <<< "$(run solver "$in" "$fast")"
    read -r slow_s slow_mb <<< "$(run rollback "$in" "$slow")"
    local verdict=identical
    cmp -s "$fast" "$slow" || verdict=DIFFERENT
    local unknown contradiction lines
    lines=$(wc -l < "$fast")
    unknown=$(grep -c UNKNOWN "$fast" || true)
    contradiction=$(grep -c CONTRADICTION "$fast" || true)
    echo "| $name | $n | $q | $fast_s | $fast_mb | $slow_s | $slow_mb | $verdict | $((lines - unknown - contradiction)) / $unknown / $contradiction |"
    [ "$verdict" = identical ]
}

# The size limits of the problem, on differently shaped histories.
bench mixed         200000 200000
bench mixed          20000 200000
bench mixed            500 200000
bench window        200000 200000
bench window         20000 200000
bench stack          20000 200000
bench grow          200000 200000
bench grow           50000 200000
bench chain-churn   100000 200000
bench chain-queries 100000 200000
bench chain-queries 199999 200000
bench storm         100000 200000
bench deep-merges    65536 200000
bench deep-merges   131072 200000

# Doubling series: how does the running time grow with the input?
for q in 25000 50000 100000 200000 400000 800000; do
    bench window $((q / 10)) "$q"
done
