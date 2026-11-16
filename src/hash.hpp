// hash.hpp -- the algebra behind the editor.
//
// A string s = s_0 s_1 ... s_{n-1} over {1..26} is summarised by the triple
//
//     mu(s) = ( n,  f(s) = sum s_i x^i,  b(s) = sum s_i x^(n-1-i) )   (mod p)
//
// i.e. its length, the hash of s and the hash of reverse(s).  Two facts make
// the whole editor work:
//
//   concatenation   mu(s t)    = ( |s|+|t|, f(s) + x^|s| f(t), b(t) + x^|t| b(s) )
//   reversal        mu(rev s)  = ( |s|, b(s), f(s) )
//
// so mu is a homomorphism from (strings, concatenation, reversal) into a tiny
// monoid with involution.  Everything here is exact arithmetic in the field
// F_p, p = 2^61 - 1; the only place probability enters is that the evaluation
// point x is chosen at random (see docs/DESIGN.md, "hash collisions").
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Number of independent evaluation points.  1 (default): 16 bytes of hash per
// node.  2: 32 bytes per node, collision bound squared.
#ifndef TTE_HASH_BASES
#define TTE_HASH_BASES 1
#endif

// The hashes live in F_p with p = 2^e - 1.  e = 61 is the only value meant
// for use.  Tests set e = 13 (p = 8191) to make collisions frequent and to
// watch what a collision does; e must make 2^e - 1 prime (13, 17, 19, 31, 61).
#ifndef TTE_MERSENNE_EXPONENT
#define TTE_MERSENNE_EXPONENT 61
#endif

namespace tte {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

inline constexpr int kExponent = TTE_MERSENNE_EXPONENT;
inline constexpr u64 kP = (u64{1} << kExponent) - 1;  // the Mersenne prime 2^e - 1
inline constexpr std::size_t kBases = TTE_HASH_BASES;

// --- F_p, operands and results always in [0, p) ---------------------------

constexpr u64 add_mod(u64 a, u64 b) {
    u64 s = a + b;  // < 2^62, no overflow
    return s >= kP ? s - kP : s;
}
constexpr u64 sub_mod(u64 a, u64 b) { return a >= b ? a - b : a + kP - b; }
constexpr u64 mul_mod(u64 a, u64 b) {
    // 2^e == 1 (mod p), so fold the 2e-bit product at bit e.
    __uint128_t t = static_cast<__uint128_t>(a) * b;
    u64 s = (static_cast<u64>(t) & kP) + static_cast<u64>(t >> kExponent);  // <= 2p - 3
    return s >= kP ? s - kP : s;
}

// --- F_p^k with componentwise operations (k = kBases) ----------------------

struct Hash {
    std::array<u64, kBases> v{};

    friend constexpr bool operator==(const Hash&, const Hash&) = default;
    friend constexpr Hash operator+(Hash a, const Hash& b) {
        for (std::size_t i = 0; i < kBases; ++i) a.v[i] = add_mod(a.v[i], b.v[i]);
        return a;
    }
    friend constexpr Hash operator-(Hash a, const Hash& b) {
        for (std::size_t i = 0; i < kBases; ++i) a.v[i] = sub_mod(a.v[i], b.v[i]);
        return a;
    }
    friend constexpr Hash operator*(Hash a, const Hash& b) {
        for (std::size_t i = 0; i < kBases; ++i) a.v[i] = mul_mod(a.v[i], b.v[i]);
        return a;
    }
    // Embeds a small integer (a character code) diagonally.
    static constexpr Hash scalar(u64 c) {
        Hash h;
        for (std::size_t i = 0; i < kBases; ++i) h.v[i] = c;
        return h;
    }
};

// Powers of the evaluation point(s): pw[i] = x^i, grown on demand.
class Powers {
  public:
    explicit Powers(const Hash& x) : x_(x), pw_{Hash::scalar(1)} {}

    const Hash& x() const { return x_; }
    const Hash& operator[](std::size_t i) const { return pw_[i]; }
    void ensure(std::size_t max_exponent) {
        // No reserve(): it would reallocate to the exact size on every call.
        while (pw_.size() <= max_exponent) pw_.push_back(pw_.back() * x_);
    }

  private:
    Hash x_;
    std::vector<Hash> pw_;
};

// mu(s): the summary of a string.
struct Summary {
    u32 len = 0;
    Hash f;  // hash of s
    Hash b;  // hash of reverse(s)

    friend bool operator==(const Summary&, const Summary&) = default;
    // mu(reverse(s))
    Summary flipped() const { return {len, b, f}; }
    // With overwhelming probability: is s a palindrome?
    bool symmetric() const { return f == b; }
};

inline Summary single(u32 code) { return {1, Hash::scalar(code), Hash::scalar(code)}; }

// mu(s t) from mu(s) and mu(t).
inline Summary concat(const Summary& s, const Summary& t, const Powers& pw) {
    return {s.len + t.len, s.f + pw[s.len] * t.f, t.b + pw[t.len] * s.b};
}

// mu(l c r) for a single character c: what a tree node stores.
inline Summary around(const Summary& l, u32 code, const Summary& r, const Powers& pw) {
    const Hash c = Hash::scalar(code);
    return {l.len + 1 + r.len, l.f + pw[l.len] * (c + pw.x() * r.f),
            r.b + pw[r.len] * (c + pw.x() * l.b)};
}

}  // namespace tte
