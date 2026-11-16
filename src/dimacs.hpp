// Streaming DIMACS CNF reader used by the solver and the baseline.
// (The checkers under checker/ have their own, separately written reader.)
//
// Grammar accepted:
//   * lines whose first non-blank character is 'c' are comments (anywhere);
//   * exactly one header "p cnf <vars> <clauses>" before the first clause;
//   * clauses are whitespace-separated non-zero integers terminated by 0 and
//     may span lines; "0" on its own is the empty clause;
//   * |literal| must not exceed <vars>;
//   * a final clause without terminating 0 and a clause count that differs
//     from the header are errors: silently guessing could change the answer.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace sat {

class DimacsReader {
public:
    explicit DimacsReader(FILE* f) : f_(f), buf_(1 << 16) {}

    // Reads the header; returns false (with error()) on malformed input.
    bool readHeader() {
        for (;;) {
            int c = skipSpace();
            if (c == 'c') {
                skipLine();
            } else if (c == 'p') {
                next();
                long long v, m;
                if (!word("cnf") || !number(v) || !number(m) || v < 0 || m < 0 || v > 0x3FFFFFFF)
                    return fail("malformed 'p cnf <vars> <clauses>' header");
                numVars = int(v);
                numClauses = m;
                return true;
            } else {
                return fail("expected 'p cnf' header");
            }
        }
    }

    // Reads the next clause as DIMACS integers.  Returns false at end of input
    // or on error (check error()).
    bool readClause(std::vector<int>& out) {
        out.clear();
        for (;;) {
            int c = skipSpace();
            if (c == EOF) {
                if (!out.empty()) return fail("last clause is not terminated by 0");
                if (clausesRead != numClauses) return fail("clause count differs from header");
                return false;
            }
            if (c == 'c') {
                skipLine();
                continue;
            }
            long long x;
            if (!number(x)) return fail("unexpected character in clause");
            if (x == 0) {
                clausesRead++;
                return true;
            }
            if (x > numVars || -x > numVars) return fail("literal exceeds declared variable count");
            out.push_back(int(x));
        }
    }

    const std::string& error() const { return err_; }
    int numVars = 0;
    long long numClauses = 0, clausesRead = 0;

private:
    int peek() {
        if (pos_ == len_) {
            len_ = fread(buf_.data(), 1, buf_.size(), f_);
            pos_ = 0;
            if (len_ == 0) return EOF;
        }
        return (unsigned char)buf_[pos_];
    }
    void next() { pos_++; }
    int skipSpace() {
        int c;
        while ((c = peek()) == ' ' || c == '\t' || c == '\n' || c == '\r') next();
        return c;
    }
    void skipLine() {
        int c;
        while ((c = peek()) != EOF && c != '\n') next();
    }
    bool word(const char* w) {
        skipSpace();
        for (; *w; w++) {
            if (peek() != *w) return false;
            next();
        }
        return true;
    }
    bool number(long long& x) {
        int c = skipSpace();
        bool negative = false;
        if (c == '-' || c == '+') {
            negative = c == '-';
            next();
            c = peek();
        }
        if (c < '0' || c > '9') return false;
        x = 0;
        while ((c = peek()) >= '0' && c <= '9') {
            x = x * 10 + (c - '0');
            if (x > (1ll << 40)) return false;
            next();
        }
        // A number must be followed by whitespace or end of input ("12x" is an error).
        if (c != EOF && c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
        if (negative) x = -x;
        return true;
    }
    bool fail(const char* msg) {
        err_ = msg;
        return false;
    }

    FILE* f_;
    std::vector<char> buf_;
    size_t pos_ = 0, len_ = 0;
    std::string err_;
};

}  // namespace sat
