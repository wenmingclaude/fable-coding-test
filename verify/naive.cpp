// naive -- the reference answer, written to be read rather than to be fast.
//
// Every version is kept as its own std::string and every operation is the
// obvious loop over characters.  It shares no code with the real editor
// (src/), not even input parsing.  Time and memory are proportional to the
// total size of all versions, so it is only meant for small inputs.
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> version(1);
    long q;
    std::cin >> version[0] >> q;

    while (q-- > 0) {
        std::string op;
        std::cin >> op;

        if (op == "INSERT") {
            long v, p;
            std::string s;
            std::cin >> v >> p >> s;
            const std::string& t = version[v];
            version.push_back(t.substr(0, p) + s + t.substr(p));

        } else if (op == "DELETE") {
            long v, l, r;
            std::cin >> v >> l >> r;
            const std::string& t = version[v];
            version.push_back(t.substr(0, l) + t.substr(r));

        } else if (op == "REVERSE") {
            long v, l, r;
            std::cin >> v >> l >> r;
            const std::string& t = version[v];
            std::string middle = t.substr(l, r - l);
            std::string reversed(middle.rbegin(), middle.rend());
            version.push_back(t.substr(0, l) + reversed + t.substr(r));

        } else if (op == "MOVE") {
            long v, l, r, p;
            std::cin >> v >> l >> r >> p;
            const std::string& t = version[v];
            std::string cut = t.substr(l, r - l);
            std::string rest = t.substr(0, l) + t.substr(r);
            version.push_back(rest.substr(0, p) + cut + rest.substr(p));

        } else if (op == "EQUAL") {
            long v1, l1, r1, v2, l2, r2;
            std::cin >> v1 >> l1 >> r1 >> v2 >> l2 >> r2;
            bool same = version[v1].substr(l1, r1 - l1) == version[v2].substr(l2, r2 - l2);
            std::cout << (same ? "YES" : "NO") << '\n';

        } else if (op == "PAL") {
            long v, l, r;
            std::cin >> v >> l >> r;
            const std::string& t = version[v];
            bool palindrome = true;
            for (long i = l, j = r - 1; i < j; ++i, --j)
                if (t[i] != t[j]) palindrome = false;
            std::cout << (palindrome ? "YES" : "NO") << '\n';

        } else if (op == "LCP") {
            long v1, p1, v2, p2;
            std::cin >> v1 >> p1 >> v2 >> p2;
            const std::string& a = version[v1];
            const std::string& b = version[v2];
            long k = 0;
            while (p1 + k < (long)a.size() && p2 + k < (long)b.size() && a[p1 + k] == b[p2 + k]) ++k;
            std::cout << k << '\n';

        } else {
            std::cerr << "naive: unknown operation " << op << '\n';
            return 2;
        }
    }
    return 0;
}
