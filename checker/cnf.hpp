// DIMACS reader for the verification tools (proof checker, model checker,
// brute force).  Written separately from src/dimacs.hpp on purpose: what a
// checker certifies is "the formula *as the checker read it*", so the checker
// must not inherit a parsing mistake from the solver.
//
// Grammar (identical in intent to the solver's, see README):
//   tokens are separated by blanks / newlines;
//   a token starting with 'c' begins a comment that runs to the end of the line;
//   "p cnf <vars> <clauses>" must precede the first clause;
//   a clause is a sequence of non-zero integers terminated by 0.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace chk {

struct Formula {
    int numVars = 0;
    std::vector<int> lits;         // all clauses back to back
    std::vector<size_t> start{0};  // clause i is lits[start[i] .. start[i+1])
    size_t numClauses() const { return start.size() - 1; }
};

// Splits a stream into whitespace-separated tokens and remembers line numbers.
class Tokens {
public:
    explicit Tokens(FILE* f) : f_(f), buf_(1 << 16) {}
    // Returns false at end of input.
    bool next(std::string& tok) {
        int c;
        while (isBlank(c = peek())) {
            if (c == '\n') line_++;
            pos_++;
        }
        if (c == EOF) return false;
        tok.clear();
        while ((c = peek()) != EOF && !isBlank(c)) {
            tok.push_back(char(c));
            pos_++;
        }
        return true;
    }
    void skipLine() {
        int c;
        while ((c = peek()) != EOF && c != '\n') pos_++;
    }
    size_t line() const { return line_; }

private:
    static bool isBlank(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    int peek() {
        if (pos_ == len_) {
            len_ = fread(buf_.data(), 1, buf_.size(), f_);
            pos_ = 0;
            if (len_ == 0) return EOF;
        }
        return (unsigned char)buf_[pos_];
    }
    FILE* f_;
    std::vector<char> buf_;
    size_t pos_ = 0, len_ = 0, line_ = 1;
};

// Strict decimal integer: optional sign, digits only, bounded magnitude.
inline bool parseInt(const std::string& tok, long long& out) {
    size_t i = 0;
    bool negative = false;
    if (i < tok.size() && (tok[i] == '-' || tok[i] == '+')) negative = tok[i++] == '-';
    if (i == tok.size() || tok.size() - i > 12) return false;
    long long x = 0;
    for (; i < tok.size(); i++) {
        if (tok[i] < '0' || tok[i] > '9') return false;
        x = x * 10 + (tok[i] - '0');
    }
    out = negative ? -x : x;
    return true;
}

inline bool parseCnf(FILE* in, Formula& f, std::string& err) {
    Tokens toks(in);
    std::string tok;
    bool haveHeader = false, open = false;
    long long declaredClauses = 0;
    while (toks.next(tok)) {
        if (tok[0] == 'c') {
            toks.skipLine();
            continue;
        }
        if (!haveHeader) {
            std::string a, b, c;
            long long v;
            if (tok != "p" || !toks.next(a) || a != "cnf" || !toks.next(b) || !toks.next(c) || !parseInt(b, v) ||
                !parseInt(c, declaredClauses) || v < 0 || declaredClauses < 0 || v > 0x3FFFFFFF) {
                err = "malformed 'p cnf <vars> <clauses>' header";
                return false;
            }
            f.numVars = int(v);
            haveHeader = true;
            continue;
        }
        long long x;
        if (!parseInt(tok, x)) {
            err = "line " + std::to_string(toks.line()) + ": unexpected token '" + tok + "'";
            return false;
        }
        if (x == 0) {
            f.start.push_back(f.lits.size());
            open = false;
        } else {
            if (x > f.numVars || -x > f.numVars) {
                err = "line " + std::to_string(toks.line()) + ": literal exceeds declared variable count";
                return false;
            }
            f.lits.push_back(int(x));
            open = true;
        }
    }
    if (!haveHeader) {
        err = "missing 'p cnf' header";
        return false;
    }
    if (open) {
        err = "last clause is not terminated by 0";
        return false;
    }
    if ((long long)f.numClauses() != declaredClauses) {
        err = "clause count differs from header";
        return false;
    }
    return true;
}

// Reads a CNF file; on failure prints a message and returns false.
inline bool loadCnf(const char* path, Formula& f) {
    FILE* in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "error: cannot read %s\n", path);
        return false;
    }
    std::string err;
    const bool ok = parseCnf(in, f, err);
    fclose(in);
    if (!ok) fprintf(stderr, "error: %s: %s\n", path, err.c_str());
    return ok;
}

// Parses a blank-separated list of non-zero literals given on the command line.
inline bool parseLitList(const char* s, int numVars, std::vector<int>& out) {
    std::string tok;
    for (const char* p = s;; p++) {
        if (*p && *p != ' ' && *p != '\t') {
            tok.push_back(*p);
            continue;
        }
        if (!tok.empty()) {
            long long x;
            if (!parseInt(tok, x) || x == 0 || x > numVars || -x > numVars) return false;
            out.push_back(int(x));
            tok.clear();
        }
        if (!*p) return true;
    }
}

}  // namespace chk
