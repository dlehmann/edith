#pragma once
// "Reformat Code": formats a buffer with the standard formatter of its language
// (clang-format, ruff, gofmt, rustfmt, prettier, ...) when that program is installed.
// The formatters are optional: without one, JSON is formatted by the built-in formatter and
// other languages are re-indented by their syntax tree (see TsDoc::indents).
//
// The commands can be changed in ~/.config/edith/formatters ($XDG_CONFIG_HOME/edith/formatters):
//
//     # language = command     ({file} = the file, {tabwidth} = the tab width, also {clangstyle}
//     #                         and {xmlindent}, see expand() in format.cpp)
//     Python = black -q --stdin-filename {file} -
//     C = clang-format --style=Google --assume-filename={file}
//     JSON = builtin           (the built-in formatter)
//     Rust = none              (only re-indent)

#include <string>
#include <vector>

#include "common.h"

struct FormatTool {
    std::string command;  // shell command with the placeholders replaced; "" = none
    std::string program;  // its program name (shown to the user)
    bool builtin = false; // the built-in JSON formatter
};

// The formatter for a language (a LANGS name): the user's configuration first, then the first
// standard formatter that is installed. suggestion = the program to install when there is none.
FormatTool findFormatter(const std::string& lang, const std::string& path, int tabw, bool expandtab,
                         std::string* suggestion = nullptr);

// Formats JSON (keeps the order of keys and the spelling of numbers and strings).
bool formatJson(const std::vector<u32>& in, int indentWidth, bool tabs, std::vector<u32>& out, u32& err);

// Languages whose indentation has a meaning: "Reformat Code" does not re-indent them by itself.
bool indentSignificant(const std::string& lang);

// Indentation by braces for languages without an indents query: one level more after a line
// ending with { ( [ and one less for a line starting with } ) ]; -1 = keep (empty lines).
std::vector<int> braceIndents(const std::vector<u32>& lines, int y1, int y2, int sw, int tabw);

// The path of the configuration file
std::string formatConfigPath();
