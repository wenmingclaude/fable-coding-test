// gen -- random test inputs.
//
//   gen SEED N Q [MODE] [ALPHABET]
//
// N = |S0|, Q = number of operations, ALPHABET = number of distinct letters
// (1..26, default 26; small alphabets make equal substrings, palindromes and
// long common prefixes frequent).
//
// MODE picks the shape of the workload:
//   mix      every operation, on uniformly random old versions      (default)
//   chain    every edit is based on the newest version
//   branch   heavy forking: bases are version 0, i/2, random, newest
//   revmove  mostly REVERSE and MOVE interleaved with queries
//   lcp      mostly LCP queries
//   pal      palindromic text, mostly PAL queries
//   edge     empty ranges, whole ranges, positions 0 and |S|
//   flip     the same range reversed over and over, compared with old versions
//   big      edits only (for memory/time measurements): all MOVE
//   worst    10000 INSERTs of 20 letters (text grows to N + 200000), then only
//            MOVE of random ranges on the newest version: the largest memory
//            footprint the limits allow
//
// While the total text is small the generator keeps every version's text, so
// that it can aim queries at substrings that really are equal / palindromic;
// a purely random EQUAL would almost always be answered NO.  For large inputs
// it only tracks lengths.  All generated operations are valid.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

std::mt19937_64 rng;

long rnd(long lo, long hi) {  // uniform in [lo, hi]
    return lo + static_cast<long>(rng() % static_cast<std::uint64_t>(hi - lo + 1));
}
bool chance(int percent) { return rnd(0, 99) < percent; }

int alphabet = 26;
std::string random_text(long n) {
    std::string s(static_cast<std::size_t>(n), 'a');
    for (char& c : s) c = static_cast<char>('a' + rnd(0, alphabet - 1));
    return s;
}

