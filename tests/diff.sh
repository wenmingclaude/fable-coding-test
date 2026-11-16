#!/usr/bin/env bash
# Black-box differential test: random inputs through the real program and
# through the naive verifier; the outputs must be byte-identical.
#
#   tests/diff.sh [ROUNDS] [FIRST_SEED] [TTE_BINARY]
#
# Each round runs every generator mode at several sizes and alphabets.  On
# the first difference the failing input is kept in build/fail.in and the
# script exits with status 1.
set -u
rounds=${1:-20}
first=${2:-1}
tte=${3:-build/tte}
gen=build/gen
naive=build/naive
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

modes="mix chain branch revmove lcp pal edge flip"
cases=0
for ((round = 0; round < rounds; ++round)); do
    seed=$((first + round))
    for mode in $modes; do
        # N Q ALPHABET
        for shape in "1 30 2" "8 60 2" "8 60 26" "40 200 1" "40 200 3" "300 400 2" "300 400 26" "2000 600 4"; do
            set -- $shape
            "$gen" "$seed" "$1" "$2" "$mode" "$3" > "$tmp/in"
            "$naive" < "$tmp/in" > "$tmp/want"
            if ! "$tte" --seed "$seed" < "$tmp/in" > "$tmp/got" || ! cmp -s "$tmp/want" "$tmp/got"; then
                cp "$tmp/in" build/fail.in
                echo "MISMATCH: seed=$seed mode=$mode n=$1 q=$2 alphabet=$3 (input kept in build/fail.in)"
                diff "$tmp/want" "$tmp/got" | head -5
                exit 1
            fi
            cases=$((cases + 1))
        done
    done
done
echo "diff.sh: $cases inputs, seeds $first..$((first + rounds - 1)): all outputs identical"
