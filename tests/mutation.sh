#!/usr/bin/env bash
# Mutation test: are the tests able to notice a wrong solver?
#
# One small bug at a time is planted into a scratch copy of the solver. The
# unit tests and a short fuzz run are then executed against it, separately.
# Every mutant must be "killed" (noticed) -- ideally by both.
#
# Three mutants that were tried turned out to be EQUIVALENT (they cannot
# change any output) and are therefore not in the list:
#   * access() splicing at child[0] instead of child[1]: the code is
#     mirror-symmetric. Totals are sums in a commutative group, so only
#     flip() -- not the left/right order -- carries the direction.
#   * negate(0) returning p instead of 0: add() reduces it away before any
#     total is read.
#   * expiry = t + 1 instead of t: time t is the Remove itself, never a query.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
survivors=0
total=0

mutant() {  # mutant <file> <sed expression> <description>
    local file=$1 expression=$2 what=$3 unit=missed fuzz=missed
    total=$((total + 1))
    rm -rf "$work/tree"
    mkdir -p "$work/tree"
    cp -r "$root/src" "$root/verify" "$root/tests" "$work/tree/"
    sed -i "$expression" "$work/tree/$file"
    if cmp -s "$root/$file" "$work/tree/$file"; then
        echo "error: mutation did not change $file: $what"
        exit 2
    fi
    g++ -std=c++20 -O1 "$work/tree/tests/unit_tests.cpp" -o "$work/unit" || exit 2
    g++ -std=c++20 -O1 "$work/tree/tests/fuzz.cpp" -o "$work/fuzz" || exit 2
    (cd "$work" && timeout 20 ./unit) >/dev/null 2>&1 || unit=KILLED
    (cd "$work" && timeout 120 ./fuzz 1 0.05) >/dev/null 2>&1 || fuzz=KILLED
    [ "$unit" = missed ] && [ "$fuzz" = missed ] && survivors=$((survivors + 1))
    printf '%-7s %-7s %s\n' "$unit" "$fuzz" "$what"
} 2>/dev/null

printf '%-7s %-7s %s\n' unit fuzz 'planted bug'
forest=src/spanning_forest.hpp
solver=src/solver.hpp

# The eviction rule (maximum spanning forest by expiry).
mutant $forest 's/if (expiry > path.first_expiry) reroute(/if (false) reroute(/'    'never evict a forest edge'
mutant $forest 's/if (expiry > path.first_expiry) reroute(/if (expiry < path.first_expiry) reroute(/' 'evict when the new edge is the weaker one'
mutant $forest 's/std::min({l.first_expiry, n.expiry, r.first_expiry})/std::max({l.first_expiry, n.expiry, r.first_expiry})/' 'track the last expiry instead of the first'
mutant $forest 's/std::min(path.first_expiry, expiry)/std::max(path.first_expiry, expiry)/' 'a cycle lasts until its LAST edge expires'
mutant $forest 's/== target ? nearer : farther/== target ? farther : nearer/'       'search for the weakest edge in the wrong subtree'
mutant $forest '/std::swap(e.child\[0\], e.child\[1\]);/d'                           'rerouting does not swap the two sides'
mutant $forest 's/e.gain = negate(gain);/e.gain = gain;/'                           'rerouted edge keeps the old direction'
mutant $forest 's/        e.expiry = expiry;/        e.expiry = e.expiry;/'         'rerouted edge keeps the old expiry'
# Gains and their signs.
mutant $forest 's/const Walk cycle{subtract(path.total, gain)/const Walk cycle{add(path.total, gain)/' 'cycle gain with the wrong sign'
mutant $forest '/n.gain = negate(n.gain);/d'                                        'reversal forgets to negate the node gain'
mutant $forest '/n.total = negate(n.total);/d'                                      'reversal forgets to negate the subtree total'
mutant $forest 's/n.flipped = !n.flipped;/n.flipped = false;/'                      'reversal is never propagated to children'
mutant $forest '/^                push(g);$/d'                                      'splay skips a pending flip'
mutant $forest 's/e.parent = a;  \/\/ the edge hangs below a .../e.parent = b;/'    'new edge attached to the wrong endpoint'
mutant $forest 's/nodes_\[y\].child\[1\] = below;/nodes_[y].child[1] = 0;/'         'access never splices the lower path in'
# Arithmetic in Z_p.
mutant $forest 's/return a + b >= kP ? a + b - kP : a + b;/return a + b > kP ? a + b - kP : a + b;/' 'sum equal to p is not reduced'
mutant $forest 's/static_cast<Residue>((x % kP + kP) % kP)/static_cast<Residue>(x % kP + kP)/' 'd is left unreduced in [p, 2p)'
mutant $forest 's/static_cast<Residue>((x % kP + kP) % kP)/static_cast<Residue>(x % kP)/' 'negative d is not normalised'
# Time and the contradiction horizon.
mutant $solver 's/if (t < contradictory_until) {/if (t <= contradictory_until) {/'  'contradiction lasts one step too long'
mutant $solver 's/if (cycle \&\& cycle->gain != 0)/if (cycle \&\& cycle->gain == 0)/' 'consistent cycles are treated as contradictions'
mutant $solver 's/contradictory_until = std::max(contradictory_until, cycle->expiry);/contradictory_until = cycle->expiry;/' 'a later, shorter contradiction overwrites the horizon'
mutant $solver 's/const bool alive = path \&\& path->expiry > t;/const bool alive = path.has_value();/' 'expired paths still answer queries'
mutant $solver 's/expiry\[static_cast<std::size_t>(it->second)\] = t;/expiry[static_cast<std::size_t>(it->second)] = t - 1;/' 'constraints expire one step early'
mutant $solver '/born.erase(it);/d'                                                 'a removed id stays reserved'
mutant $solver 's/forest.insert(op.u, op.v, to_residue(op.d),/forest.insert(op.v, op.u, to_residue(op.d),/' 'constraint direction swapped'

echo "$((total - survivors)) of $total planted bugs were caught"
[ "$survivors" -eq 0 ]
