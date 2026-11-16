// stress -- full-size runs (|S0| = 200000, 200000 operations) checked
// against plain strings.
//
//   stress [SEED]
//
// Keeping 200000 versions of a 200000-character string is impossible for a
// naive model, so the model keeps the text of a bounded set of "live"
// versions only: the few newest ones and a handful of pinned old ones
// (version 0 is pinned forever, others are pinned for tens of thousands of
// operations).  Edits are based on live versions and queries compare live
// versions, so every answer is checked exactly, and the pinned versions show
// that a version stays intact while 100000+ later versions are derived
// around it.  At the end every live version is read back in full and its
// structural invariants are recomputed.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../src/editor.hpp"

namespace {

using tte::Editor;
using tte::u32;

std::mt19937_64 rng;
u32 rnd(u32 lo, u32 hi) { return lo + static_cast<u32>(rng() % (static_cast<std::uint64_t>(hi) - lo + 1)); }

[[noreturn]] void die(const char* scenario, const char* what, long step) {
    std::printf("STRESS FAILURE in '%s': %s (operation %ld)\n", scenario, what, step);
    std::exit(1);
}

// A range whose length is short, medium or long with equal probability.
void range(u32 n, u32& l, u32& r) {
    const u32 max_len = std::min(n, rnd(0, 2) == 0 ? 8u : rnd(0, 1) ? 2000u : n);
    const u32 len = rnd(0, max_len);
    l = rnd(0, n - len);
    r = l + len;
}

// `grow` > 0: the first `grow` operations are 20-character INSERTs into the
// newest version, which brings the text to its largest legal length.
void run(const char* name, std::string s0, int insert_percent, std::uint64_t seed, long grow = 0) {
    const auto t0 = std::chrono::steady_clock::now();
    const long q = 200000;
    Editor ed(s0, seed);
    std::map<int, std::string> live;  // version -> its text, for live versions only
    live[0] = std::move(s0);
    std::vector<int> pinned{0};
    const std::size_t window = 5, max_pinned = 6;
    long edits = 0, queries = 0, yes = 0, pinned_reads = 0;
    unsigned long long lcp_total = 0;
    u32 max_len = ed.length(0);
    long budget = 200000;  // total inserted characters allowed by the statement

    auto pick_live = [&]() -> int {
        auto it = live.begin();
        std::advance(it, rnd(0, static_cast<u32>(live.size() - 1)));
        return it->first;
    };

    for (long step = 0; step < q; ++step) {
        const int newest = ed.versions() - 1;
        const u32 kind = step < grow ? 0 : rnd(0, 99);
        if (kind < 55) {  // ---- edit ----
            const int base = (step < grow || rnd(0, 9) < 6) ? newest : pick_live();
            std::string t = live.at(base);
            const u32 n = static_cast<u32>(t.size());
            u32 l, r;
            range(n, l, r);
            int v;
            const u32 op = step < grow ? 0 : rnd(0, 99);
            if ((step < grow || op < static_cast<u32>(insert_percent)) && budget > 0) {
                std::string piece(static_cast<std::size_t>(std::min<long>(budget, step < grow ? 20 : rnd(1, 20))), 'a');
                for (char& c : piece) c = t.empty() ? 'a' : t[rnd(0, n - 1)];  // letters of the text itself
                budget -= static_cast<long>(piece.size());
                t.insert(l, piece);
                v = ed.insert(base, l, piece);
            } else if (op < static_cast<u32>(insert_percent) + 8) {
                r = std::min(r, l + 25);  // keep the text long
                t.erase(l, r - l);
                v = ed.erase(base, l, r);
            } else if (op < 60) {
                std::reverse(t.begin() + l, t.begin() + r);
                v = ed.reverse(base, l, r);
            } else {
                const u32 p = rnd(0, n - (r - l));
                const std::string cut = t.substr(l, r - l);
                t.erase(l, r - l);
                t.insert(p, cut);
                v = ed.move(base, l, r, p);
            }
            if (v != newest + 1) die(name, "edit returned the wrong version number", step);
            ++edits;
            max_len = std::max(max_len, static_cast<u32>(t.size()));
            if (ed.length(v) != t.size()) die(name, "new version has the wrong length", step);
            live[v] = std::move(t);

            // Occasionally pin the new version; otherwise it lives while it is
            // recent.  The first two pins are never released, the others rotate.
            if (rnd(0, 2999) == 0) {
                if (pinned.size() < max_pinned) pinned.push_back(v);
                else pinned[rnd(3, static_cast<u32>(max_pinned - 1))] = v;
            }
            for (auto it = live.begin(); it != live.end();) {
                const bool recent = it->first + static_cast<int>(window) > v;
                const bool keep = recent || std::find(pinned.begin(), pinned.end(), it->first) != pinned.end();
                it = keep ? std::next(it) : live.erase(it);
            }
            // Spot-check characters of the new version through a query.
            const std::string& now = live.at(v);
            if (!now.empty() && rnd(0, 7) == 0) {
                const u32 i = rnd(0, static_cast<u32>(now.size()) - 1), len = std::min<u32>(40, static_cast<u32>(now.size()) - i);
                const int w = pick_live();
                const std::string& other = live.at(w);
                const std::size_t at = other.find(now.substr(i, len));
                if (at != std::string::npos && !ed.equal(v, i, i + len, w, static_cast<u32>(at), static_cast<u32>(at) + len))
                    die(name, "EQUAL denies a true occurrence", step);
            }

        } else {  // ---- query ----
            const int v1 = pick_live(), v2 = rnd(0, 3) ? pick_live() : v1;
            const std::string& a = live.at(v1);
            const std::string& b = live.at(v2);
            const u32 na = static_cast<u32>(a.size()), nb = static_cast<u32>(b.size());
            pinned_reads += (v1 != newest && v1 + static_cast<int>(window) <= newest);
            ++queries;
            if (kind < 70) {
                u32 l1, r1;
                range(na, l1, r1);
                if (r1 - l1 > nb) r1 = l1 + nb;
                const u32 len = r1 - l1;
                const u32 l2 = (rnd(0, 1) && l1 + len <= nb) ? l1 : rnd(0, nb - len);
                const bool want = a.compare(l1, len, b, l2, len) == 0;
                yes += want;
                if (ed.equal(v1, l1, r1, v2, l2, l2 + len) != want) die(name, "EQUAL differs from the model", step);
            } else if (kind < 82) {
                u32 l, r;
                range(na, l, r);
                bool want = true;
                for (u32 i = l, j = r; i < j; ++i) { --j; if (i < j && a[i] != a[j]) { want = false; break; } }
                yes += want;
                if (ed.palindrome(v1, l, r) != want) die(name, "PAL differs from the model", step);
            } else {
                u32 p1 = rnd(0, na), p2 = rnd(0, 1) ? std::min(p1, nb) : rnd(0, nb);
                u32 want = 0;
                while (p1 + want < na && p2 + want < nb && a[p1 + want] == b[p2 + want]) ++want;
                lcp_total += want;
                if (ed.lcp(v1, p1, v2, p2) != want) die(name, "LCP differs from the model", step);
            }
        }
    }

    // Final audit of every live version, pinned old ones included.
    u32 max_depth = 0;
    for (const auto& [v, t] : live) {
        if (ed.text(v) != t) die(name, "a live version has the wrong text at the end", v);
        const std::string problem = ed.check_invariants(v);
        if (!problem.empty()) die(name, problem.c_str(), v);
        max_depth = std::max(max_depth, ed.depth(v));
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("ok   %-28s %ld edits, %ld queries (%ld YES, mean LCP %.0f, %ld on old pinned versions)\n", name, edits,
                queries, yes, static_cast<double>(lcp_total) / static_cast<double>(std::max(1L, queries)), pinned_reads);
    std::printf("     max length %u, %d versions, %zu nodes (%.0f MiB), max depth %u, oldest pinned version %d, %.1f s incl. model\n",
                max_len, ed.versions(), ed.node_count(), static_cast<double>(ed.memory_bytes()) / 1048576.0, max_depth,
                pinned.size() > 1 ? *std::min_element(pinned.begin() + 1, pinned.end()) : 0, secs);
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1;
    rng.seed(seed);
    const u32 n = 200000;

    std::string random26(n, 'a');
    for (char& c : random26) c = static_cast<char>('a' + rnd(0, 25));
    run("random text", random26, 10, seed);

    std::string sparse(n, 'a');  // long runs: long LCPs, long palindromes, many equal substrings
    for (int i = 0; i < 12; ++i) sparse[rnd(0, n - 1)] = 'b';
    run("a...a with a few b", sparse, 10, seed + 1);

    std::string periodic(n, 'a');
    for (u32 i = 0; i < n; ++i) periodic[i] = static_cast<char>('a' + i % 3);
    run("period 3 (abcabc...)", periodic, 10, seed + 2);

    std::string binary(n, 'a');
    for (char& c : binary) c = static_cast<char>('a' + rnd(0, 1));
    run("random binary, insert-heavy", binary, 45, seed + 3);

    std::string grown(n, 'a');
    for (char& c : grown) c = static_cast<char>('a' + rnd(0, 3));
    run("grown to 400000 first", grown, 0, seed + 4, 10000);

    std::printf("stress: all scenarios consistent with the string model\n");
    return 0;
}
