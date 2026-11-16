// A forest over vertices 1..n whose edges carry two labels:
//
//   gain    an element of Z_p. Walking an edge a -> b forwards adds `gain`
//           to a running total; walking it backwards subtracts it.
//   expiry  an integer time (kNever = never).
//
// Class invariant: the forest is a MAXIMUM SPANNING FOREST, with respect to
// expiry, of all edges ever inserted. Equivalently, every edge that is not in
// the forest expires no later than each edge on the forest path between its
// endpoints. Two consequences are what the solver needs:
//
//   * of all routes between two vertices, the forest path is one that
//     survives longest;
//   * an edge thrown out of the forest is never needed again, so it is simply
//     forgotten. The forest never holds more than n - 1 edges.
//
// Representation: link-cut trees (Sleator & Tarjan). Every root-to-node path
// of interest is kept in a splay tree ordered from the root downwards. An
// edge is itself a node, sitting between its two endpoints. Each operation
// costs O(log n) amortised.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace dce {

using Residue = std::uint32_t;  // an element of Z_p, stored in [0, p)
inline constexpr Residue kP = 1'000'000'007;

constexpr Residue add(Residue a, Residue b) { return a + b >= kP ? a + b - kP : a + b; }
constexpr Residue negate(Residue a) { return a == 0 ? 0 : kP - a; }
constexpr Residue subtract(Residue a, Residue b) { return add(a, negate(b)); }
constexpr Residue to_residue(std::int64_t x) {
    return static_cast<Residue>((x % kP + kP) % kP);
}

inline constexpr int kNever = std::numeric_limits<int>::max();

class SpanningForest {
public:
    // A walk through the forest, summarised: the total gain collected along
    // it, and the time at which it breaks (the smallest expiry among its edges).
    struct Walk {
        Residue gain;
        int expiry;
    };

    explicit SpanningForest(int vertices)
        : vertices_(vertices), nodes_(2 * static_cast<std::size_t>(vertices)) {}

    // The forest path from a to b, if they are connected.
    std::optional<Walk> path(int a, int b) {
        if (!expose(a, b)) return std::nullopt;
        return Walk{nodes_[b].total, nodes_[b].first_expiry};
    }

    // Inserts the edge a -> b. If a and b were already connected, the edge
    // closes a cycle with the forest path: that cycle (a ~> b along the path,
    // then back through the new edge) is returned, and whichever of its edges
    // expires first is dropped to restore the invariant.
    std::optional<Walk> insert(int a, int b, Residue gain, int expiry) {
        if (!expose(a, b)) {
            assert(edges_ < vertices_ - 1);
            attach(vertices_ + ++edges_, a, b, gain, expiry);
            return std::nullopt;
        }
        const Node& path = nodes_[b];
        const Walk cycle{subtract(path.total, gain), std::min(path.first_expiry, expiry)};
        if (expiry > path.first_expiry) reroute(weakest_edge(b), gain, expiry);
        return cycle;
    }

private:
    // Node 0 is a sentinel ("no node"), 1..n are vertices, n+1..2n-1 are edges.
    // 32 bytes, so that two nodes share a cache line and none straddles two.
    struct alignas(32) Node {
        std::array<int, 2> child{0, 0};  // in the splay tree: [0] is nearer the root
        int parent = 0;                  // splay parent; for a splay root, the node
                                         // its whole path hangs from (0 if none)
        Residue gain = 0;                // gain of stepping across this node, downwards
        Residue total = 0;               // sum of `gain` over the splay subtree
        int expiry = kNever;             // vertices never expire
        int first_expiry = kNever;       // least `expiry` in the splay subtree
        bool flipped = false;            // lazy: the children still have to be reversed
    };
    static_assert(sizeof(Node) == 32);

    // After this call the splay tree rooted at b holds exactly the path a ~> b,
    // in that order. Returns false (and exposes nothing useful) if a and b lie
    // in different trees.
    bool expose(int a, int b) {
        assert(1 <= a && a <= vertices_ && 1 <= b && b <= vertices_);
        make_root(a);
        access(b);
        return a == b || nodes_[a].parent != 0;
    }

