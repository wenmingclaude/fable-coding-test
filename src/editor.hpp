// editor.hpp -- a text editor whose every version stays alive.
//
// Representation
//   * Every version is the root of a binary tree; the in-order walk of the
//     tree spells the text.  Trees of different versions share subtrees.
//   * A node is never modified once its operation has finished.
//   * A subtree is named by a Ref = (node index, orientation bit).  The same
//     node read through a flipped Ref is its mirror image: children swapped
//     (and flipped), forward/backward hashes swapped.  Reversing a range is
//     therefore "flip one bit in one Ref"; no node is touched, no flag is
//     ever pushed down, and an old version cannot observe the reversal.
//   * Balance comes from random priorities (a treap): the shape of a tree is
//     a function of its content alone, not of the edit history.
//   * Every node carries the hash of its subtree's text and of the reverse
//     of that text (see hash.hpp); the three queries are answered from them.
//
// See docs/DESIGN.md for invariants, proofs and complexity.
#pragma once

#include <algorithm>
#include <chrono>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hash.hpp"
#include "pool.hpp"

namespace tte {

// (node index << 1) | orientation.  Index 0 is the empty tree.
using Ref = u32;
constexpr Ref flip(Ref t) { return t ^ 1; }

struct Node {
    Ref left, right;  // children, as seen through an unflipped Ref
    u32 lsize;        // number of characters in the left subtree
    u32 key;          // (priority << 5) | character code; max-heap on the whole word
    Hash f, b;        // hash of the subtree's text, and of its reverse
};
static_assert(sizeof(Node) == 16 + 16 * kBases);

// A subtree together with its length.  Lengths are not stored per node (only
// the left child's is); they are known at the root and derived on the way
// down, so that walking a path never has to load a node beside the path.
struct Tree {
    Ref ref = 0;
    u32 size = 0;
};
constexpr Tree flip(Tree t) { return {flip(t.ref), t.size}; }

class Editor {
  public:
    // `seed` determines the tree priorities and the hash evaluation point.
    // Results never depend on it, except with the (tiny) probability of a
    // hash collision.  Use entropy_seed() unless reproducibility is wanted.
    explicit Editor(std::string_view initial, u64 seed = entropy_seed())
        : rng_(seed), pw_(random_base()) {
        check_text(initial);
        pool_.push(Node{0, 0, 0, 0, {}, {}});  // index 0: the empty tree
        mark_ = pool_.size();
        publish(build(initial));
    }

