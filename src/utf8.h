#pragma once
// UTF-8 conversion, terminal character widths and simple case folding.

#include <string>

#include "common.h"

// Appends c to o as UTF-8 (U+FFFD for surrogates and values above U+10FFFF).
void putUtf8(std::string& o, char32_t c);
std::string toUtf8(const u32& s);
// Strict decoding; returns false for invalid UTF-8.
bool fromUtf8(const std::string& s, u32& out);
// UTF-8 to u32; falls back to Latin-1 (byte by byte) for invalid input.
u32 U(const std::string& s);
// Display width of a character in the terminal (0, 1 or 2 columns).
int cw(char32_t c);
// C0 and C1 control characters and DEL: a terminal would execute them (U+009B is CSI,
// U+009D is OSC), so they are never written to the screen.
inline bool isControl(char32_t c) { return c < 32 || (c >= 127 && c < 160); }
int strWidth(const u32& s);
// Simple case folding to lower case (ASCII, Latin-1, Greek, Cyrillic).
char32_t fold(char32_t c);
// Word characters: letters, digits and '_' (also outside of ASCII).
bool isWord(char32_t c);
// printf-style formatting with up to two int arguments.
u32 fmtU(const char* f, int a, int b = 0);
