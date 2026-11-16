// tte -- reads the problem from stdin, writes one line per query to stdout.
//
//   tte [--seed N] [--stats]
//
// --seed fixes the random choices (tree priorities, hash point); by default
// they come from the OS.  --stats prints node/memory figures to stderr.
// Malformed or out-of-range input is reported on stderr with exit status 2.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "editor.hpp"

namespace {

std::vector<char> read_all(std::FILE* in) {
    std::vector<char> buf;
    std::size_t used = 0;
    for (;;) {
        if (buf.size() - used < (1u << 16)) buf.resize(buf.size() * 2 + (1u << 16));
        const std::size_t got = std::fread(buf.data() + used, 1, buf.size() - used, in);
        if (got == 0) break;
        used += got;
    }
    buf.resize(used);
    return buf;
}

[[noreturn]] void fail(long line, const char* what) {
    std::fprintf(stderr, "tte: input line %ld: %s\n", line, what);
    std::exit(2);
}

// Whitespace-separated tokens; tracks the line number for error messages.
class Tokens {
  public:
    explicit Tokens(const std::vector<char>& buf) : p_(buf.data()), end_(buf.data() + buf.size()) {}

    long line() const { return line_; }
    std::string_view word() {
        while (p_ != end_ && is_space(*p_)) line_ += (*p_++ == '\n');
        const char* start = p_;
        while (p_ != end_ && !is_space(*p_)) ++p_;
        if (start == p_) fail(line_, "unexpected end of input");
        return {start, static_cast<std::size_t>(p_ - start)};
    }
    // A non-negative integer below 2^31.
    tte::u32 number() {
        const std::string_view w = word();
        tte::u64 x = 0;
        for (const char c : w) {
            if (c < '0' || c > '9') fail(line_, "expected a non-negative integer");
            x = x * 10 + static_cast<tte::u64>(c - '0');
            if (x >> 31) fail(line_, "number too large");
        }
        return static_cast<tte::u32>(x);
    }

  private:
    static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
    const char* p_;
    const char* end_;
    long line_ = 1;
};

}  // namespace

int main(int argc, char** argv) {
    tte::u64 seed = tte::Editor::entropy_seed();
    bool stats = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--stats")) stats = true;
        else {
            std::fprintf(stderr, "usage: %s [--seed N] [--stats] < input > output\n", argv[0]);
            return 2;
        }
    }

    const std::vector<char> input = read_all(stdin);
    Tokens in(input);
    std::string out;

    try {
        tte::Editor ed(in.word(), seed);
        const tte::u32 q = in.number();
        for (tte::u32 i = 0; i < q; ++i) {
            const std::string_view op = in.word();
            if (op == "INSERT") {
                const int v = static_cast<int>(in.number());
                const tte::u32 p = in.number();
                ed.insert(v, p, in.word());
            } else if (op == "DELETE") {
                const int v = static_cast<int>(in.number());
                const tte::u32 l = in.number(), r = in.number();
                ed.erase(v, l, r);
            } else if (op == "REVERSE") {
                const int v = static_cast<int>(in.number());
                const tte::u32 l = in.number(), r = in.number();
                ed.reverse(v, l, r);
            } else if (op == "MOVE") {
                const int v = static_cast<int>(in.number());
                const tte::u32 l = in.number(), r = in.number(), p = in.number();
                ed.move(v, l, r, p);
            } else if (op == "EQUAL") {
                const int v1 = static_cast<int>(in.number());
                const tte::u32 l1 = in.number(), r1 = in.number();
                const int v2 = static_cast<int>(in.number());
                const tte::u32 l2 = in.number(), r2 = in.number();
                out += ed.equal(v1, l1, r1, v2, l2, r2) ? "YES\n" : "NO\n";
            } else if (op == "PAL") {
                const int v = static_cast<int>(in.number());
                const tte::u32 l = in.number(), r = in.number();
                out += ed.palindrome(v, l, r) ? "YES\n" : "NO\n";
            } else if (op == "LCP") {
                const int v1 = static_cast<int>(in.number());
                const tte::u32 p1 = in.number();
                const int v2 = static_cast<int>(in.number());
                const tte::u32 p2 = in.number();
                out += std::to_string(ed.lcp(v1, p1, v2, p2));
                out += '\n';
            } else {
                fail(in.line(), "unknown operation");
            }
        }
        if (stats) {
            std::fprintf(stderr, "versions %d  nodes %zu  node bytes %zu  (%.1f MiB)\n", ed.versions(),
                         ed.node_count(), sizeof(tte::Node), static_cast<double>(ed.memory_bytes()) / 1048576.0);
        }
    } catch (const std::exception& e) {
        fail(in.line(), e.what());
    }

    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
