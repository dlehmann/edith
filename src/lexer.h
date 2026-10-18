#pragma once
// Simple fallback lexer for languages without a tree-sitter grammar.

#include <cstdint>

#include "common.h"

// Highlights one line. st = state at line start, returns the state at line end.
int highlightLine(int lang, const u32& s, int st, uint8_t* c);
