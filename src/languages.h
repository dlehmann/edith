#pragma once
// The table of supported languages: file detection, display names and the
// keyword lists for the fallback lexer.

#include <cstdint>
#include <string>
#include <vector>

#include "common.h"

// Highlight class of a character and its foreground color on the blue background
enum { H_TEXT, H_KEYWORD, H_TYPE, H_STRING, H_NUMBER, H_COMMENT, H_PREPROC, H_FUNC, H_SPECIAL };
inline constexpr uint8_t HLCOL[] = {7, 15, 10, 11, 13, 6, 9, 14, 9};

enum LexKind { LK_CODE, LK_MARKUP, LK_MARKDOWN, LK_INI, LK_YAML, LK_DIFF };

enum : unsigned {
    F_NOCASE = 1u << 0,      // keywords are case-insensitive
    F_PREPROC = 1u << 1,     // '#' at the start of a line = preprocessor
    F_DOLLAR = 1u << 2,      // $var, ${var}
    F_HASHSPACE = 1u << 3,   // '#' starts a comment only after whitespace (shell)
    F_NOESC = 1u << 4,       // no backslash escapes in strings
    F_DECOR = 1u << 5,       // @annotation / @decorator
    F_CHARSTRICT = 1u << 6,  // ' only as a short character literal (Rust lifetimes, Haskell)
    F_DASHID = 1u << 7,      // '-' is part of identifiers (CSS, PowerShell)
    F_LABELS = 1u << 8,      // "name:" at the start of a line = label / target
    F_PERCENT = 1u << 9,     // %VAR% (Batch)
    F_FIRSTWORD = 1u << 10,  // first word of a line = instruction (assembler)
    F_KEYSTR = 1u << 11,     // highlight "key": (JSON)
    F_BSCMD = 1u << 12,      // \command (TeX)
    F_HEXCOLOR = 1u << 13,   // #fff (CSS)
    F_SYMBOL = 1u << 14,     // :symbol (Ruby, Elixir)
    F_ATVAR = 1u << 15,      // @var (Ruby, Perl)
    F_LISP = 1u << 16,       // Lisp identifiers containing -?!*+<>=/
    F_MAKE = 1u << 17,       // $(VAR) (Makefile)
    F_BATLABEL = 1u << 18,   // :label (Batch)
    F_DOTID = 1u << 19,      // .directive / %macro (assembler)
};

struct LangDef {
    const char* name;
    const char* files;    // extensions (".c") and file names ("Makefile"), separated by spaces
    const char* shebang;  // interpreter names for "#!" lines
    int kind;
    unsigned flags;
    const char* lineCmt;   // line comments
    const char* startCmt;  // comments only at the start of a line (REM, ::)
    const char* blk1;      // block comment "start end"
    const char* blk2;
    const char* str;       // single-line string delimiters
    const char* mlstr;     // multi-line strings: "open close escapes(0/1)" ...
    const char* kw;
    const char* ty;        // types, constants, built-in functions
};

extern const LangDef LANGS[];
extern const int NLANGS;

std::vector<u32> splitWords(const char* s);
u32 lower(u32 s);
// Detects the language from file name and "#!" line; -1 = none.
int detectLang(const std::string& path, const u32& firstLine);
