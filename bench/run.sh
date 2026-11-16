#!/usr/bin/env bash
# Whole-program measurements at full size (|S0| = 200000, q = 200000):
# wall time, CPU time and peak resident memory as reported by the kernel.
#
#   bench/run.sh [SEED]
#
# Then the in-process benchmark (per-operation costs).
set -eu
seed=${1:-1}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "whole program, input -> output (|S0| = 200000, q = 200000)"
# MODE ALPHABET  description
while read -r mode alphabet what; do
    build/gen "$seed" 200000 200000 "$mode" "$alphabet" > "$tmp/in"
    build/measure "$what" build/tte --seed "$seed" < "$tmp/in" > "$tmp/out"
    if [ -x build/dual/tte ]; then
        build/measure "  same, two hash bases" build/dual/tte --seed "$seed" < "$tmp/in" > "$tmp/out2"
        cmp -s "$tmp/out" "$tmp/out2" || { echo "OUTPUTS DIFFER between one and two hash bases"; exit 1; }
    fi
done <<'LIST'
mix 26 mixed operations, random versions
branch 3 heavy branching, 3 letters
revmove 2 REVERSE/MOVE interleaved, 2 letters
lcp 1 mostly LCP, one letter
lcp 26 mostly LCP, 26 letters
big 26 200000 x MOVE
worst 26 worst memory: grow to 400000, MOVE
LIST

echo
build/bench "$seed"