    // Links the isolated edge node between a and b, which lie in different trees.
    void attach(int edge, int a, int b, Residue gain, int expiry) {
        make_root(b);
        Node& e = nodes_[edge];
        e.gain = e.total = gain;
        e.expiry = e.first_expiry = expiry;
        e.parent = a;  // the edge hangs below a ...
        nodes_[b].parent = edge;  // ... and b's tree hangs below the edge
    }

    // The edge that expires first within the splay tree rooted at x. Only
    // called when that expiry is finite, so the answer is an edge, not a vertex.
    int weakest_edge(int x) const {
        const int target = nodes_[x].first_expiry;
        while (nodes_[x].expiry != target) {
            const auto& [nearer, farther] = nodes_[x].child;
            x = nodes_[nearer].first_expiry == target ? nearer : farther;
        }
        return x;
    }

    // Precondition: the path a ~> b is exposed and reads [L, f, R]. With the
    // new edge e from b back to a it forms a cycle. Cutting that cycle open at
    // f instead of at e leaves the path [R, e, L]: f's node swaps its two
    // sides and becomes e, now walked from b to a.
    void reroute(int f, Residue gain, int expiry) {
        splay(f);
        Node& e = nodes_[f];
        std::swap(e.child[0], e.child[1]);
        e.gain = negate(gain);
        e.expiry = expiry;
        pull(f);
    }

    // --- link-cut tree machinery ------------------------------------------

    bool is_splay_root(int x) const {
        const Node& p = nodes_[nodes_[x].parent];
        return p.child[0] != x && p.child[1] != x;
    }

    // Reverses the path stored below x. Seen from the other end, every step
    // goes the opposite way, so every gain changes sign.
    void flip(int x) {
        Node& n = nodes_[x];
        std::swap(n.child[0], n.child[1]);
        n.gain = negate(n.gain);
        n.total = negate(n.total);
        n.flipped = !n.flipped;
    }

    void push(int x) {
        Node& n = nodes_[x];
        if (!n.flipped) return;
        for (const int c : n.child)
            if (c != 0) flip(c);
        n.flipped = false;
    }

    void pull(int x) {
        Node& n = nodes_[x];
        const Node& l = nodes_[n.child[0]];  // the sentinel contributes nothing
        const Node& r = nodes_[n.child[1]];
        n.total = add(add(l.total, n.gain), r.total);
        n.first_expiry = std::min({l.first_expiry, n.expiry, r.first_expiry});
    }

    void rotate(int x) {
        const int p = nodes_[x].parent;
        const int g = nodes_[p].parent;
        const int side = nodes_[p].child[1] == x;
        const int inner = nodes_[x].child[side ^ 1];
        if (!is_splay_root(p)) nodes_[g].child[nodes_[g].child[1] == p] = x;
        nodes_[x].parent = g;
        nodes_[x].child[side ^ 1] = p;
        nodes_[p].parent = x;
        nodes_[p].child[side] = inner;
        if (inner != 0) nodes_[inner].parent = p;
        pull(p);
    }

    // Pending flips are settled just ahead of each rotation. Flips still
    // waiting higher up do not interfere: a rotation never changes the order
    // of the nodes below them.
    void splay(int x) {
        while (!is_splay_root(x)) {
            const int p = nodes_[x].parent;
            if (is_splay_root(p)) {
                push(p);
                push(x);
            } else {
                const int g = nodes_[p].parent;
                push(g);
                push(p);
                push(x);
                const bool zigzig = (nodes_[g].child[0] == p) == (nodes_[p].child[0] == x);
                rotate(zigzig ? p : x);
            }
            rotate(x);
        }
        push(x);
        pull(x);
    }

    // Makes the path from the tree's root to x one splay tree, rooted at x.
    void access(int x) {
        int below = 0;
        for (int y = x; y != 0; y = nodes_[y].parent) {
            splay(y);
            nodes_[y].child[1] = below;
            pull(y);
            below = y;
        }
        splay(x);
    }

    void make_root(int x) {
        access(x);
        flip(x);
    }

    int vertices_;
    int edges_ = 0;  // edge nodes handed out so far
    std::vector<Node> nodes_;
};

}  // namespace dce
