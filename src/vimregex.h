#pragma once
// Regular expressions with Vim pattern syntax (":help pattern"): magic / very magic modes,
// groups, alternation, multis (*, \+, \=, \{n,m}, \{-}), character classes, \< \>, \zs \ze,
// look-around, back references and \c / \C. Patterns are matched on a list of lines so
// that they can span line breaks (\n, \_s, \_. ...).

#include <climits>
#include <string>
#include <vector>

#include "common.h"
#include "editor.h"

struct RxMatch {
    Pos s, e;          // matched text [s, e)
    Pos gs[10], ge[10];
    bool g[10] = {};   // group i took part in the match
};

struct VimRegex {
    struct Node {
        int t = 0;
        char32_t c = 0, c2 = 0;
        int a = 0, b = 0;       // repeat bounds, group index, look-around kind, line/column value
        bool nl = false;        // also matches a line break
        bool greedy = true;
        bool neg = false;
        std::vector<int> kids;
        std::vector<std::pair<char32_t, char32_t>> ranges;  // character collection
        int classes = 0;        // [:alpha:] ... inside a collection (bit mask)
    };

    std::vector<Node> nodes;
    int root = -1;
    bool icase = false;
    int ngroups = 0;
    u32 err;  // error message after a failed compile()

    // Compiles a pattern. ignorecase/smartcase as the Vim options; \c and \C override them.
    bool compile(const u32& pat, bool ignorecase = false, bool smartcase = false);
    bool valid() const { return root >= 0; }

    // Anchored match at position p.
    bool matchAt(const std::vector<u32>& L, Pos p, RxMatch& m) const;
    // The same, counting the matcher's steps in steps (gives up after RX_MAX_STEPS).
    bool matchAt(const std::vector<u32>& L, Pos p, RxMatch& m, long& steps) const;
    // First match whose start lies in line y at an index >= x.
    bool findInLine(const std::vector<u32>& L, int y, int x, RxMatch& m) const;
    // Last match whose start lies in line y at an index < x.
    bool findLastInLine(const std::vector<u32>& L, int y, int x, RxMatch& m) const;
    // Whether line y contains a match.
    bool lineMatches(const std::vector<u32>& L, int y) const {
        RxMatch m;
        return findInLine(L, y, 0, m);
    }
};

// Backtracking steps after which a search in a line gives up (a catastrophic pattern such as
// \(a*\)*b would otherwise hang the editor).
constexpr long RX_MAX_STEPS = 5000000;

// Last substitute string, matched by "~" in a pattern.
extern u32 g_rxLastSub;

// Escapes a literal string for use in a magic pattern (used by * and #).
u32 rxEscape(const u32& s);
