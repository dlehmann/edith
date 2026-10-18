#pragma once
// Syntax highlighting with tree-sitter: full incremental parsing of the document,
// highlight queries from nvim-treesitter and embedded languages (injections).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <tree_sitter/api.h>

#include "common.h"

struct TsPred {
    std::string name;
    std::vector<std::pair<int, std::string>> args;  // first >= 0: capture id, otherwise text
};

struct TsQ {
    TSQuery* q = nullptr;
    std::vector<std::vector<TsPred>> preds, dirs;  // per pattern: predicates (…?) and directives (…!)
    std::vector<std::string> capNames;
    std::vector<uint8_t> capClass;
    int dropped = 0;  // patterns that did not match the grammar
    TsQ() = default;
    TsQ(const TsQ&) = delete;
    TsQ& operator=(const TsQ&) = delete;
    ~TsQ() {
        if (q) ts_query_delete(q);
    }
};

struct TsLang {
    std::string name;
    const TSLanguage* lang = nullptr;
    TsQ hl, inj, ind;  // highlights, injections, indents
};

struct TsDoc {
    TsLang* L = nullptr;
    TSParser* parser = nullptr;   // main document
    TSParser* iparser = nullptr;  // embedded languages
    TSTree* tree = nullptr;
    std::string text;             // document as UTF-8
    std::vector<uint32_t> ls;     // byte offset of each line start
    bool dirty = true;
    std::unordered_map<std::string, TSTree*> injCache;

    struct Inj {
        TsLang* L;
        std::vector<TSRange> ranges;
    };
    TsDoc() = default;
    TsDoc(const TsDoc&) = delete;
    TsDoc& operator=(const TsDoc&) = delete;
    ~TsDoc();
    void clearInj();
    void reset(TsLang* nl);
    static TSPoint pointIn(const std::vector<uint32_t>& lines, uint32_t b);
    TSPoint pointAt(uint32_t b) const { return pointIn(ls, b); }
    uint32_t byteAt(long row, long col) const;
    TSRange range(uint32_t a, uint32_t b) const { return TSRange{pointAt(a), pointAt(b), a, b}; }

    // Rebuilds the text and reparses incrementally (the edit is found by comparing prefix/suffix)
    void sync(const std::vector<u32>& lines);
    std::string nodeText(TSNode n) const;
    bool check(const TsQ& Q, const TSQueryMatch& m) const;

    // Collects the embedded languages in the visible range
    void collectInj(TsLang* PL, TSNode root, uint32_t vs, uint32_t ve, std::vector<Inj>& out);
    TSTree* parseInj(const Inj& d);
    void paint(TsLang* PL, TSTree* t, uint32_t vs, uint32_t ve, std::vector<uint8_t>& out, int depth);

    // Highlight classes for the bytes [vs, ve) of the document
    void highlight(uint32_t vs, uint32_t ve, std::vector<uint8_t>& out);

    // Indentation (in columns) of the lines y1..y2 from the language's indents query, like
    // nvim-treesitter's indent.lua; -1 = keep the line as it is (strings, comments ...).
    // sw is the width of one indent level. Returns false without an indents query.
    bool indents(const std::vector<u32>& lines, int y1, int y2, int sw, int tabw, std::vector<int>& out);
    // Is the byte column col of line row inside a string (where white space matters)?
    bool insideString(uint32_t row, uint32_t col) const;
};

// tree-sitter grammar name for an editor language (index into LANGS), or nullptr.
const char* tsNameFor(int lang);
// Finds a grammar by name, alias or file extension (e.g. "py", "c++", "shell").
TsLang* tsFind(std::string name);
// Loads every grammar and its queries and prints a report (edith --ts-check).
void tsCheckAll();
