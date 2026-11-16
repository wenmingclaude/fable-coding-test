// bench -- in-process timings of each operation at full problem size.
//
//   bench [SEED]
//
// Every scenario starts from a fresh editor with |S0| = 200000, so the
// figures do not depend on the order of the scenarios.  Edits report time
// and frozen nodes per operation; queries report time per query.  The
// answers of the queries are folded into a checksum that is printed, so that
// two runs (e.g. with one and with two hash bases) can be compared.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "../src/editor.hpp"

namespace {

using tte::Editor;
using tte::u32;
using Clock = std::chrono::steady_clock;

std::mt19937_64 rng;
u32 rnd(u32 lo, u32 hi) { return lo + static_cast<u32>(rng() % (static_cast<std::uint64_t>(hi) - lo + 1)); }

std::string random_text(u32 n, u32 alphabet) {
    std::string s(n, 'a');
    for (char& c : s) c = static_cast<char>('a' + rnd(0, alphabet - 1));
    return s;
}

double seconds_since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

void report_edit(const char* name, long ops, double secs, const Editor& ed, std::size_t nodes_before) {
    const double nodes = static_cast<double>(ed.node_count() - nodes_before) / static_cast<double>(ops);
    std::printf("  %-44s %7.0f ns/op  %6.1f nodes/op  %6.0f B/op  depth %u\n", name, secs * 1e9 / static_cast<double>(ops),
                nodes, nodes * sizeof(tte::Node), ed.depth(ed.versions() - 1));
}
void report_query(const char* name, long ops, double secs, unsigned long long checksum) {
    std::printf("  %-44s %7.0f ns/op  (checksum %llu)\n", name, secs * 1e9 / static_cast<double>(ops), checksum);
}

const u32 kN = 200000;
const long kQ = 200000;

void range(u32 n, u32& l, u32& r) {
    l = rnd(0, n);
    r = rnd(0, n);
    if (l > r) std::swap(l, r);
}

// ---- edits ----------------------------------------------------------------

void bench_edits(std::uint64_t seed) {
    std::printf("edits (|S0| = %u, %ld operations each)\n", kN, kQ);
    const std::string s0 = random_text(kN, 26);
    {
        const auto t0 = Clock::now();
        Editor ed(s0, seed);
        std::printf("  %-44s %7.1f ms\n", "build initial version", seconds_since(t0) * 1e3);
    }
    {  // INSERT: 10000 x 20 characters uses the whole insertion budget
        Editor ed(s0, seed);
        const std::string piece = random_text(20, 26);
        const std::size_t before = ed.node_count();
        const auto t0 = Clock::now();
        for (int i = 0; i < 10000; ++i) ed.insert(i, rnd(0, ed.length(i)), piece);
        report_edit("INSERT 20 chars, newest version (x10000)", 10000, seconds_since(t0), ed, before);
    }
    for (int branchy = 0; branchy < 2; ++branchy) {
        const char* suffix = branchy ? "random old version" : "newest version";
        char name[80];
        {
            Editor ed(s0, seed);
            const std::size_t before = ed.node_count();
            const auto t0 = Clock::now();
            for (long i = 0; i < kQ; ++i) {
                const int v = branchy ? static_cast<int>(rnd(0, static_cast<u32>(i))) : static_cast<int>(i);
                u32 l, r;
                range(ed.length(v), l, r);
                ed.reverse(v, l, r);
            }
            std::snprintf(name, sizeof name, "REVERSE, %s", suffix);
            report_edit(name, kQ, seconds_since(t0), ed, before);
        }
        {
            Editor ed(s0, seed);
            const std::size_t before = ed.node_count();
            const auto t0 = Clock::now();
            for (long i = 0; i < kQ; ++i) {
                const int v = branchy ? static_cast<int>(rnd(0, static_cast<u32>(i))) : static_cast<int>(i);
                u32 l, r;
                range(ed.length(v), l, r);
                ed.move(v, l, r, rnd(0, ed.length(v) - (r - l)));
            }
            std::snprintf(name, sizeof name, "MOVE, %s", suffix);
            report_edit(name, kQ, seconds_since(t0), ed, before);
        }
    }
    {  // DELETE of short ranges (long ones would empty the text)
        Editor ed(s0, seed);
        const std::size_t before = ed.node_count();
        const auto t0 = Clock::now();
        for (long i = 0; i < kQ; ++i) {
            const int v = static_cast<int>(rnd(0, static_cast<u32>(i)));
            const u32 n = ed.length(v), l = rnd(0, n), r = std::min(n, l + rnd(0, 20));
            ed.erase(v, l, r);
        }
        report_edit("DELETE <= 20 chars, random old version", kQ, seconds_since(t0), ed, before);
    }
    {  // The largest footprint the limits allow: grow to 400000, then only MOVE.
        Editor ed(s0, seed);
        const std::string piece = random_text(20, 26);
        const std::size_t before = ed.node_count();
        const auto t0 = Clock::now();
        for (int i = 0; i < 10000; ++i) ed.insert(i, rnd(0, ed.length(i)), piece);
        for (long i = 10000; i < kQ; ++i) {
            u32 l, r;
            range(400000, l, r);
            ed.move(static_cast<int>(i), l, r, rnd(0, 400000 - (r - l)));
        }
        report_edit("worst case: 10000 INSERT + 190000 MOVE", kQ, seconds_since(t0), ed, before);
        std::printf("  %-44s %7.1f MiB in %zu nodes\n", "  total node memory", static_cast<double>(ed.memory_bytes()) / 1048576.0,
                    ed.node_count());
    }
}

// ---- queries --------------------------------------------------------------

// An editor with `versions` versions made by random REVERSE/MOVE (and a few
// INSERT/DELETE) on random old versions: a wide, deep version tree whose
// texts are all rearrangements of nearly the same characters.
void grow_history(Editor& ed, int versions) {
    while (ed.versions() < versions) {
        const int v = static_cast<int>(rnd(0, static_cast<u32>(ed.versions() - 1)));
        const u32 n = ed.length(v);
        u32 l, r;
        range(n, l, r);
        switch (rnd(0, 9)) {
            case 0: ed.insert(v, l, "b"); break;
            case 1: ed.erase(v, l, std::min(n, l + 1)); break;
            case 2: case 3: case 4: case 5: ed.reverse(v, l, r); break;
            default: ed.move(v, l, r, rnd(0, n - (r - l))); break;
        }
    }
}

void bench_queries(std::uint64_t seed, const char* title, const std::string& s0, int versions) {
    std::printf("queries on %s, %d versions (%ld queries each)\n", title, versions, kQ);
    Editor ed(s0, seed);
    grow_history(ed, versions);
    auto any = [&] { return static_cast<int>(rnd(0, static_cast<u32>(versions - 1))); };
    unsigned long long sum = 0;

    auto t0 = Clock::now();
    for (long i = 0; i < kQ; ++i) {
        const int v1 = any(), v2 = any();
        u32 l1, r1;
        range(ed.length(v1), l1, r1);
        const u32 len = std::min(r1 - l1, ed.length(v2));
        const u32 l2 = rnd(0, ed.length(v2) - len);
        sum += ed.equal(v1, l1, l1 + len, v2, l2, l2 + len);
    }
    report_query("EQUAL, random ranges of equal length", kQ, seconds_since(t0), sum);

    sum = 0;
    t0 = Clock::now();
    for (long i = 0; i < kQ; ++i) {
        const int v = any();
        u32 l, r;
        range(ed.length(v), l, r);
        sum += ed.palindrome(v, l, r);
    }
    report_query("PAL, random ranges", kQ, seconds_since(t0), sum);

    sum = 0;
    t0 = Clock::now();
    for (long i = 0; i < kQ; ++i) {
        const int v1 = any(), v2 = any();
        sum += ed.lcp(v1, rnd(0, ed.length(v1)), v2, rnd(0, ed.length(v2)));
    }
    report_query("LCP, random positions", kQ, seconds_since(t0), sum);
    std::printf("  %-44s %7.1f      (mean answer)\n", "", static_cast<double>(sum) / static_cast<double>(kQ));

    sum = 0;
    t0 = Clock::now();
    for (long i = 0; i < kQ; ++i) {
        const int v1 = any(), v2 = any();
        const u32 p = rnd(0, std::min(ed.length(v1), ed.length(v2)));
        sum += ed.lcp(v1, p, v2, p);
    }
    report_query("LCP, same position in two versions", kQ, seconds_since(t0), sum);
    std::printf("  %-44s %7.1f      (mean answer)\n", "", static_cast<double>(sum) / static_cast<double>(kQ));
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1;
    rng.seed(seed);
    std::printf("node size %zu bytes, hash bases %zu, seed %llu\n", sizeof(tte::Node), tte::kBases,
                static_cast<unsigned long long>(seed));
    bench_edits(seed);

    bench_queries(seed, "random text (26 letters)", random_text(kN, 26), 2000);
    bench_queries(seed, "one repeated letter", std::string(kN, 'a'), 2000);
    {
        // Period-2 text: half of all substring pairs are equal, and LCPs
        // between unrelated positions are long but end at arbitrary places.
        std::string s(kN, 'a');
        for (u32 i = 1; i < kN; i += 2) s[i] = 'b';
        bench_queries(seed, "period-2 text (abab...)", s, 2000);
    }
    return 0;
}
