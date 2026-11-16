// Unit tests: small, named, deterministic.  Each test states one fact about
// the editor or the algebra underneath it.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../src/editor.hpp"

namespace {

int failures = 0;
int checks = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++checks;                                                                    \
        if (!(cond)) {                                                               \
            ++failures;                                                              \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                            \
    } while (0)
#define CHECK_THROWS(expr)                                                           \
    do {                                                                             \
        ++checks;                                                                    \
        bool threw = false;                                                          \
        try { (void)(expr); } catch (const std::exception&) { threw = true; }        \
        if (!threw) {                                                                \
            ++failures;                                                              \
            std::printf("  FAILED %s:%d: %s did not throw\n", __FILE__, __LINE__, #expr); \
        }                                                                            \
    } while (0)

using tte::Editor;
using tte::Hash;
using tte::Summary;
using tte::u32;
using tte::u64;

std::string reversed(std::string s) {
    std::reverse(s.begin(), s.end());
    return s;
}

// ---- arithmetic -----------------------------------------------------------

void test_field_arithmetic() {
    const u64 p = tte::kP;
    const u64 edge[] = {0, 1, 2, p - 2, p - 1, p / 2, p / 2 + 1, (u64{1} << 32) - 1, u64{1} << 32, u64{1} << 60};
    std::mt19937_64 rng(1);
    std::vector<u64> xs;
    for (const u64 e : edge) xs.push_back(e % p);
    for (int i = 0; i < 2000; ++i) xs.push_back(rng() % p);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        for (std::size_t j = 0; j < xs.size(); j += (i < 10 ? 1 : 97)) {
            const u64 a = xs[i], b = xs[j];
            CHECK(tte::mul_mod(a, b) == static_cast<u64>(static_cast<__uint128_t>(a) * b % p));
            CHECK(tte::add_mod(a, b) == (a + b) % p);
            CHECK(tte::sub_mod(a, b) == (a + p - b) % p);
        }
    }
}

// mu computed character by character, straight from the definition.
Summary mu(const std::string& s, const tte::Powers& pw) {
    Summary out;
    out.len = static_cast<u32>(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const Hash c = Hash::scalar(static_cast<u64>(s[i] - 'a' + 1));
        out.f = out.f + pw[i] * c;
        out.b = out.b + pw[s.size() - 1 - i] * c;
    }
    return out;
}

void test_summary_is_a_homomorphism() {
    Hash x;
    for (std::size_t i = 0; i < tte::kBases; ++i) x.v[i] = 1000003 + 7919 * i;
    tte::Powers pw(x);
    pw.ensure(64);
    std::mt19937 rng(2);
    auto word = [&](int max) {
        std::string s(rng() % static_cast<unsigned>(max + 1), 'a');
        for (char& c : s) c = static_cast<char>('a' + rng() % 3);
        return s;
    };
    for (int it = 0; it < 3000; ++it) {
        const std::string s = word(8), t = word(8), u = word(8);
        // concatenation
        CHECK(tte::concat(mu(s, pw), mu(t, pw), pw) == mu(s + t, pw));
        // associativity and identity
        CHECK(tte::concat(tte::concat(mu(s, pw), mu(t, pw), pw), mu(u, pw), pw) ==
              tte::concat(mu(s, pw), tte::concat(mu(t, pw), mu(u, pw), pw), pw));
        CHECK(tte::concat(Summary{}, mu(s, pw), pw) == mu(s, pw));
        CHECK(tte::concat(mu(s, pw), Summary{}, pw) == mu(s, pw));
        // reversal is an involution that reverses products
        CHECK(mu(s, pw).flipped() == mu(reversed(s), pw));
        CHECK(mu(s, pw).flipped().flipped() == mu(s, pw));
        CHECK(tte::concat(mu(s, pw), mu(t, pw), pw).flipped() ==
              tte::concat(mu(t, pw).flipped(), mu(s, pw).flipped(), pw));
        // a node: left subtree, one character, right subtree
        const char c = static_cast<char>('a' + rng() % 3);
        CHECK(tte::around(mu(s, pw), static_cast<u32>(c - 'a' + 1), mu(t, pw), pw) == mu(s + c + t, pw));
        // palindromes are exactly the fixed points of reversal
        CHECK(mu(s, pw).symmetric() == (s == reversed(s)));
    }
}

// ---- the statement's example ---------------------------------------------

void test_sample() {
    Editor ed("abacaba", 1);
    CHECK(ed.palindrome(0, 0, 7));
    CHECK(ed.reverse(0, 0, 4) == 1);
    CHECK(ed.text(1) == "cabaaba");
    CHECK(ed.equal(0, 0, 3, 1, 1, 4));
    CHECK(ed.lcp(0, 0, 1, 1) == 3);
    CHECK(ed.insert(1, 4, "xy") == 2);
    CHECK(ed.text(2) == "cabaxyaba");
    CHECK(ed.move(2, 4, 6, 0) == 3);
    CHECK(ed.text(3) == "xycabaaba");
    CHECK(ed.erase(3, 0, 2) == 4);
    CHECK(ed.text(4) == "cabaaba");
    CHECK(ed.equal(1, 0, 7, 4, 0, 7));
    CHECK(!ed.palindrome(4, 1, 6));
    CHECK(ed.text(0) == "abacaba");
    CHECK(ed.versions() == 5);
}

// ---- empty ranges and boundaries -----------------------------------------

void test_empty_and_boundary() {
    Editor ed("abcde", 3);
    // empty ranges are legal everywhere and still create versions
    CHECK(ed.erase(0, 2, 2) == 1 && ed.text(1) == "abcde");
    CHECK(ed.reverse(0, 5, 5) == 2 && ed.text(2) == "abcde");
    CHECK(ed.reverse(0, 3, 4) == 3 && ed.text(3) == "abcde");  // one character
    CHECK(ed.text(ed.reverse(0, 3, 5)) == "abced");             // two characters: the shortest real reversal
    CHECK(ed.text(ed.reverse(0, 0, 2)) == "bacde");
    CHECK(ed.versions() == 6);
    CHECK(ed.move(0, 1, 1, 4) == 6 && ed.text(6) == "abcde");
    CHECK(ed.equal(0, 0, 0, 0, 5, 5));    // empty == empty
    CHECK(ed.equal(0, 3, 3, 1, 0, 0));
    CHECK(!ed.equal(0, 0, 0, 0, 0, 1));   // different lengths
    CHECK(ed.palindrome(0, 2, 2));        // empty
    CHECK(ed.palindrome(0, 2, 3));        // single character
    CHECK(ed.lcp(0, 5, 0, 0) == 0);       // empty suffix
    CHECK(ed.lcp(0, 5, 0, 5) == 0);
    CHECK(ed.lcp(0, 0, 0, 0) == 5);
    CHECK(ed.lcp(0, 4, 0, 4) == 1);
    // both ends
    CHECK(ed.text(ed.insert(0, 0, "x")) == "xabcde");
    CHECK(ed.text(ed.insert(0, 5, "x")) == "abcdex");
    CHECK(ed.text(ed.erase(0, 0, 5)).empty());
    CHECK(ed.text(ed.erase(0, 0, 1)) == "bcde");
    CHECK(ed.text(ed.erase(0, 4, 5)) == "abcd");
    CHECK(ed.text(ed.reverse(0, 0, 5)) == "edcba");
    const int empty = ed.erase(0, 0, 5);
    CHECK(ed.length(empty) == 0);
    CHECK(ed.palindrome(empty, 0, 0) && ed.equal(empty, 0, 0, 0, 1, 1) && ed.lcp(empty, 0, 0, 0) == 0);
    CHECK(ed.text(ed.reverse(empty, 0, 0)).empty());
    CHECK(ed.text(ed.move(empty, 0, 0, 0)).empty());
    CHECK(ed.text(ed.insert(empty, 0, "zz")) == "zz");
    // an editor may start empty, although the statement never does
    Editor blank("", 4);
    CHECK(blank.length(0) == 0 && blank.text(blank.insert(0, 0, "a")) == "a");
}

void test_single_character_text() {
    Editor ed("q", 5);
    CHECK(ed.palindrome(0, 0, 1) && ed.lcp(0, 0, 0, 0) == 1 && ed.lcp(0, 0, 0, 1) == 0);
    CHECK(ed.text(ed.reverse(0, 0, 1)) == "q");
    CHECK(ed.text(ed.move(0, 0, 1, 0)) == "q");
    CHECK(ed.text(ed.insert(0, 1, "abcdefghijklmnopqrst")) == "qabcdefghijklmnopqrst");
}

// ---- MOVE: the target index refers to the text after the cut -------------

void test_move_indexing() {
    Editor ed("abcdefghij", 6);
    // cut "cde" (positions 2..5); the remainder is "abfghij" (length 7)
    CHECK(ed.text(ed.move(0, 2, 5, 0)) == "cdeabfghij");
    CHECK(ed.text(ed.move(0, 2, 5, 1)) == "acdebfghij");
    CHECK(ed.text(ed.move(0, 2, 5, 2)) == "abcdefghij");  // back where it was
    CHECK(ed.text(ed.move(0, 2, 5, 3)) == "abfcdeghij");  // NOT "abc|cde|..." : 3 indexes the remainder
    CHECK(ed.text(ed.move(0, 2, 5, 6)) == "abfghicdej");
    CHECK(ed.text(ed.move(0, 2, 5, 7)) == "abfghijcde");
    CHECK_THROWS(ed.move(0, 2, 5, 8));                     // remainder has only 7 characters
    // whole text: remainder is empty, only p = 0 is legal
    CHECK(ed.text(ed.move(0, 0, 10, 0)) == "abcdefghij");
    CHECK_THROWS(ed.move(0, 0, 10, 1));
    // prefix to the end, suffix to the front
    CHECK(ed.text(ed.move(0, 0, 3, 7)) == "defghijabc");
    CHECK(ed.text(ed.move(0, 7, 10, 0)) == "hijabcdefg");
    // exhaustive against the definition
    const std::string s = "abcdefg";
    Editor e2(s, 7);
    for (u32 l = 0; l <= 7; ++l)
        for (u32 r = l; r <= 7; ++r)
            for (u32 p = 0; p <= 7 - (r - l); ++p) {
                std::string rest = s.substr(0, l) + s.substr(r);
                const std::string want = rest.substr(0, p) + s.substr(l, r - l) + rest.substr(p);
                CHECK(e2.text(e2.move(0, l, r, p)) == want);
            }
    CHECK(e2.text(0) == s);
}

// ---- persistence ----------------------------------------------------------

void test_branching_history() {
    Editor ed("abcdef", 8);
    const int a = ed.insert(0, 3, "xyz");   // abcxyzdef
    const int b = ed.erase(0, 1, 5);        // af            (sibling of a)
    const int c = ed.reverse(a, 2, 7);      // abdzyxcef     (child of a)
    const int d = ed.move(a, 0, 3, 6);      // xyzdefabc     (another child of a)
    const int e = ed.insert(b, 1, "q");     // aqf
    const int f = ed.reverse(0, 0, 6);      // fedcba        (third child of the root)
    CHECK(ed.text(0) == "abcdef");
    CHECK(ed.text(a) == "abcxyzdef");
    CHECK(ed.text(b) == "af");
    CHECK(ed.text(c) == "abdzyxcef");
    CHECK(ed.text(d) == "xyzdefabc");
    CHECK(ed.text(e) == "aqf");
    CHECK(ed.text(f) == "fedcba");
    // queries across branches
    CHECK(ed.equal(a, 3, 6, d, 0, 3));          // "xyz"
    CHECK(ed.equal(a, 0, 3, a, 3, 6) == false); // same version, different places: "abc" vs "xyz"
    CHECK(ed.equal(a, 0, 3, a, 0, 3));
    CHECK(ed.lcp(a, 0, a, 3) == 0 && ed.lcp(a, 4, a, 4) == 5);
    CHECK(ed.equal(0, 0, 6, f, 0, 6) == false);
    CHECK(ed.lcp(a, 0, c, 0) == 2);             // "ab"
    CHECK(ed.lcp(d, 3, 0, 3) == 3);             // "def"
    CHECK(ed.lcp(f, 5, b, 0) == 1);             // "a"
    for (int v = 0; v < ed.versions(); ++v) CHECK(ed.check_invariants(v).empty());
}

// The classic failure: a reversal recorded lazily in a node that an older
// version also reaches.  Every reader of the old versions must see no change.
void test_reversal_does_not_leak_into_other_versions() {
    const std::string s = "abcdefghijklmnopqrstuvwxyz";
    Editor ed(s, 9);
    std::vector<std::string> want{s};
    // many reversals of overlapping ranges, all based on version 0 or on each other
    const u32 ranges[][2] = {{0, 26}, {3, 20}, {5, 9}, {0, 13}, {13, 26}, {1, 25}, {7, 8}, {2, 24}};
    for (const auto& lr : ranges) {
        for (const int base : {0, ed.versions() - 1, ed.versions() / 2}) {
            std::string t = want[static_cast<std::size_t>(base)];
            std::reverse(t.begin() + lr[0], t.begin() + lr[1]);
            ed.reverse(base, lr[0], lr[1]);
            want.push_back(t);
            // after every single edit, every version is re-read
            for (int v = 0; v < ed.versions(); ++v) CHECK(ed.text(v) == want[static_cast<std::size_t>(v)]);
        }
    }
    // read the old versions through queries too, not only through text()
    for (int v = 0; v < ed.versions(); ++v) {
        const std::string& t = want[static_cast<std::size_t>(v)];
        for (u32 i = 0; i < 26; ++i) {
            CHECK(ed.equal(v, i, i + 1, 0, static_cast<u32>(t[i] - 'a'), static_cast<u32>(t[i] - 'a') + 1));
        }
        CHECK(ed.check_invariants(v).empty());
    }
}

void test_flipping_the_same_range_repeatedly() {
    const std::string s = "thequickbrownfoxjumpsoverthelazydog";
    Editor ed(s, 10);
    const u32 l = 4, r = 29;
    std::string odd = s;
    std::reverse(odd.begin() + l, odd.begin() + r);
    int v = 0;
    for (int i = 1; i <= 200; ++i) {
        v = ed.reverse(v, l, r);
        CHECK(ed.text(v) == (i % 2 ? odd : s));
        CHECK(ed.equal(v, 0, 35, i % 2, 0, 35));              // equal to version 1 or 0
        CHECK(ed.equal(v, 0, 35, i % 2 ? 0 : 1, 0, 35) == false);
        CHECK(ed.lcp(v, 0, 0, 0) == (i % 2 ? 4u : 35u));
        CHECK(ed.palindrome(v, l, r) == false);
    }
    CHECK(ed.text(0) == s && ed.text(1) == odd);
    // nested: reversing inside a reversed region, then undoing both
    const int a = ed.reverse(0, 0, 35), b = ed.reverse(a, 10, 20), c = ed.reverse(b, 0, 35), d = ed.reverse(c, 15, 25);
    CHECK(ed.text(d) == s);
    CHECK(ed.equal(d, 0, 35, 0, 0, 35) && ed.lcp(d, 0, 0, 0) == 35);
}

// ---- queries --------------------------------------------------------------

void test_queries_against_brute_force() {
    // every substring pair / palindrome / suffix pair of two related versions
    const std::string s = "abaababaabaab";
    Editor ed(s, 11);
    const int v1 = ed.reverse(0, 2, 9);
    const std::string t = ed.text(v1);
    const u32 n = static_cast<u32>(s.size());
    for (u32 l1 = 0; l1 <= n; ++l1)
        for (u32 r1 = l1; r1 <= n; ++r1) {
            const std::string a = s.substr(l1, r1 - l1);
            CHECK(ed.palindrome(0, l1, r1) == (a == reversed(a)));
            for (u32 l2 = 0; l2 + (r1 - l1) <= n; ++l2) {
                CHECK(ed.equal(0, l1, r1, v1, l2, l2 + (r1 - l1)) == (a == t.substr(l2, r1 - l1)));
            }
        }
    for (u32 p1 = 0; p1 <= n; ++p1)
        for (u32 p2 = 0; p2 <= n; ++p2) {
            u32 k = 0;
            while (p1 + k < n && p2 + k < n && s[p1 + k] == t[p2 + k]) ++k;
            CHECK(ed.lcp(0, p1, v1, p2) == k);
            CHECK(ed.lcp(v1, p2, 0, p1) == k);
            CHECK(ed.lcp_reference(0, p1, v1, p2) == k);
        }
}

void test_long_common_prefixes() {
    // one letter repeated: every LCP is as long as the shorter suffix
    const std::string s(5000, 'a');
    Editor ed(s, 12);
    const int v1 = ed.insert(0, 2500, "b");
    const int v2 = ed.reverse(v1, 100, 4000);   // b moves to 100 + (3999 - 2500) = 1599
    CHECK(ed.lcp(0, 0, 0, 1) == 4999);
    CHECK(ed.lcp(0, 17, 0, 4000) == 1000);
    CHECK(ed.lcp(0, 0, v1, 0) == 2500);
    CHECK(ed.lcp(0, 0, v1, 2501) == 2500);
    CHECK(ed.lcp(v1, 0, v1, 1) == 2499);
    CHECK(ed.lcp(v1, 2500, v2, 1599) == 2501);
    CHECK(ed.lcp(v2, 0, 0, 0) == 1599);
    CHECK(ed.palindrome(0, 0, 5000));
    CHECK(ed.palindrome(v1, 0, 5001));          // b exactly in the middle
    CHECK(!ed.palindrome(v1, 0, 5000) && !ed.palindrome(v1, 1, 5001));
    CHECK(ed.palindrome(v1, 1, 5000) && !ed.palindrome(v2, 0, 5001));
}

// ---- contract -------------------------------------------------------------

void test_invalid_arguments_are_rejected() {
    Editor ed("abc", 13);
    CHECK_THROWS(ed.insert(1, 0, "a"));      // no such version
    CHECK_THROWS(ed.insert(-1, 0, "a"));
    CHECK_THROWS(ed.insert(0, 4, "a"));
    CHECK_THROWS(ed.insert(0, 0, "A"));      // not a-z
    CHECK_THROWS(ed.erase(0, 2, 1));
    CHECK_THROWS(ed.erase(0, 0, 4));
    CHECK_THROWS(ed.reverse(0, 3, 4));
    CHECK_THROWS(ed.move(0, 0, 1, 3));
    CHECK_THROWS(ed.equal(0, 0, 4, 0, 0, 4));
    CHECK_THROWS(ed.palindrome(0, 2, 5));
    CHECK_THROWS(ed.lcp(0, 4, 0, 0));
    CHECK_THROWS(Editor("a b", 1));
    // a rejected call changes nothing
    CHECK(ed.versions() == 1 && ed.text(0) == "abc" && ed.text(ed.insert(0, 1, "z")) == "azbc");
}

// ---- sharing and balance ---------------------------------------------------

void test_versions_share_nodes() {
    std::mt19937 rng(14);
    std::string s(100000, 'a');
    for (char& c : s) c = static_cast<char>('a' + rng() % 26);
    Editor ed(s, 15);
    CHECK(ed.node_count() == 100001);  // one node per character, plus the empty tree
    CHECK(ed.depth(0) < 80);           // about 2.5 log2 n expected; n would mean "no balance"
    std::size_t worst = 0;
    for (int i = 0; i < 300; ++i) {
        const std::size_t before = ed.node_count();
        const u32 n = ed.length(0);
        u32 l = static_cast<u32>(rng() % n), r = static_cast<u32>(rng() % n);
        if (l > r) std::swap(l, r);
        switch (i % 4) {
            case 0: ed.insert(0, l, "hello"); break;
            case 1: ed.erase(0, l, r); break;
            case 2: ed.reverse(0, l, r); break;
            default: ed.move(0, l, r, static_cast<u32>(rng() % (n - (r - l) + 1))); break;
        }
        worst = std::max(worst, ed.node_count() - before);
    }
    CHECK(worst < 400);  // O(log n) new nodes per edit, not O(n)
    // operations that change nothing allocate nothing
    const std::size_t before = ed.node_count();
    ed.erase(0, 7, 7);
    ed.reverse(0, 9, 10);
    ed.move(0, 5, 5, 3);
    ed.move(0, 5, 50, 5);
    CHECK(ed.node_count() == before);
    CHECK(ed.text(0) == s);
}

void test_answers_do_not_depend_on_the_seed() {
    std::vector<std::string> transcripts;
    for (const u64 seed : {1ull, 2ull, 0xDEADBEEFull}) {
        Editor ed("mississippi", seed);
        std::string out;
        const int a = ed.reverse(0, 1, 8), b = ed.move(a, 2, 6, 5), c = ed.insert(b, 4, "ssi"), d = ed.erase(c, 0, 3);
        for (int v = 0; v <= d; ++v) out += ed.text(v) + "|";
        for (int v = 0; v <= d; ++v)
            for (u32 p = 0; p <= 11; ++p) {  // every version has at least 11 characters
                out += std::to_string(ed.lcp(v, p, 0, 1)) + ",";
                out += ed.palindrome(v, p / 2, p) ? 'P' : '-';
                out += ed.equal(v, 0, p / 2, d, p / 2, p / 2 * 2) ? 'E' : '-';
            }
        transcripts.push_back(out);
    }
    CHECK(transcripts[0] == transcripts[1] && transcripts[1] == transcripts[2]);
}

struct Test {
    const char* name;
    void (*run)();
};

}  // namespace

int main() {
    const Test tests[] = {
        {"field arithmetic mod 2^61-1", test_field_arithmetic},
        {"summary is a homomorphism (concat, reverse)", test_summary_is_a_homomorphism},
        {"statement sample", test_sample},
        {"empty ranges and boundaries", test_empty_and_boundary},
        {"single-character text", test_single_character_text},
        {"MOVE indexes the remainder", test_move_indexing},
        {"branching history", test_branching_history},
        {"reversal does not leak into other versions", test_reversal_does_not_leak_into_other_versions},
        {"flipping the same range repeatedly", test_flipping_the_same_range_repeatedly},
        {"queries against brute force", test_queries_against_brute_force},
        {"long common prefixes", test_long_common_prefixes},
        {"invalid arguments are rejected", test_invalid_arguments_are_rejected},
        {"versions share nodes", test_versions_share_nodes},
        {"answers do not depend on the seed", test_answers_do_not_depend_on_the_seed},
    };
    for (const Test& t : tests) {
        const int before = failures;
        t.run();
        std::printf("%s  %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("unit tests: %d checks, %d failed (hash bases: %zu, node: %zu bytes)\n", checks, failures,
                tte::kBases, sizeof(tte::Node));
    return failures ? 1 : 0;
}