    static u64 entropy_seed() {
        std::random_device rd;
        u64 s = (static_cast<u64>(rd()) << 32) ^ rd();
        return s ^ static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    // ---- edits: each creates a version and returns its number -------------

    int insert(int v, u32 p, std::string_view s) {
        const Tree t = begin(v);
        require(p <= t.size, "insert: position out of range");
        check_text(s);
        if (s.empty()) return publish(t);
        auto [a, b] = split(t, p);
        return publish(merge(merge(a, build(s)), b));
    }

    int erase(int v, u32 l, u32 r) {
        const Tree t = begin(v);
        require(l <= r && r <= t.size, "erase: range out of range");
        if (l == r) return publish(t);
        // Keep a prefix and a suffix; a suffix is the mirror image of a
        // prefix of the mirror image.
        return publish(merge(take(t, l), flip(take(flip(t), t.size - r))));
    }

    int reverse(int v, u32 l, u32 r) {
        const Tree t = begin(v);
        require(l <= r && r <= t.size, "reverse: range out of range");
        if (r - l < 2) return publish(t);
        auto [a, rest] = split(t, l);
        auto [mid, b] = split(rest, r - l);
        return publish(merge(merge(a, flip(mid)), b));
    }

    // Cuts [l, r) and re-inserts it before index p of what remains.
    int move(int v, u32 l, u32 r, u32 p) {
        const Tree t = begin(v);
        require(l <= r && r <= t.size, "move: range out of range");
        require(p <= t.size - (r - l), "move: target out of range");
        if (l == r || p == l) return publish(t);
        auto [a, rest] = split(t, l);
        auto [mid, b] = split(rest, r - l);
        auto [x, y] = split(merge(a, b), p);
        return publish(merge(merge(x, mid), y));
    }

    // ---- queries: read-only, never allocate nodes -------------------------

    // Is v1[l1, r1) equal to v2[l2, r2)?  "false" is always right; "true" is
    // right unless the hashes collide.
    bool equal(int v1, u32 l1, u32 r1, int v2, u32 l2, u32 r2) const {
        const Tree a = version(v1), b = version(v2);
        require(l1 <= r1 && r1 <= a.size && l2 <= r2 && r2 <= b.size, "equal: range out of range");
        if (r1 - l1 != r2 - l2) return false;
        return same_text(a, l1, r1, b, l2);
    }

    // Is v[l, r) a palindrome?  Same one-sided error as equal().
    // A palindrome is a text equal to its mirror image, and the mirror image
    // of a tree is the same tree read through a flipped reference.
    bool palindrome(int v, u32 l, u32 r) const {
        const Tree t = version(v);
        require(l <= r && r <= t.size, "palindrome: range out of range");
        return same_text(t, l, r, flip(t), t.size - r);
    }

    // Length of the longest common prefix of v1[p1, ...) and v2[p2, ...).
    // Never too small; too large only if hashes collide.
    u32 lcp(int v1, u32 p1, int v2, u32 p2) const {
        const Tree ta = version(v1), tb = version(v2);
        require(p1 <= ta.size && p2 <= tb.size, "lcp: position out of range");
        const u32 limit = std::min(ta.size - p1, tb.size - p2);
        if (limit == 0 || (ta.ref == tb.ref && p1 == p2)) return limit;

        // Two fingers.  Write A, B for the two texts.  The finger in A is
        // parked at p1; its stack lists the subtrees around p1 from the
        // smallest up to the whole tree.  The finger in B is moved to
        // wherever A's structure asks a question.
        std::vector<Frame>&A = finger_a_, &B = finger_b_;
        A.assign(1, Frame{ta, 0, Hash{}});
        B.assign(1, Frame{tb, 0, Hash{}});
        const Hash a0 = seek(A, p1), b0 = seek(B, p2);
        const Hash wa = pw_[p2], wb = pw_[p1];
        // Given ha = f(A[0, q)): does A[p1, q) equal B[p2, p2 + q - p1)?
        // Both sides are scaled to the common exponent offset p1 + p2.
        auto matches = [&](u32 q, const Hash& ha) {
            return (ha - a0) * wa == (seek(B, p2 + (q - p1)) - b0) * wb;
        };

        u32 known = p1;              // A[p1, known) is known to match
        const u32 end = p1 + limit;  // and a match cannot extend past `end`

        // Climb: test the right ends of ever larger subtrees containing p1,
        // until one fails.  Each test costs O(1) on the A side.
        std::size_t k = A.size();
        for (;;) {
            if (known == end || k == 0) return known - p1;
            const Frame& fr = A[--k];
            const u32 e = fr.lo + fr.t.size;
            if (e <= known) continue;
            if (e > end) {
                // This subtree reaches past the limit.  Before searching it,
                // test the limit itself: texts that agree all the way to the
                // end are common, and are the costly case of the descent.
                if (matches(end, walk(fr, end))) return limit;
                break;
            }
            if (!matches(e, fr.acc + pw_[fr.lo] * forward(fr.t.ref))) break;
            known = e;
        }

        // Descend: the first mismatch is inside A[k].t, at or after `known`.
        // If that subtree is not the innermost one, its root character sits
        // exactly at `known` (everything to its left already matched).
        Tree t = A[k].t;
        u32 lo = A[k].lo;
        Hash acc = A[k].acc;
        while (t.size != 0) {
            const View n = expose(t);
            const u32 m = lo + n.left.size;  // position of the root character
            Hash through;                    // f(A[0, m + 1))
            if (m < end) through = acc + pw_[lo] * head(t.ref, n);
            if (m < end && matches(m + 1, through)) {
                known = lo = m + 1;
                acc = through;
                t = n.right;
            } else if (m == known) {
                break;  // the root character itself is the first mismatch
            } else {
                t = n.left;
            }
        }
        return known - p1;
    }

    // ---- inspection -------------------------------------------------------

    int versions() const { return static_cast<int>(versions_.size()); }
    u32 length(int v) const { return version(v).size; }

    std::string text(int v) const {
        std::string out;
        out.reserve(length(v));
        append_text(version(v), out);
        return out;
    }

    // ---- diagnostics (used by tests and benchmarks) -----------------------

    std::size_t node_count() const { return pool_.size(); }
    const Node* nodes() const { return pool_.data(); }
    std::size_t memory_bytes() const { return pool_.size() * sizeof(Node); }
    u32 depth(int v) const { return depth_of(version(v)); }

    // Recomputes everything the nodes of version v claim and compares.
    // O(length); returns an empty string if the version is sound.
    std::string check_invariants(int v) const {
        std::string problem, text_out;
        verify(version(v), ~u32{0}, text_out, problem);
        return problem;
    }

    // Reference implementation of lcp(): plain binary search, O(log^2 n).
    // Only used to cross-check lcp().
    u32 lcp_reference(int v1, u32 p1, int v2, u32 p2) const {
        const Tree a = version(v1), b = version(v2);
        u32 lo = 0, hi = std::min(a.size - p1, b.size - p2);
        while (lo < hi) {
            const u32 mid = lo + (hi - lo + 1) / 2;
            if (same_text(a, p1, p1 + mid, b, p2)) lo = mid;
            else hi = mid - 1;
        }
        return lo;
    }

  private:
    // ---- reading nodes ----------------------------------------------------

    struct View {
        Tree left, right;
        u32 key;
    };
    // The one place where orientation is interpreted: a flipped node shows
    // its children in the opposite order, each flipped in turn.
    View expose(Tree t) const {
        const Node& n = pool_[t.ref >> 1];
        const u32 rsize = t.size - 1 - n.lsize;
        return (t.ref & 1) ? View{{flip(n.right), rsize}, {flip(n.left), n.lsize}, n.key}
                           : View{{n.left, n.lsize}, {n.right, rsize}, n.key};
    }
    u32 key(Ref t) const { return pool_[t >> 1].key; }
    // Hash of the text of t, read in t's orientation.  (0 for the empty tree.)
    const Hash& forward(Ref t) const { return (t & 1) ? pool_[t >> 1].b : pool_[t >> 1].f; }
    Summary summary(Tree t) const {
        const Node& n = pool_[t.ref >> 1];
        return (t.ref & 1) ? Summary{t.size, n.b, n.f} : Summary{t.size, n.f, n.b};
    }
    // Hash of (left subtree + root character) of t, where n = expose(t).
    // Computed as f(t) - x^(|left|+1) f(right): if the caller continues into
    // the right subtree, the left subtree is never loaded.
    Hash head(Ref t, const View& n) const { return forward(t) - pw_[n.left.size + 1] * forward(n.right.ref); }

    Tree version(int v) const {
        require(v >= 0 && static_cast<std::size_t>(v) < versions_.size(), "no such version");
        return versions_[static_cast<std::size_t>(v)];
    }
    static void require(bool ok, const char* what) {
        if (!ok) throw std::out_of_range(what);
    }
    static void check_text(std::string_view s) {
        for (const char ch : s)
            if (ch < 'a' || ch > 'z') throw std::invalid_argument("text must consist of the letters a-z");
    }

    // ---- building trees (inside one operation) ----------------------------
    //
    // Nodes with index >= mark_ were created by the operation in progress
    // ("the nursery").  Nodes below mark_ are frozen: nothing in this file
    // writes to them.  The nursery is private to the operation and every
    // nursery node is referenced exactly once, so "the same node with other
    // children" may reuse the node itself instead of allocating a copy
    // (with()), which keeps the number of allocations per edit equal to the
    // number of nodes the new version really needs.  Nursery nodes carry
    // structure only; commit() computes their hashes and freezes them.

    bool in_nursery(Ref t) const { return (t >> 1) >= mark_; }

    Tree begin(int v) {
        const Tree t = version(v);
        pool_.truncate(mark_);  // drop leftovers of an operation that threw
        return t;
    }

    Tree make(Tree left, u32 key, Tree right) {
        const std::size_t id = pool_.push(Node{left.ref, right.ref, left.size, key, {}, {}});
        if (id >> 31) throw std::length_error("tte: node index space exhausted");
        return {static_cast<Ref>(id << 1), left.size + 1 + right.size};
    }

    // The root of t with new children (as seen through t's orientation).
    // A frozen root is copied; a nursery root is rewritten in place.
    Tree with(Tree t, Tree left, Tree right) {
        if (!in_nursery(t.ref)) return make(left, key(t.ref), right);
        Node& n = pool_[t.ref >> 1];
        if (t.ref & 1) {
            n.left = flip(right.ref);
            n.right = flip(left.ref);
            n.lsize = right.size;
        } else {
            n.left = left.ref;
            n.right = right.ref;
            n.lsize = left.size;
        }
        return {t.ref, left.size + 1 + right.size};
    }

    // The first k characters of t, and the rest.
    std::pair<Tree, Tree> split(Tree t, u32 k) {
        if (k == 0) return {Tree{}, t};
        if (k == t.size) return {t, Tree{}};
        const View v = expose(t);
        if (k <= v.left.size) {
            auto [a, b] = split(v.left, k);
            return {a, with(t, b, v.right)};
        }
        auto [a, b] = split(v.right, k - v.left.size - 1);
        return {with(t, v.left, a), b};
    }

    // The first k characters of a frozen tree; allocates nothing for the
    // part that is dropped.
    Tree take(Tree t, u32 k) {
        if (k == 0) return {};
        if (k == t.size) return t;
        const View v = expose(t);
        if (k <= v.left.size) return take(v.left, k);
        return make(v.left, v.key, take(v.right, k - v.left.size - 1));
    }

    // The text of a followed by the text of b.
    Tree merge(Tree a, Tree b) {
        if (a.size == 0) return b;
        if (b.size == 0) return a;
        if (key(a.ref) >= key(b.ref)) {
            const View v = expose(a);
            return with(a, v.left, merge(v.right, b));
        }
        const View v = expose(b);
        return with(b, merge(a, v.left), v.right);
    }

    // A fresh treap spelling s, in O(|s|): the Cartesian tree of the keys.
    Tree build(std::string_view s) {
        std::vector<u32>& spine = build_spine_;  // right spine, root first
        spine.clear();
        for (const char ch : s) {
            const u32 key = (next_priority() << 5) | static_cast<u32>(ch - 'a' + 1);
            const u32 id = make(Tree{}, key, Tree{}).ref >> 1;
            // Everything on the spine with a smaller key becomes the left
            // subtree of the new node.
            Tree below;
            while (!spine.empty() && pool_[spine.back()].key < key) {
                below = {static_cast<Ref>(spine.back() << 1), pool_[spine.back()].lsize + 1 + below.size};
                spine.pop_back();
            }
            pool_[id].left = below.ref;
            pool_[id].lsize = below.size;
            if (!spine.empty()) pool_[spine.back()].right = static_cast<Ref>(id << 1);
            spine.push_back(id);
        }
        return spine.empty() ? Tree{} : Tree{static_cast<Ref>(spine.front() << 1), static_cast<u32>(s.size())};
    }

    // Ends an operation: computes the hashes of the nursery nodes, children
    // first, and freezes them.
    Tree commit(Tree top) {
        pw_.ensure(top.size);
        sealed_ = 0;
        if (in_nursery(top.ref)) seal(top);
        // Every nursery node must be part of the new version: a node left
        // behind would be a leak, a node sealed twice would be shared.
        if (sealed_ != pool_.size() - mark_) throw std::logic_error("tte: nursery accounting is off");
        mark_ = pool_.size();
        return top;
    }
    void seal(Tree t) {  // t's root is a nursery node
        const std::size_t id = t.ref >> 1;
        const Node n = pool_[id];
        const Tree l{n.left, n.lsize}, r{n.right, t.size - 1 - n.lsize};
        // Frozen nodes never point into the nursery, so the walk stops there.
        if (in_nursery(l.ref)) seal(l);
        if (in_nursery(r.ref)) seal(r);
        const Summary s = around(summary(l), n.key & 31, summary(r), pw_);
        pool_[id].f = s.f;
        pool_[id].b = s.b;
        ++sealed_;
    }

    int publish(Tree top) {
        versions_.push_back(commit(top));
        return static_cast<int>(versions_.size()) - 1;
    }

    // ---- queries ----------------------------------------------------------

    // A subtree placed inside its text.
    struct Frame {
        Tree t;    // covers positions [lo, lo + t.size)
        u32 lo;
        Hash acc;  // f(text[0, lo))
    };
    // f(text[0, q)) for a position q inside (or at either end of) `from`.
    // Loads only the nodes on the path to q.
    Hash walk(Frame from, u32 q) const {
        Tree t = from.t;
        u32 lo = from.lo;
        Hash acc = from.acc;
        while (q != lo) {
            if (q == lo + t.size) return acc + pw_[lo] * forward(t.ref);
            const View n = expose(t);
            if (q <= lo + n.left.size) {
                t = n.left;
            } else {
                acc = acc + pw_[lo] * head(t.ref, n);
                lo += n.left.size + 1;
                t = n.right;
            }
        }
        return acc;
    }
    // f(first k characters of t).
    Hash prefix_hash(Tree t, u32 k) const { return walk(Frame{t, 0, Hash{}}, k); }
    // Is a[la, ra) the same text as b[lb, lb + ra - la)?
    // f(a[0, ra)) - f(a[0, la)) = x^la f(a[la, ra)); both sides are brought
    // to the common factor x^(la + lb) instead of dividing.
    bool same_text(Tree a, u32 la, u32 ra, Tree b, u32 lb) const {
        if (la == ra || (a.ref == b.ref && la == lb)) return true;
        const u32 rb = lb + (ra - la);
        return (prefix_hash(a, ra) - prefix_hash(a, la)) * pw_[lb] ==
               (prefix_hash(b, rb) - prefix_hash(b, lb)) * pw_[la];
    }

    // A finger: the path from the root to some position, as a stack of
    // frames.  seek() moves the finger to position q and returns f(text[0, q));
    // it costs the length of the tree path between the old and the new
    // position, not the depth of the tree.
    Hash seek(std::vector<Frame>& path, u32 q) const {
        while (q < path.back().lo || q > path.back().lo + path.back().t.size) path.pop_back();
        for (;;) {
            const Frame fr = path.back();
            if (q == fr.lo) return fr.acc;
            if (q == fr.lo + fr.t.size) return fr.acc + pw_[fr.lo] * forward(fr.t.ref);
            const View n = expose(fr.t);
            if (q <= fr.lo + n.left.size) {
                path.push_back({n.left, fr.lo, fr.acc});
            } else {
                path.push_back({n.right, fr.lo + n.left.size + 1, fr.acc + pw_[fr.lo] * head(fr.t.ref, n)});
            }
        }
    }

    // ---- inspection helpers -----------------------------------------------

    void append_text(Tree t, std::string& out) const {
        while (t.size != 0) {
            const View v = expose(t);
            append_text(v.left, out);
            out.push_back(static_cast<char>('a' + (v.key & 31) - 1));
            t = v.right;
        }
    }
    u32 depth_of(Tree t) const {
        if (t.size == 0) return 0;
        const View v = expose(t);
        return 1 + std::max(depth_of(v.left), depth_of(v.right));
    }
    // Returns mu(t) recomputed from the characters; appends the text of t to
    // `text`; records the first violated invariant in `problem`.
    Summary verify(Tree t, u32 parent_key, std::string& text, std::string& problem) const {
        if (t.size == 0) return {};
        if ((t.ref >> 1) == 0 || (t.ref >> 1) >= mark_) {
            if (problem.empty()) problem = "reference to a node that is not frozen";
            return {};
        }
        const View v = expose(t);
        if (v.key > parent_key && problem.empty()) problem = "heap order violated";
        const u32 code = v.key & 31;
        if ((code < 1 || code > 26) && problem.empty()) problem = "bad character code";
        if (v.left.size >= t.size && problem.empty()) problem = "left size out of range";
        if (!problem.empty()) return {};
        const std::size_t start = text.size();
        const Summary l = verify(v.left, v.key, text, problem);
        if (text.size() - start != v.left.size && problem.empty()) problem = "stored left size is wrong";
        text.push_back(static_cast<char>('a' + code - 1));
        const Summary r = verify(v.right, v.key, text, problem);
        if (text.size() - start != t.size && problem.empty()) problem = "subtree length is wrong";
        const Summary here = around(l, code, r, pw_);
        if (!(here == summary(t)) && problem.empty()) problem = "stored hashes differ from the children's";
        // Independent of the tree: hash the characters directly.
        Summary direct;
        for (std::size_t i = start; i < text.size(); ++i)
            direct = concat(direct, single(static_cast<u32>(text[i] - 'a' + 1)), pw_);
        if (!(direct == here) && problem.empty()) problem = "hashes differ from the text";
        return here;
    }

    // ---- randomness -------------------------------------------------------

    u64 next_random() {  // splitmix64
        u64 z = (rng_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    u32 next_priority() { return static_cast<u32>(next_random() >> 37); }  // 27 bits
    Hash random_base() {
        Hash x;
        for (u64& c : x.v) {
            u64 r;
            do r = next_random() >> (64 - kExponent); while (r >= kP - 3);  // uniform in [0, p - 3)
            c = r + 2;                                        // uniform in [2, p - 2]
        }
        return x;
    }

    // ---- state ------------------------------------------------------------

    Pool<Node> pool_;             // all nodes of all versions
    std::size_t mark_ = 0;        // nodes below are frozen; nodes at/above are the nursery
    std::vector<Tree> versions_;  // versions_[v] is the root (and length) of version v
    u64 rng_;
    Powers pw_;
    std::size_t sealed_ = 0;      // nursery nodes hashed by the commit in progress
    std::vector<u32> build_spine_;     // scratch for build()
    mutable std::vector<Frame> finger_a_, finger_b_;  // scratch for lcp()
};

}  // namespace tte