bool edge_mode = false;
// A range [l, r) inside [0, n).
void random_range(long n, long& l, long& r) {
    const int style = static_cast<int>(rnd(0, edge_mode ? 5 : 9));
    if (style == 0) { l = r = rnd(0, n); }                       // empty
    else if (style == 1) { l = 0; r = n; }                       // everything
    else if (style == 2) { l = 0; r = rnd(0, n); }               // prefix
    else if (style == 3) { l = rnd(0, n); r = n; }               // suffix
    else if (style == 4) { l = rnd(0, n); r = std::min(n, l + rnd(0, 3)); }  // tiny
    else { l = rnd(0, n); r = rnd(0, n); if (l > r) std::swap(l, r); }
}
long random_pos(long n) {
    if (edge_mode || chance(10)) return chance(50) ? 0 : n;
    return rnd(0, n);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s SEED N Q [MODE] [ALPHABET]\n", argv[0]);
        return 2;
    }
    rng.seed(std::strtoull(argv[1], nullptr, 10));
    const long n = std::atol(argv[2]);
    const long q = std::atol(argv[3]);
    const std::string mode = argc > 4 ? argv[4] : "mix";
    if (argc > 5) alphabet = std::atoi(argv[5]);
    edge_mode = mode == "edge";

    // Keep full texts only while that stays cheap.
    const bool track = (n + 20 * q) * q <= 200'000'000L;

    std::string s0 = random_text(n);
    if (mode == "pal") for (long i = 0; i < n / 2; ++i) s0[static_cast<std::size_t>(n - 1 - i)] = s0[static_cast<std::size_t>(i)];
    std::vector<long> len{n};
    std::vector<std::string> text;
    if (track) text.push_back(s0);

    long insert_budget = 200000;
    long flip_l = 0, flip_r = 0;
    if (mode == "flip") random_range(n, flip_l, flip_r);

    // Operation weights: INSERT DELETE REVERSE MOVE EQUAL PAL LCP
    std::vector<int> w = {1, 1, 1, 1, 1, 1, 1};
    if (mode == "revmove") w = {1, 1, 8, 8, 3, 2, 2};
    if (mode == "lcp") w = {2, 1, 2, 2, 1, 1, 20};
    if (mode == "pal") w = {1, 1, 4, 2, 2, 12, 2};
    if (mode == "flip") w = {0, 0, 10, 0, 6, 2, 2};
    if (mode == "big" || mode == "worst") w = {0, 0, 0, 1, 0, 0, 0};
    std::discrete_distribution<int> pick(w.begin(), w.end());

    std::printf("%s\n%ld\n", s0.c_str(), q);
    for (long i = 0; i < q; ++i) {
        const long newest = static_cast<long>(len.size()) - 1;
        auto base = [&]() -> long {
            if (mode == "chain" || mode == "flip" || mode == "big" || mode == "worst") return newest;
            if (mode == "branch") {
                switch (rnd(0, 3)) {
                    case 0: return 0;
                    case 1: return (newest + 1) / 2;
                    case 2: return rnd(0, newest);
                    default: return newest;
                }
            }
            return chance(25) ? newest : rnd(0, newest);
        };
        auto any = [&]() -> long { return chance(20) ? newest : rnd(0, newest); };

        int op = pick(rng);
        if (op == 0 && insert_budget == 0) op = 2;
        if (mode == "worst" && insert_budget > 0) {  // growth phase
            const long v = newest;
            const std::string s = random_text(std::min(insert_budget, 20L));
            insert_budget -= static_cast<long>(s.size());
            std::printf("INSERT %ld %ld %s\n", v, rnd(0, len[static_cast<std::size_t>(v)]), s.c_str());
            len.push_back(len[static_cast<std::size_t>(v)] + static_cast<long>(s.size()));
            continue;
        }

        if (op == 0) {  // INSERT
            const long v = base(), p = random_pos(len[static_cast<std::size_t>(v)]);
            const std::string s = random_text(std::min(insert_budget, rnd(1, chance(30) ? 20 : 3)));
            insert_budget -= static_cast<long>(s.size());
            std::printf("INSERT %ld %ld %s\n", v, p, s.c_str());
            len.push_back(len[static_cast<std::size_t>(v)] + static_cast<long>(s.size()));
            if (track) { std::string t = text[static_cast<std::size_t>(v)]; t.insert(static_cast<std::size_t>(p), s); text.push_back(t); }

        } else if (op == 1) {  // DELETE
            const long v = base();
            long l, r;
            random_range(len[static_cast<std::size_t>(v)], l, r);
            if (!edge_mode && r - l > 40 && chance(90)) r = l + rnd(0, 40);  // keep texts from vanishing
            std::printf("DELETE %ld %ld %ld\n", v, l, r);
            len.push_back(len[static_cast<std::size_t>(v)] - (r - l));
            if (track) { std::string t = text[static_cast<std::size_t>(v)]; t.erase(static_cast<std::size_t>(l), static_cast<std::size_t>(r - l)); text.push_back(t); }

        } else if (op == 2) {  // REVERSE
            const long v = base();
            long l, r;
            if (mode == "flip" && chance(80)) { l = flip_l; r = flip_r; }
            else random_range(len[static_cast<std::size_t>(v)], l, r);
            std::printf("REVERSE %ld %ld %ld\n", v, l, r);
            len.push_back(len[static_cast<std::size_t>(v)]);
            if (track) { std::string t = text[static_cast<std::size_t>(v)]; std::reverse(t.begin() + l, t.begin() + r); text.push_back(t); }

        } else if (op == 3) {  // MOVE
            const long v = base();
            long l, r;
            random_range(len[static_cast<std::size_t>(v)], l, r);
            const long p = random_pos(len[static_cast<std::size_t>(v)] - (r - l));
            std::printf("MOVE %ld %ld %ld %ld\n", v, l, r, p);
            len.push_back(len[static_cast<std::size_t>(v)]);
            if (track) {
                std::string t = text[static_cast<std::size_t>(v)];
                const std::string cut = t.substr(static_cast<std::size_t>(l), static_cast<std::size_t>(r - l));
                t.erase(static_cast<std::size_t>(l), static_cast<std::size_t>(r - l));
                t.insert(static_cast<std::size_t>(p), cut);
                text.push_back(t);
            }

        } else if (op == 4) {  // EQUAL
            long v1 = any(), v2 = any();
            if (mode == "flip" && newest >= 2) { v1 = newest; v2 = newest - rnd(0, 2); }  // same / one flip / two flips ago
            long l1, r1, l2, r2;
            random_range(len[static_cast<std::size_t>(v1)], l1, r1);
            random_range(len[static_cast<std::size_t>(v2)], l2, r2);
            if (mode == "flip") { l2 = l1; r2 = r1; }
            else if (track && chance(60) && r1 > l1) {
                // Aim at a real occurrence of v1[l1, r1) inside v2.
                if (r1 - l1 > 12 && chance(70)) r1 = l1 + rnd(1, 12);
                const std::string needle = text[static_cast<std::size_t>(v1)].substr(static_cast<std::size_t>(l1), static_cast<std::size_t>(r1 - l1));
                const std::size_t from = static_cast<std::size_t>(rnd(0, len[static_cast<std::size_t>(v2)]));
                std::size_t at = text[static_cast<std::size_t>(v2)].find(needle, from);
                if (at == std::string::npos) at = text[static_cast<std::size_t>(v2)].find(needle);
                if (at != std::string::npos) { l2 = static_cast<long>(at); r2 = l2 + (r1 - l1); }
            } else if (chance(50)) {  // at least make the lengths agree
                const long m = std::min(r1 - l1, len[static_cast<std::size_t>(v2)]);
                r1 = l1 + m;
                l2 = rnd(0, len[static_cast<std::size_t>(v2)] - m);
                r2 = l2 + m;
            }
            std::printf("EQUAL %ld %ld %ld %ld %ld %ld\n", v1, l1, r1, v2, l2, r2);

        } else if (op == 5) {  // PAL
            const long v = any();
            long l, r;
            random_range(len[static_cast<std::size_t>(v)], l, r);
            if (track && chance(60) && len[static_cast<std::size_t>(v)] > 0) {
                // Grow a real palindrome around a random centre.
                const std::string& t = text[static_cast<std::size_t>(v)];
                const long c = rnd(0, len[static_cast<std::size_t>(v)] - 1);
                l = c; r = c + rnd(0, 1);
                while (l > 0 && r < static_cast<long>(t.size()) && t[static_cast<std::size_t>(l - 1)] == t[static_cast<std::size_t>(r)]) { --l; ++r; }
                if (chance(20) && r < static_cast<long>(t.size())) ++r;  // spoil it sometimes
            }
            std::printf("PAL %ld %ld %ld\n", v, l, r);

        } else {  // LCP
            const long v1 = any(), v2 = chance(30) ? v1 : any();
            long p1 = random_pos(len[static_cast<std::size_t>(v1)]), p2 = random_pos(len[static_cast<std::size_t>(v2)]);
            if (chance(30)) p2 = std::min(p1, len[static_cast<std::size_t>(v2)]);  // same place in a relative: long answers
            if (track && chance(40) && p1 < len[static_cast<std::size_t>(v1)]) {
                // Start B where A's next few characters occur.
                const std::string needle = text[static_cast<std::size_t>(v1)].substr(static_cast<std::size_t>(p1), static_cast<std::size_t>(rnd(1, 8)));
                const std::size_t at = text[static_cast<std::size_t>(v2)].find(needle, static_cast<std::size_t>(rnd(0, len[static_cast<std::size_t>(v2)])));
                if (at != std::string::npos) p2 = static_cast<long>(at);
            }
            std::printf("LCP %ld %ld %ld %ld\n", v1, p1, v2, p2);
        }
    }
    return 0;
}
