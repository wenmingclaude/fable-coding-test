// explain -- prints the trees behind a few versions of a short text, to make
// the data structure visible.
//
//   build/explain [TEXT] [SEED]
//
// For each version it draws the tree (root on the left, the text reads from
// top to bottom) and marks which nodes the version shares with older ones.
//   #7        node number 7, read as stored
//   #7~       node number 7, read as its mirror image (orientation bit set)
//   *         node created by this version
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/editor.hpp"

namespace {

using tte::Editor;
using tte::Tree;

std::size_t first_new = 0;  // nodes with this index or above were created by the version being drawn
std::size_t shared = 0, fresh = 0;

void draw(const Editor& ed, Tree t, const std::string& indent) {
    if (t.size == 0) return;
    const tte::Node& n = ed.nodes()[t.ref >> 1];
    const bool mirrored = t.ref & 1;
    // Reading a node through a flipped reference: children swap places and
    // are themselves read mirrored.
    const Tree stored_left{n.left, n.lsize}, stored_right{n.right, t.size - 1 - n.lsize};
    const Tree left = mirrored ? flip(stored_right) : stored_left;
    const Tree right = mirrored ? flip(stored_left) : stored_right;
    const bool is_new = (t.ref >> 1) >= first_new;
    (is_new ? fresh : shared) += 1;

    draw(ed, left, indent + "      ");
    std::printf("%s%c  #%u%s%s\n", indent.c_str(), static_cast<char>('a' + (n.key & 31) - 1), t.ref >> 1,
                mirrored ? "~" : "", is_new ? " *" : "");
    draw(ed, right, indent + "      ");
}

void show(const Editor& ed, int v, const char* how, std::size_t nodes_before) {
    first_new = nodes_before;
    shared = fresh = 0;
    std::printf("\nversion %d = %s   text \"%s\"\n", v, how, ed.text(v).c_str());
    draw(ed, ed.tree(v), "    ");
    std::printf("  -> %zu nodes created, %zu nodes reused from older versions\n", fresh, shared);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string text = argc > 1 ? argv[1] : "abcdefg";
    const tte::u64 seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 7;
    const tte::u32 n = static_cast<tte::u32>(text.size());

    Editor ed(text, seed);
    show(ed, 0, "the initial text", 1);

    std::size_t before = ed.node_count();
    int v = ed.insert(0, n / 2, "xy");
    show(ed, v, "INSERT 0 (middle) xy", before);

    before = ed.node_count();
    v = ed.reverse(0, 0, n);
    show(ed, v, "REVERSE 0 (everything)", before);

    before = ed.node_count();
    v = ed.reverse(0, 1, n - 1);
    show(ed, v, "REVERSE 0 (all but the ends)", before);

    before = ed.node_count();
    v = ed.move(1, 0, 2, n);
    show(ed, v, "MOVE 1 (first two characters to the end)", before);

    std::size_t characters = 0;
    for (int i = 0; i < ed.versions(); ++i) characters += ed.length(i);
    std::printf("\n%d versions holding %zu characters between them are stored in %zu nodes\n", ed.versions(),
                characters, ed.node_count() - 1);
    std::printf("version 0 still reads \"%s\"\n", ed.text(0).c_str());
    return 0;
}
