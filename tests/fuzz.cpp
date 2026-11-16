// fuzz -- white-box random testing against an in-process model.
//
//   fuzz [ROUNDS] [SEED]
//
// The model is a std::vector<std::string>: one plain string per version.
// After EVERY edit the harness checks, in addition to the new version's text:
//
//   * frozen-pool audit: every node that existed before the edit is
//     byte-for-byte what it was (memcmp against a private copy).  This is
//     the direct test of "old versions are never modified": no edit, however
//     it combines reversals, moves and sharing, may write to an old node.
//   * structural invariants of the new version (sizes, heap order, stored
//     hashes versus hashes recomputed from the characters).
//   * the tree is balanced and the edit allocated O(log n) nodes.
//
// and at the end of each round every version is read back in full and its
// invariants are re-checked, so damage to old versions cannot hide.
// Queries are compared with the model, and lcp() with lcp_reference().
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../src/editor.hpp"

namespace {

using tte::Editor;
using tte::u32;

std::mt19937_64 rng;
u32 rnd(u32 lo, u32 hi) { return lo + static_cast<u32>(rng() % (static_cast<std::uint64_t>(hi) - lo + 1)); }

struct Totals {
    long edits = 0, queries = 0, yes = 0, long_lcp = 0, versions_reread = 0, collisions = 0;
    std::size_t max_new_nodes = 0;
    u32 max_depth = 0;
} totals;

[[noreturn]] void die(const char* what, std::uint64_t seed, int round, long step) {
    std::printf("FUZZ FAILURE: %s (seed %llu, round %d, step %ld)\n", what,
                static_cast<unsigned long long>(seed), round, step);
    std::exit(1);
}

// With the real modulus every answer must equal the model's.  When built
// with a tiny modulus (make collide) hash collisions do happen; then a wrong
// answer is tolerated, and counted, only in the direction the design allows:
// "equal"/"palindrome" claimed for texts that are not, or an LCP too long.
const bool kTolerant = tte::kExponent < 61;
bool acceptable(bool got, bool want) {
    if (got == want) return true;
    if (kTolerant && got && !want) return ++totals.collisions, true;
    return false;
}
bool acceptable_lcp(u32 got, u32 want) {
    if (got == want) return true;
    if (kTolerant && got > want) return ++totals.collisions, true;
    return false;
}

void one_round(std::uint64_t seed, int round) {
    const u32 alphabet = rnd(0, 3) == 0 ? rnd(1, 2) : rnd(1, 26);
    auto text = [&](u32 n) {
        std::string s(n, 'a');
        for (char& c : s) c = static_cast<char>('a' + rnd(0, alphabet - 1));
        return s;
    };
    const u32 n0 = rnd(0, 5) == 0 ? rnd(0, 3) : rnd(1, rnd(0, 4) == 0 ? 3000 : 120);
    const int steps = static_cast<int>(rnd(20, 400));
    // How edits pick their base: 0 newest, 1 uniform, 2 one fixed version, 3 mixture.
    const u32 base_style = rnd(0, 3);

    std::vector<std::string> model{text(n0)};
    Editor ed(model[0], rng());
    std::vector<tte::Node> frozen(ed.nodes(), ed.nodes() + ed.node_count());

    auto range = [&](u32 n, u32& l, u32& r) {
        switch (rnd(0, 7)) {
            case 0: l = r = rnd(0, n); break;
            case 1: l = 0; r = n; break;
            case 2: l = 0; r = rnd(0, n); break;
            case 3: l = rnd(0, n); r = n; break;
            default: l = rnd(0, n); r = rnd(0, n); if (l > r) std::swap(l, r);
        }
    };

    for (int step = 0; step < steps; ++step) {
        const int newest = ed.versions() - 1;
        auto any = [&] { return static_cast<int>(rnd(0, static_cast<u32>(newest))); };
        int base = newest;
        if (base_style == 1) base = any();
        if (base_style == 2) base = std::min(newest, 1);
        if (base_style == 3) base = rnd(0, 2) ? any() : newest;
        const std::string& src = model[static_cast<std::size_t>(base)];
        const u32 n = static_cast<u32>(src.size());

        const u32 op = rnd(0, 9);
        if (op <= 5) {  // ---- an edit ----
            std::string want = src;
            u32 l, r;
            range(n, l, r);
            int v;
            if (op == 0 || (op == 1 && n < 50)) {
                const u32 p = rnd(0, n);
                const std::string s = text(rnd(1, 20));
                want.insert(p, s);
                v = ed.insert(base, p, s);
            } else if (op == 1) {
                if (r - l > 30) r = l + rnd(0, 30);
                want.erase(l, r - l);
                v = ed.erase(base, l, r);
            } else if (op <= 3) {
                std::reverse(want.begin() + l, want.begin() + r);
                v = ed.reverse(base, l, r);
            } else {
                const u32 p = rnd(0, n - (r - l));
                const std::string cut = want.substr(l, r - l);
                want.erase(l, r - l);
                want.insert(p, cut);
                v = ed.move(base, l, r, p);
            }
            ++totals.edits;
            if (v != newest + 1) die("edit returned the wrong version number", seed, round, step);

            // frozen-pool audit
            if (ed.node_count() < frozen.size() ||
                std::memcmp(ed.nodes(), frozen.data(), frozen.size() * sizeof(tte::Node)) != 0)
                die("an edit modified a node of an older version", seed, round, step);
            const std::size_t fresh = ed.node_count() - frozen.size();
            totals.max_new_nodes = std::max(totals.max_new_nodes, fresh);
            if (static_cast<double>(fresh) > 40 * std::log2(n + 2.0) + 64) die("an edit allocated far more than O(log n) nodes", seed, round, step);
            frozen.insert(frozen.end(), ed.nodes() + frozen.size(), ed.nodes() + ed.node_count());

            if (ed.text(v) != want) die("new version has the wrong text", seed, round, step);
            if (!ed.check_invariants(v).empty()) die(ed.check_invariants(v).c_str(), seed, round, step);
            const u32 depth = ed.depth(v);
            totals.max_depth = std::max(totals.max_depth, depth);
            if (depth > 12 * std::log2(static_cast<double>(want.size()) + 2.0) + 12) die("tree is badly unbalanced", seed, round, step);
            model.push_back(std::move(want));
            // the base must be untouched right away, not only at the end
            if (ed.text(base) != model[static_cast<std::size_t>(base)]) die("base version changed", seed, round, step);

        } else {  // ---- a query ----
            const int v1 = any(), v2 = rnd(0, 2) ? any() : v1;
            const std::string& a = model[static_cast<std::size_t>(v1)];
            const std::string& b = model[static_cast<std::size_t>(v2)];
            const u32 na = static_cast<u32>(a.size()), nb = static_cast<u32>(b.size());
            ++totals.queries;
            if (op == 6) {
                u32 l1, r1;
                range(na, l1, r1);
                if (r1 - l1 > nb) r1 = l1 + nb;
                u32 l2 = rnd(0, nb - (r1 - l1)), r2 = l2 + (r1 - l1);
                if (rnd(0, 1) && r1 > l1) {  // aim at a real occurrence
                    const std::size_t at = b.find(a.substr(l1, std::min<u32>(r1 - l1, 6)), rnd(0, nb));
                    if (at != std::string::npos && at + (r1 - l1) <= nb) { l2 = static_cast<u32>(at); r2 = l2 + (r1 - l1); }
                }
                if (rnd(0, 9) == 0) r2 = rnd(l2, nb);  // sometimes unequal lengths
                const bool want = a.substr(l1, r1 - l1) == b.substr(l2, r2 - l2);
                totals.yes += want;
                if (!acceptable(ed.equal(v1, l1, r1, v2, l2, r2), want)) die("EQUAL differs from the model", seed, round, step);
            } else if (op == 7) {
                u32 l, r;
                range(na, l, r);
                if (rnd(0, 1) && na > 0) {  // grow a real palindrome
                    l = rnd(0, na - 1); r = l + rnd(0, 1);
                    while (l > 0 && r < na && a[l - 1] == a[r]) { --l; ++r; }
                    if (rnd(0, 4) == 0 && r < na) ++r;
                }
                bool want = true;
                for (u32 i = l, j = r; i < j; ++i) { --j; if (i < j && a[i] != a[j]) want = false; }
                totals.yes += want;
                if (!acceptable(ed.palindrome(v1, l, r), want)) die("PAL differs from the model", seed, round, step);
            } else {
                u32 p1 = rnd(0, na), p2 = rnd(0, nb);
                if (rnd(0, 2) == 0) p2 = std::min(p1, nb);
                u32 want = 0;
                while (p1 + want < na && p2 + want < nb && a[p1 + want] == b[p2 + want]) ++want;
                totals.long_lcp += want >= 16;
                if (!acceptable_lcp(ed.lcp(v1, p1, v2, p2), want)) die("LCP differs from the model", seed, round, step);
                if (!acceptable_lcp(ed.lcp_reference(v1, p1, v2, p2), want)) die("lcp_reference differs from the model", seed, round, step);
            }
            if (ed.node_count() != frozen.size()) die("a query allocated nodes", seed, round, step);
        }
    }

    // Every version, old and new, is read back and re-verified.
    for (int v = 0; v < ed.versions(); ++v) {
        if (ed.text(v) != model[static_cast<std::size_t>(v)]) die("an old version changed", seed, round, v);
        if (!ed.check_invariants(v).empty()) die("an old version lost its invariants", seed, round, v);
        ++totals.versions_reread;
    }
    if (std::memcmp(ed.nodes(), frozen.data(), frozen.size() * sizeof(tte::Node)) != 0)
        die("a query modified a node", seed, round, -1);
}

}  // namespace

int main(int argc, char** argv) {
    const int rounds = argc > 1 ? std::atoi(argv[1]) : 300;
    const std::uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
    for (int round = 0; round < rounds; ++round) {
        rng.seed(seed * 1000003 + static_cast<std::uint64_t>(round));
        one_round(seed, round);
    }
    std::printf("fuzz: %d rounds (seed %llu): %ld edits, %ld queries (%ld YES, %ld LCP >= 16), "
                "%ld versions re-read; max new nodes per edit %zu, max depth %u -- all consistent\n",
                rounds, static_cast<unsigned long long>(seed), totals.edits, totals.queries, totals.yes,
                totals.long_lcp, totals.versions_reread, totals.max_new_nodes, totals.max_depth);
    if (kTolerant) {
        std::printf("fuzz: hash modulus 2^%d - 1: %ld answers were wrong because of collisions, every one of them "
                    "in the permitted direction\n", tte::kExponent, totals.collisions);
        if (totals.collisions == 0) {
            std::printf("fuzz: expected to see collisions with this modulus\n");
            return 1;
        }
    }
    return 0;
}
