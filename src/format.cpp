// Formatters: the table of standard formatters, the user's configuration, running a
// formatter and the built-in JSON formatter.

#include "format.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h>

#include "i18n.h"
#include "languages.h"
#include "paths.h"
#include "utf8.h"

namespace {

// Standard formatters, best first. Placeholders: {file} the file name (for the formatter's
// configuration lookup), {tabwidth}, {clangstyle} (the project's .clang-format or the editor's
// tab settings), {xmlindent} (the indentation for xmllint).
struct Std {
    const char* lang;
    std::vector<const char*> cmds;
};

const std::vector<Std>& standards() {
    static const char* CLANG = "clang-format {clangstyle} --assume-filename={file}";
    static const char* PRETTIER = "prettier --stdin-filepath {file}";
    static const std::vector<Std> T = {
        {"C", {CLANG}},
        {"C++", {CLANG}},
        {"C#", {CLANG}},
        {"Java", {"google-java-format -", CLANG}},
        {"JavaScript", {PRETTIER, CLANG}},
        {"TypeScript", {PRETTIER, CLANG}},
        {"TSX", {PRETTIER}},
        {"JSON", {PRETTIER}},
        {"CSS", {PRETTIER}},
        {"SCSS", {PRETTIER}},
        {"HTML", {PRETTIER}},
        {"Markdown", {PRETTIER, "mdformat -"}},
        {"YAML", {PRETTIER, "yamlfmt -"}},
        {"Python",
         {"ruff format --stdin-filename {file} -", "black -q --stdin-filename {file} -", "autopep8 -", "yapf"}},
        {"Go", {"gofmt"}},
        {"Rust", {"rustfmt --emit stdout --edition 2021"}},
        {"Zig", {"zig fmt --stdin"}},
        {"Shell", {"shfmt -filename {file}"}},
        {"Fish", {"fish_indent"}},
        {"Lua", {"stylua --stdin-filepath {file} -"}},
        {"Ruby", {"rufo"}},
        {"Perl", {"perltidy -st -se"}},
        {"Kotlin", {"ktlint --stdin -F --log-level=none"}},
        {"Swift", {"swift-format"}},
        {"Scala", {"scalafmt --stdin"}},
        {"Haskell", {"ormolu --stdin-input-file {file}", "fourmolu --stdin-input-file {file}"}},
        {"Elixir", {"mix format -"}},
        {"Erlang", {"erlfmt -"}},
        {"XML", {"XMLLINT_INDENT={xmlindent} xmllint --format -"}},
        {"TOML", {"taplo fmt -"}},
        {"Terraform", {"terraform fmt -"}},
        {"Nix", {"nixfmt", "alejandra -q -"}},
        {"SQL", {"sql-formatter", "pg_format -"}},
        {"CMake", {"cmake-format -"}},
    };
    return T;
}

std::string shellQuote(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
}

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

std::string trimS(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// The program of a command: the first word that is not an environment assignment
std::string programOf(const std::string& cmd) {
    size_t i = 0;
    for (;;) {
        while (i < cmd.size() && cmd[i] == ' ') i++;
        size_t j = cmd.find(' ', i);
        std::string w = cmd.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (w.empty()) return w;
        if (w.find('=') == std::string::npos || w[0] == '/' || w[0] == '.') return w;
        if (j == std::string::npos) return std::string();
        i = j + 1;
    }
}

bool installed(const std::string& prog) {
    if (prog.empty()) return false;
    auto exe = [](const std::string& p) {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(p.c_str(), X_OK) == 0;
    };
    if (prog.find('/') != std::string::npos) return exe(prog);
    const char* path = getenv("PATH");
    std::string ps = path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t s = 0;
    while (s <= ps.size()) {
        size_t e = ps.find(':', s);
        if (e == std::string::npos) e = ps.size();
        std::string d = ps.substr(s, e - s);
        if (d.empty()) d = ".";
        if (exe(d + "/" + prog)) return true;
        s = e + 1;
    }
    return false;
}

// A .clang-format of the project, searched upwards from the file's directory
bool hasClangFormat(std::string dir) {
    for (int k = 0; k < 64; k++) {
        if (fileExists(dir + "/.clang-format") || fileExists(dir + "/_clang-format")) return true;
        if (dir == "/" || dir.empty()) return false;
        dir = dirOf(dir);
    }
    return false;
}

std::string extensionOf(const std::string& lang) {
    for (int i = 0; i < NLANGS; i++) {
        if (lang != LANGS[i].name) continue;
        for (auto& w : splitWords(LANGS[i].files)) {
            std::string f = toUtf8(w);
            if (!f.empty() && f[0] == '.') return f;
        }
    }
    return ".txt";
}

std::string expand(const std::string& cmd, const std::string& lang, const std::string& path, int tabw, bool expandtab) {
    std::string file = path.empty() ? getCwd() + "/untitled" + extensionOf(lang) : path;
    std::string style;
    if (hasClangFormat(dirOf(file))) {
        style = "--style=file";
    } else {
        style = "--style=" + shellQuote("{BasedOnStyle: LLVM, IndentWidth: " + std::to_string(tabw) +
                                        ", TabWidth: " + std::to_string(tabw) +
                                        ", UseTab: " + std::string(expandtab ? "Never" : "ForIndentation") + "}");
    }
    std::string xmlIndent = shellQuote(expandtab ? std::string(size_t(tabw), ' ') : std::string("\t"));
    std::string r;
    for (size_t i = 0; i < cmd.size(); i++) {
        if (cmd[i] == '{') {
            size_t e = cmd.find('}', i);
            if (e != std::string::npos) {
                std::string k = cmd.substr(i + 1, e - i - 1);
                std::string v;
                bool known = true;
                if (k == "file") v = shellQuote(file);
                else if (k == "tabwidth") v = std::to_string(tabw);
                else if (k == "clangstyle") v = style;
                else if (k == "xmlindent") v = xmlIndent;
                else known = false;
                if (known) {
                    r += v;
                    i = e;
                    continue;
                }
            }
        }
        r += cmd[i];
    }
    return r;
}

}  // namespace

std::string formatConfigPath() {
    const char* x = getenv("XDG_CONFIG_HOME");
    if (x && *x) return std::string(x) + "/edith/formatters";
    const char* h = getenv("HOME");
    return std::string(h ? h : "") + "/.config/edith/formatters";
}

FormatTool findFormatter(const std::string& lang, const std::string& path, int tabw, bool expandtab,
                         std::string* suggestion) {
    FormatTool t;
    if (suggestion) suggestion->clear();
    // the user's configuration
    FILE* f = fopen(formatConfigPath().c_str(), "r");
    if (f) {
        char buf[4096];
        std::string found;
        bool have = false;
        while (fgets(buf, sizeof buf, f)) {
            std::string l = trimS(buf);
            if (l.empty() || l[0] == '#') continue;
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            if (ieq(trimS(l.substr(0, eq)), lang)) {
                found = trimS(l.substr(eq + 1));
                have = true;
            }
        }
        fclose(f);
        if (have) {
            if (ieq(found, "builtin")) {
                t.builtin = true;
                t.program = "builtin";
            } else if (ieq(found, "none") || found.empty()) {
                t.program = "none";
            } else {
                t.command = expand(found, lang, path, tabw, expandtab);
                t.program = programOf(found);
            }
            return t;
        }
    }
    for (auto& s : standards()) {
        if (lang != s.lang) continue;
        for (const char* c : s.cmds) {
            std::string prog = programOf(c);
            if (installed(prog)) {
                t.command = expand(c, lang, path, tabw, expandtab);
                t.program = prog;
                return t;
            }
        }
        if (suggestion && !s.cmds.empty()) *suggestion = programOf(s.cmds[0]);
    }
    if (lang == "JSON") {
        t.builtin = true;
        t.program = "builtin";
    }
    return t;
}

bool indentSignificant(const std::string& lang) {
    static const char* const L[] = {"Python", "YAML", "Makefile", "Haskell", "Markdown", "Diff", "BASIC", "Batch",
                                    "Assembler", "NASM", "BibTeX", "INI", "Fortran"};
    for (auto l : L)
        if (lang == l) return true;
    return false;
}

std::vector<int> braceIndents(const std::vector<u32>& lines, int y1, int y2, int sw, int tabw) {
    std::vector<int> out;
    auto lead = [&](const u32& l) {
        size_t i = 0;
        while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
        return i;
    };
    auto width = [&](const u32& l, size_t n) {
        int c = 0;
        for (size_t i = 0; i < n; i++) c += l[i] == '\t' ? tabw - c % tabw : 1;
        return c;
    };
    auto lastCode = [&](const u32& l) -> char32_t {
        size_t e = l.find(U"//");
        long x = long(e == u32::npos ? l.size() : e) - 1;
        while (x >= 0 && (l[size_t(x)] == ' ' || l[size_t(x)] == '\t')) x--;
        return x >= 0 ? l[size_t(x)] : 0;
    };
    std::vector<int> newW(lines.size(), -1);
    for (int y = y1; y <= y2 && y < int(lines.size()); y++) {
        const u32& l = lines[size_t(y)];
        size_t i = lead(l);
        if (i == l.size()) { out.push_back(-1); continue; }
        int p = y - 1;
        while (p >= 0 && lead(lines[size_t(p)]) == lines[size_t(p)].size()) p--;
        int w = 0;
        if (p >= 0) {
            w = newW[size_t(p)] >= 0 ? newW[size_t(p)] : width(lines[size_t(p)], lead(lines[size_t(p)]));
            char32_t lc = lastCode(lines[size_t(p)]);
            if (lc == '{' || lc == '(' || lc == '[') w += sw;
        }
        if (l[i] == '}' || l[i] == ')' || l[i] == ']') w = std::max(0, w - sw);
        newW[size_t(y)] = w;
        out.push_back(w);
    }
    return out;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {

struct JsonFmt {
    const u32& s;
    size_t i = 0;
    u32 ind;          // one level
    std::vector<u32> out;
    u32 cur;
    u32 err;

    JsonFmt(const u32& text, u32 unit) : s(text), ind(std::move(unit)) {}

    int lineOf(size_t p) const {
        return int(std::count(s.begin(), s.begin() + long(std::min(p, s.size())), U'\n')) + 1;
    }
    bool fail(const char32_t* what) {  // what is already translated
        if (err.empty()) err = fmtU(trs("Invalid JSON in line %d: ").c_str(), lineOf(i)) + what;
        return false;
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        if (i + 1 < s.size() && s[i] == '/' && (s[i + 1] == '/' || s[i + 1] == '*')) {
            // comments are not JSON
            err = fmtU(trs("Invalid JSON in line %d: ").c_str(), lineOf(i)) + tr(U"comments are not allowed");
        }
    }
    void newline(int depth) {
        out.push_back(cur);
        cur.clear();
        for (int k = 0; k < depth; k++) cur += ind;
    }
    bool value(int depth) {
        ws();
        if (!err.empty()) return false;
        if (i >= s.size()) return fail(tr(U"a value is missing"));
        char32_t c = s[i];
        if (c == '{' || c == '[') {
            char32_t close = c == '{' ? '}' : ']';
            cur += c;
            i++;
            ws();
            if (i < s.size() && s[i] == close) {
                cur += close;
                i++;
                return true;
            }
            for (;;) {
                newline(depth + 1);
                if (c == '{') {
                    ws();
                    if (i >= s.size() || s[i] != '"') return fail(tr(U"a key in quotes is expected"));
                    if (!str()) return false;
                    ws();
                    if (i >= s.size() || s[i] != ':') return fail(tr(U"':' is expected"));
                    i++;
                    cur += U": ";
                }
                if (!value(depth + 1)) return false;
                ws();
                if (!err.empty()) return false;
                if (i < s.size() && s[i] == ',') {
                    cur += ',';
                    i++;
                    ws();
                    if (i < s.size() && s[i] == close) return fail(tr(U"a trailing comma is not allowed"));
                    continue;
                }
                if (i < s.size() && s[i] == close) {
                    i++;
                    newline(depth);
                    cur += close;
                    return true;
                }
                return fail(c == '{' ? tr(U"',' or '}' is expected") : tr(U"',' or ']' is expected"));
            }
        }
        if (c == '"') return str();
        // numbers and literals are kept as written
        size_t st = i;
        while (i < s.size() && !strchr(",:]} \t\r\n{[\"", int(s[i] < 128 ? s[i] : 'x'))) i++;
        u32 tok = s.substr(st, i - st);
        if (tok.empty()) return fail(tr(U"a value is missing"));
        bool num = true;
        for (char32_t ch : tok)
            if (!((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E'))
                num = false;
        if (!num && tok != U"true" && tok != U"false" && tok != U"null") return fail(tr(U"an unknown value"));
        cur += tok;
        return true;
    }
    bool str() {
        size_t st = i;
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\') i++;
            else if (s[i] == '\n') return fail(tr(U"a string must end in the same line"));
            i++;
        }
        if (i >= s.size()) return fail(tr(U"a string is not closed"));
        i++;
        cur += s.substr(st, i - st);
        return true;
    }
};

}  // namespace

bool formatJson(const std::vector<u32>& in, int indentWidth, bool tabs, std::vector<u32>& out, u32& err) {
    u32 text;
    for (size_t k = 0; k < in.size(); k++) {
        if (k) text += '\n';
        text += in[k];
    }
    JsonFmt f(text, tabs ? u32(U"\t") : u32(size_t(std::max(1, indentWidth)), ' '));
    f.ws();
    if (f.i >= text.size()) {  // an empty file stays empty
        out = in;
        return true;
    }
    if (!f.value(0)) {
        err = f.err;
        return false;
    }
    f.ws();
    if (!f.err.empty()) { err = f.err; return false; }
    if (f.i < text.size()) {
        f.fail(tr(U"text after the end"));
        err = f.err;
        return false;
    }
    f.out.push_back(f.cur);
    f.out.push_back(u32());  // a line break at the end of the file
    out = std::move(f.out);
    return true;
}
