// tree-sitter based syntax highlighting.
//
// For languages with a tree-sitter grammar the whole document is parsed (incrementally
// on changes) and highlighted with the queries from nvim-treesitter. Embedded
// languages (HTML in PHP, JS/CSS in HTML, code blocks in Markdown, regex in JS ...) are
// found via injections.scm and parsed with their own grammar.
// Languages without a grammar use the simple fallback lexer (lexer.cpp).

#include "treesitter.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <regex>
#include <unordered_set>

#include "languages.h"
#include "utf8.h"

// Set EDITH_TSDEBUG=<file> to log all captures and injections of each redraw.
static FILE* g_tsDebug = getenv("EDITH_TSDEBUG") ? fopen(getenv("EDITH_TSDEBUG"), "w") : nullptr;

// Generated table of all compiled-in grammars and queries (see tools/gen-registry.sh)
#include "ts_registry.inc"

// Editor language (name in LANGS) → tree-sitter grammar
static const char* const TS_FOR_LANG[][2] = {
    {"C", "c"}, {"C++", "cpp"}, {"C#", "c_sharp"}, {"Java", "java"}, {"Kotlin", "kotlin"},
    {"Swift", "swift"}, {"Scala", "scala"}, {"Dart", "dart"}, {"Go", "go"}, {"Rust", "rust"},
    {"Zig", "zig"}, {"JavaScript", "javascript"}, {"TypeScript", "typescript"}, {"TSX", "tsx"},
    {"Python", "python"}, {"Ruby", "ruby"}, {"Perl", "perl"}, {"PHP", "php"}, {"Lua", "lua"},
    {"Shell", "bash"}, {"Fish", "fish"}, {"PowerShell", "powershell"}, {"Haskell", "haskell"},
    {"Common Lisp", "commonlisp"}, {"Clojure", "clojure"}, {"Scheme", "scheme"}, {"Erlang", "erlang"},
    {"Elixir", "elixir"}, {"R", "r"}, {"Julia", "julia"}, {"SQL", "sql"}, {"CSS", "css"},
    {"SCSS", "scss"}, {"JSON", "json"}, {"HTML", "html"}, {"XML", "xml"}, {"Markdown", "markdown"},
    {"INI", "ini"}, {"TOML", "toml"}, {"YAML", "yaml"}, {"Diff", "diff"}, {"Makefile", "make"},
    {"CMake", "cmake"}, {"Dockerfile", "dockerfile"}, {"Assembler", "asm"}, {"NASM", "nasm"},
    {"TeX", "latex"}, {"Terraform", "terraform"}, {"HCL", "hcl"}, {"Nix", "nix"},
    {"Pascal", "pascal"}, {"Fortran", "fortran"},
};

const char* tsNameFor(int lang) {
    if (lang < 0) return nullptr;
    for (auto& e : TS_FOR_LANG)
        if (!strcmp(e[0], LANGS[lang].name)) return e[1];
    return nullptr;
}

// Highlight class for capture names (prefix match, more specific names first)
static uint8_t tsCaptureClass(const std::string& c) {
    if (c.empty() || c[0] == '_') return 255;
    struct M {
        const char* p;
        uint8_t k;
    };
    static const M map[] = {
        {"none", 255}, {"spell", 255}, {"nospell", 255}, {"conceal", 255}, {"error", 255},
        {"embedded", 255}, {"comment", H_COMMENT},
        {"keyword.directive", H_PREPROC}, {"keyword", H_KEYWORD},
        {"function.macro", H_PREPROC}, {"function", H_FUNC}, {"method", H_FUNC},
        {"constructor", H_TYPE}, {"type", H_TYPE}, {"module", H_TYPE}, {"namespace", H_TYPE},
        {"label", H_TYPE}, {"attribute", H_PREPROC},
        {"tag.attribute", H_TYPE}, {"tag.delimiter", H_TEXT}, {"tag", H_KEYWORD},
        {"string.escape", H_SPECIAL}, {"string.regexp", H_SPECIAL}, {"string.special", H_SPECIAL},
        {"string", H_STRING}, {"character.special", H_SPECIAL}, {"character", H_STRING},
        {"number", H_NUMBER}, {"boolean", H_NUMBER}, {"constant.builtin", H_NUMBER},
        {"constant.macro", H_PREPROC}, {"constant", H_NUMBER},
        {"variable.builtin", H_KEYWORD}, {"variable", H_TEXT}, {"property", H_TEXT},
        {"operator", H_TEXT}, {"punctuation.special", H_SPECIAL}, {"punctuation", H_TEXT},
        {"markup.heading", H_KEYWORD}, {"markup.strong", H_KEYWORD}, {"markup.italic", H_TYPE},
        {"markup.strikethrough", H_COMMENT}, {"markup.underline", H_TYPE}, {"markup.raw", H_STRING},
        {"markup.math", H_STRING}, {"markup.link.url", H_FUNC}, {"markup.link", H_TYPE},
        {"markup.list", H_NUMBER}, {"markup.quote", H_COMMENT}, {"markup.environment", H_KEYWORD},
        {"diff.plus", H_TYPE}, {"diff.minus", H_PREPROC}, {"diff.delta", H_NUMBER},
    };
    for (auto& m : map) {
        size_t L = strlen(m.p);
        if (c.compare(0, L, m.p) == 0 && (c.size() == L || c[L] == '.')) return m.k;
    }
    return 255;
}

static const char* tsQueryRaw(const std::string& lang, const char* kind) {
    for (auto& e : TS_QUERIES)
        if (lang == e.lang && !strcmp(kind, e.kind)) return e.text;
    return nullptr;
}

// Query source including the queries inherited via "; inherits: a,b"
static std::string tsQuerySource(const std::string& lang, const char* kind, int depth = 0) {
    const char* raw = tsQueryRaw(lang, kind);
    if (!raw || depth > 6) return std::string();
    std::string s = raw, pre;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(pos, e - pos);
        if (!line.empty() && line[0] != ';') break;
        size_t k = line.find("inherits:");
        if (k != std::string::npos) {
            std::string cur;
            for (char ch : line.substr(k + 9) + ",") {
                if (ch == ',') {
                    if (!cur.empty()) pre += tsQuerySource(cur, kind, depth + 1) + "\n";
                    cur.clear();
                } else if (ch != ' ' && ch != '\t' && ch != '(' && ch != ')' && ch != '\r') {
                    cur += ch;
                }
            }
        }
        pos = e + 1;
    }
    return pre + s;
}

// Splits a query into its individual patterns (for error recovery)
static std::vector<std::string> tsSplitPatterns(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool closed = false;
    auto flush = [&] {
        if (depth == 0 && closed) {
            out.push_back(cur);
            cur.clear();
            closed = false;
        }
    };
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch == ';') {
            size_t e = s.find('\n', i);
            i = e == std::string::npos ? s.size() : e;
            cur += '\n';
            continue;
        }
        if (ch == '"') {
            size_t j = i + 1;
            while (j < s.size() && s[j] != '"') {
                if (s[j] == '\\') j++;
                j++;
            }
            flush();
            cur.append(s, i, std::min(j, s.size() - 1) - i + 1);
            i = j;
            if (depth == 0) closed = true;
            continue;
        }
        if (ch == '(' || ch == '[') {
            flush();
            depth++;
        } else if (ch == ')' || ch == ']') {
            depth--;
            if (depth == 0) closed = true;
        }
        cur += ch;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static TSQuery* tsCompile(const TSLanguage* L, const std::string& src, int& dropped) {
    uint32_t off;
    TSQueryError err;
    dropped = 0;
    TSQuery* q = ts_query_new(L, src.data(), uint32_t(src.size()), &off, &err);
    if (q || src.empty()) return q;
    // Some patterns do not match the grammar: drop them and keep the rest
    std::string ok;
    for (auto& p : tsSplitPatterns(src)) {
        TSQuery* t = ts_query_new(L, p.data(), uint32_t(p.size()), &off, &err);
        if (t) {
            ts_query_delete(t);
            ok += p;
            ok += '\n';
        } else if (p.find_first_not_of(" \t\r\n") != std::string::npos) {
            dropped++;
        }
    }
    return ts_query_new(L, ok.data(), uint32_t(ok.size()), &off, &err);
}

static void tsLoadQuery(TsQ& Q, const TSLanguage* L, const std::string& src, bool highlights,
                        const std::string& lang = std::string()) {
    // In data formats @property marks the keys, which should stand out
    static const std::unordered_set<std::string> dataLangs = {"json", "yaml", "toml", "ini", "hcl", "terraform"};
    bool data = dataLangs.count(lang) > 0;
    Q.q = tsCompile(L, src, Q.dropped);
    if (!Q.q) return;
    uint32_t np = ts_query_pattern_count(Q.q);
    Q.preds.resize(np);
    Q.dirs.resize(np);
    for (uint32_t p = 0; p < np; p++) {
        uint32_t len;
        const TSQueryPredicateStep* st = ts_query_predicates_for_pattern(Q.q, p, &len);
        TsPred cur;
        bool first = true;
        for (uint32_t i = 0; i < len; i++) {
            uint32_t l;
            if (st[i].type == TSQueryPredicateStepTypeDone) {
                if (!cur.name.empty()) (cur.name.back() == '!' ? Q.dirs : Q.preds)[p].push_back(cur);
                cur = TsPred();
                first = true;
            } else if (first) {
                const char* v = ts_query_string_value_for_id(Q.q, st[i].value_id, &l);
                cur.name.assign(v, l);
                first = false;
            } else if (st[i].type == TSQueryPredicateStepTypeCapture) {
                cur.args.push_back({int(st[i].value_id), std::string()});
            } else {
                const char* v = ts_query_string_value_for_id(Q.q, st[i].value_id, &l);
                cur.args.push_back({-1, std::string(v, l)});
            }
        }
    }
    uint32_t nc = ts_query_capture_count(Q.q);
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t l;
        const char* n = ts_query_capture_name_for_id(Q.q, i, &l);
        Q.capNames.emplace_back(n, l);
        uint8_t k = highlights ? tsCaptureClass(Q.capNames.back()) : 255;
        if (data && Q.capNames.back().compare(0, 8, "property") == 0) k = H_TYPE;
        Q.capClass.push_back(k);
    }
}

static std::string asciiLower(std::string s) {
    for (auto& c : s) c = char(tolower((unsigned char)c));
    return s;
}

// Finds a grammar by name, alias or file extension (e.g. "py", "c++", "shell")
TsLang* tsFind(std::string name) {
    static std::vector<std::unique_ptr<TsLang>> loaded;
    static std::unordered_map<std::string, TsLang*> byName;
    name = asciiLower(name);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\r')) name.pop_back();
    while (!name.empty() && (name[0] == ' ' || name[0] == '{' || name[0] == '.')) name.erase(0, 1);
    auto it = byName.find(name);
    if (it != byName.end()) return it->second;

    static const char* const alias[][2] = {
        {"js", "javascript"}, {"jsx", "javascript"}, {"node", "javascript"}, {"ecmascript", "javascript"},
        {"mjs", "javascript"}, {"ts", "typescript"}, {"py", "python"}, {"python3", "python"},
        {"py3", "python"}, {"sh", "bash"}, {"shell", "bash"}, {"zsh", "bash"}, {"ksh", "bash"},
        {"console", "bash"}, {"shell-session", "bash"}, {"shellscript", "bash"}, {"rs", "rust"},
        {"c++", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"cs", "c_sharp"}, {"csharp", "c_sharp"},
        {"c#", "c_sharp"}, {"rb", "ruby"}, {"yml", "yaml"}, {"md", "markdown"}, {"golang", "go"},
        {"kt", "kotlin"}, {"kts", "kotlin"}, {"ps1", "powershell"}, {"pwsh", "powershell"},
        {"ex", "elixir"}, {"exs", "elixir"}, {"hs", "haskell"}, {"pl", "perl"}, {"pm", "perl"},
        {"clj", "clojure"}, {"cljs", "clojure"}, {"lisp", "commonlisp"}, {"cl", "commonlisp"},
        {"elisp", "commonlisp"}, {"emacs-lisp", "commonlisp"}, {"scm", "scheme"}, {"racket", "scheme"},
        {"tex", "latex"}, {"patch", "diff"}, {"makefile", "make"}, {"mk", "make"}, {"docker", "dockerfile"},
        {"jsonc", "json"}, {"json5", "json"}, {"htm", "html"}, {"xhtml", "html"}, {"svg", "xml"},
        {"tf", "terraform"}, {"postgres", "sql"}, {"postgresql", "sql"}, {"mysql", "sql"}, {"sqlite", "sql"},
        {"plsql", "sql"}, {"jl", "julia"}, {"erl", "erlang"}, {"f90", "fortran"}, {"delphi", "pascal"},
        {"objective-c", "c"}, {"objc", "c"}, {"cuda", "cpp"}, {"glsl", "c"}, {"x86asm", "nasm"},
        {"assembly", "asm"}, {"gas", "asm"},
    };
    std::string target = name;
    for (auto& a : alias)
        if (name == a[0]) target = a[1];
    if (target != name) {
        // an alias shares the grammar loaded under its real name
        TsLang* r = tsFind(target);
        byName[name] = r;
        return r;
    }

    TsLang* result = nullptr;
    const TsGrammarEntry* g = nullptr;
    for (auto& e : TS_GRAMMARS)
        if (target == e.name) g = &e;
    if (g) {
        auto L = std::make_unique<TsLang>();
        L->name = target;
        L->lang = g->fn();
        tsLoadQuery(L->hl, L->lang, tsQuerySource(target, "highlights"), true, target);
        tsLoadQuery(L->inj, L->lang, tsQuerySource(target, "injections"), false);
        tsLoadQuery(L->ind, L->lang, tsQuerySource(target, "indents"), false);
        result = L.get();
        loaded.push_back(std::move(L));
    } else if (target == name) {
        // via the editor's language detection (file extension or language name)
        int l = detectLang("x." + name, u32());
        if (l < 0)
            for (int i = 0; i < NLANGS; i++)
                if (asciiLower(LANGS[i].name) == name) l = i;
        const char* tn = tsNameFor(l);
        if (tn && name != tn) result = tsFind(tn);
    }
    byName[name] = result;
    return result;
}

// Converts a Lua pattern (from #lua-match?) into an ECMAScript regex
static std::string luaToRegex(const std::string& p) {
    std::string r;
    bool inSet = false;
    for (size_t i = 0; i < p.size(); i++) {
        char c = p[i];
        if (c == '%' && i + 1 < p.size()) {
            char d = p[++i];
            const char* cls = nullptr;
            switch (d) {
                case 'a': cls = "A-Za-z"; break;
                case 'd': cls = "0-9"; break;
                case 'l': cls = "a-z"; break;
                case 'u': cls = "A-Z"; break;
                case 's': cls = " \\t\\n\\r\\f\\v"; break;
                case 'w': cls = "A-Za-z0-9"; break;
                case 'x': cls = "0-9A-Fa-f"; break;
                case 'p': cls = "!-/:-@\\[-`{-~"; break;
            }
            if (cls) {
                if (!inSet) r += '[';
                r += cls;
                if (!inSet) r += ']';
            } else {
                r += '\\';
                r += d;
            }
            continue;
        }
        if (c == '[' && !inSet) {
            inSet = true;
            r += c;
            if (i + 1 < p.size() && p[i + 1] == '^') { r += '^'; i++; }
            continue;
        }
        if (c == ']' && inSet) { inSet = false; r += c; continue; }
        if (inSet) {
            if (c == '\\') r += "\\\\";
            else r += c;
            continue;
        }
        if (c == '-') {
            if (r.empty() || r.back() == '^') r += "\\-";
            else r += "*?";
            continue;
        }
        if (c == '\\' || c == '{' || c == '}' || c == '|' || c == '/') r += '\\';
        r += c;
    }
    return r;
}

static bool regexSearch(const std::string& pattern, const std::string& text) {
    static std::unordered_map<std::string, std::unique_ptr<std::regex>> cache;
    auto it = cache.find(pattern);
    if (it == cache.end()) {
        std::unique_ptr<std::regex> re;
        try {
            re = std::make_unique<std::regex>(pattern, std::regex::ECMAScript | std::regex::optimize);
        } catch (const std::regex_error&) {
        }
        it = cache.emplace(pattern, std::move(re)).first;
    }
    if (!it->second) return true;  // invalid expression: ignore the predicate
    return std::regex_search(text, *it->second);
}

static bool tsCapNode(const TSQueryMatch& m, int id, TSNode& n) {
    for (uint16_t k = 0; k < m.capture_count; k++)
        if (int(m.captures[k].index) == id) {
            n = m.captures[k].node;
            return true;
        }
    return false;
}

static std::string mimeLang(const std::string& t) {
    std::string s = asciiLower(t);
    if (s.find("json") != std::string::npos || s == "importmap") return "json";
    if (s.find("typescript") != std::string::npos) return "typescript";
    if (s.find("css") != std::string::npos) return "css";
    if (s.find("html") != std::string::npos || s.find("template") != std::string::npos) return "html";
    return "javascript";
}


TsDoc::~TsDoc() {
    reset(nullptr);
    if (parser) ts_parser_delete(parser);
    if (iparser) ts_parser_delete(iparser);
}

void TsDoc::clearInj() {
    for (auto& e : injCache)
        if (e.second) ts_tree_delete(e.second);
    injCache.clear();
}

void TsDoc::reset(TsLang* nl) {
    clearInj();
    if (tree) ts_tree_delete(tree);
    tree = nullptr;
    text.clear();
    ls.clear();
    L = nl;
    dirty = true;
    if (!parser) {
        parser = ts_parser_new();
        iparser = ts_parser_new();
    }
    if (L) ts_parser_set_language(parser, L->lang);
}

TSPoint TsDoc::pointIn(const std::vector<uint32_t>& lines, uint32_t b) {
    size_t r = size_t(std::upper_bound(lines.begin(), lines.end(), b) - lines.begin());
    if (r) r--;
    return {uint32_t(r), b - (lines.empty() ? 0 : lines[r])};
}

uint32_t TsDoc::byteAt(long row, long col) const {
    if (row < 0) return 0;
    if (size_t(row) >= ls.size()) return uint32_t(text.size());
    return uint32_t(std::min<long>(long(ls[row]) + std::max(0L, col), long(text.size())));
}

void TsDoc::sync(const std::vector<u32>& lines) {
    if (!L || (!dirty && tree)) return;
    dirty = false;
    auto t0 = Clock::now();
    std::string nt;
    std::vector<uint32_t> nls;
    nls.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) nt += '\n';
        nls.push_back(uint32_t(nt.size()));
        for (char32_t c : lines[i]) putUtf8(nt, c);
    }
    if (tree) {
        size_t m = std::min(text.size(), nt.size()), a = 0, b = 0;
        while (a < m && text[a] == nt[a]) a++;
        while (b < m - a && text[text.size() - 1 - b] == nt[nt.size() - 1 - b]) b++;
        if (a == text.size() && a == nt.size()) {
            ls = std::move(nls);
            return;
        }
        TSInputEdit e;
        e.start_byte = uint32_t(a);
        e.old_end_byte = uint32_t(text.size() - b);
        e.new_end_byte = uint32_t(nt.size() - b);
        e.start_point = pointIn(ls, e.start_byte);
        e.old_end_point = pointIn(ls, e.old_end_byte);
        e.new_end_point = pointIn(nls, e.new_end_byte);
        ts_tree_edit(tree, &e);
    }
    auto t1 = Clock::now();
    TSTree* t = ts_parser_parse_string(parser, tree, nt.data(), uint32_t(nt.size()));
    if (g_tsDebug)
        fprintf(g_tsDebug, "sync: Text+Diff %.1f ms, Parse %.1f ms\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                std::chrono::duration<double, std::milli>(Clock::now() - t1).count());
    if (tree) ts_tree_delete(tree);
    tree = t;
    text = std::move(nt);
    ls = std::move(nls);
    clearInj();
}

std::string TsDoc::nodeText(TSNode n) const {
    uint32_t a = ts_node_start_byte(n), b = ts_node_end_byte(n);
    b = std::min<uint32_t>(b, uint32_t(text.size()));
    a = std::min(a, b);
    return text.substr(a, b - a);
}

bool TsDoc::check(const TsQ& Q, const TSQueryMatch& m) const {
    for (auto& p : Q.preds[m.pattern_index]) {
        std::string nm = p.name;
        bool neg = nm.compare(0, 4, "not-") == 0;
        if (neg) nm = nm.substr(4);
        if (p.args.empty() || p.args[0].first < 0) continue;
        TSNode n;
        if (!tsCapNode(m, p.args[0].first, n)) continue;
        bool r;
        if (nm == "eq?") {
            std::string b;
            if (p.args.size() > 1) {
                TSNode n2;
                if (p.args[1].first < 0) b = p.args[1].second;
                else if (tsCapNode(m, p.args[1].first, n2)) b = nodeText(n2);
            }
            r = nodeText(n) == b;
        } else if (nm == "any-of?") {
            std::string a = nodeText(n);
            r = false;
            for (size_t k = 1; k < p.args.size() && !r; k++) r = p.args[k].second == a;
        } else if (nm == "match?" || nm == "vim-match?" || nm == "lua-match?") {
            if (p.args.size() < 2) continue;
            r = regexSearch(nm == "lua-match?" ? luaToRegex(p.args[1].second) : p.args[1].second, nodeText(n));
        } else if (nm == "contains?") {
            std::string a = nodeText(n);
            r = false;
            for (size_t k = 1; k < p.args.size() && !r; k++) r = a.find(p.args[k].second) != std::string::npos;
        } else if (nm == "kind-eq?") {
            r = false;
            for (size_t k = 1; k < p.args.size() && !r; k++) r = p.args[k].second == ts_node_type(n);
        } else if (nm == "has-ancestor?" || nm == "has-parent?") {
            r = false;
            for (TSNode q = ts_node_parent(n); !ts_node_is_null(q) && !r; q = ts_node_parent(q)) {
                for (size_t k = 1; k < p.args.size() && !r; k++) r = p.args[k].second == ts_node_type(q);
                if (nm == "has-parent?") break;
            }
        } else {
            continue;  // unknown predicate
        }
        if (r == neg) return false;
    }
    return true;
}

void TsDoc::collectInj(TsLang* PL, TSNode root, uint32_t vs, uint32_t ve, std::vector<Inj>& out) {
    const TsQ& Q = PL->inj;
    TSQueryCursor* qc = ts_query_cursor_new();
    ts_query_cursor_set_byte_range(qc, vs, ve);
    ts_query_cursor_exec(qc, Q.q, root);
    std::vector<std::pair<std::pair<uint32_t, TsLang*>, size_t>> combined;
    TSQueryMatch m;
    while (ts_query_cursor_next_match(qc, &m)) {
        if (!check(Q, m)) continue;
        std::string lang;
        bool comb = false, incl = false, off = false;
        long o[4] = {0, 0, 0, 0};
        TSNode content{};
        bool has = false;
        for (uint16_t k = 0; k < m.capture_count; k++) {
            const std::string& cn = Q.capNames[m.captures[k].index];
            if (cn == "injection.content") { content = m.captures[k].node; has = true; }
            else if (cn == "injection.language") lang = nodeText(m.captures[k].node);
        }
        for (auto& d : Q.dirs[m.pattern_index]) {
            std::vector<std::string> s;
            for (auto& a : d.args)
                if (a.first < 0) s.push_back(a.second);
            if (d.name == "set!") {
                if (!s.empty() && s[0] == "injection.combined") comb = true;
                else if (!s.empty() && s[0] == "injection.include-children") incl = true;
                else if (s.size() >= 2 && s[0] == "injection.language") lang = s[1];
                else if (!s.empty() && (s[0] == "injection.self" || s[0] == "injection.parent")) lang = PL->name;
            } else if ((d.name == "set-lang-from-info-string!" || d.name == "set-lang-from-mimetype!") &&
                       !d.args.empty() && d.args[0].first >= 0) {
                TSNode n;
                if (tsCapNode(m, d.args[0].first, n)) {
                    lang = nodeText(n);
                    if (d.name == "set-lang-from-mimetype!") lang = mimeLang(lang);
                }
            } else if (d.name == "offset!" && s.size() >= 4) {
                off = true;
                for (int k = 0; k < 4; k++) o[k] = atol(s[k].c_str());
            }
        }
        if (!has || lang.empty()) continue;
        TsLang* IL = tsFind(lang);
        if (!IL) continue;
        uint32_t a = ts_node_start_byte(content), b = ts_node_end_byte(content);
        if (off) {
            TSPoint pa = ts_node_start_point(content), pb = ts_node_end_point(content);
            a = byteAt(long(pa.row) + o[0], long(pa.column) + o[1]);
            b = byteAt(long(pb.row) + o[2], long(pb.column) + o[3]);
            if (b <= a) continue;
        }
        std::vector<TSRange> rs;
        if (incl || off) {
            rs.push_back(range(a, b));
        } else {  // named child nodes are not part of the embedded language (as in Neovim)
            uint32_t cur = a, nc = ts_node_named_child_count(content);
            for (uint32_t k = 0; k < nc; k++) {
                TSNode ch = ts_node_named_child(content, k);
                uint32_t ca = ts_node_start_byte(ch), cb = ts_node_end_byte(ch);
                if (ca > cur) rs.push_back(range(cur, std::min(ca, b)));
                cur = std::max(cur, cb);
            }
            if (cur < b) rs.push_back(range(cur, b));
        }
        if (rs.empty()) continue;
        if (comb) {
            auto key = std::make_pair(uint32_t(m.pattern_index), IL);
            bool merged = false;
            for (auto& c : combined)
                if (c.first == key) {
                    auto& v = out[c.second].ranges;
                    v.insert(v.end(), rs.begin(), rs.end());
                    merged = true;
                }
            if (merged) continue;
            combined.push_back({key, out.size()});
        }
        out.push_back({IL, std::move(rs)});
    }
    ts_query_cursor_delete(qc);
    for (auto& d : out) {
        std::sort(d.ranges.begin(), d.ranges.end(),
                  [](const TSRange& x, const TSRange& y) { return x.start_byte < y.start_byte; });
        std::vector<TSRange> merged;
        for (auto& r : d.ranges) {
            if (!merged.empty() && r.start_byte <= merged.back().end_byte) {
                if (r.end_byte > merged.back().end_byte) {
                    merged.back().end_byte = r.end_byte;
                    merged.back().end_point = r.end_point;
                }
            } else {
                merged.push_back(r);
            }
        }
        d.ranges = std::move(merged);
    }
}

TSTree* TsDoc::parseInj(const Inj& d) {
    std::string key = d.L->name;
    for (auto& r : d.ranges) key += ':' + std::to_string(r.start_byte) + '-' + std::to_string(r.end_byte);
    auto it = injCache.find(key);
    if (it != injCache.end()) return it->second;
    if (injCache.size() > 2000) clearInj();
    TSTree* t = nullptr;
    ts_parser_set_language(iparser, d.L->lang);
    if (ts_parser_set_included_ranges(iparser, d.ranges.data(), uint32_t(d.ranges.size())))
        t = ts_parser_parse_string(iparser, nullptr, text.data(), uint32_t(text.size()));
    ts_parser_set_included_ranges(iparser, nullptr, 0);
    injCache[key] = t;
    return t;
}

void TsDoc::paint(TsLang* PL, TSTree* t, uint32_t vs, uint32_t ve, std::vector<uint8_t>& out, int depth) {
    TSNode root = ts_tree_root_node(t);
    if (PL->hl.q) {
        struct Cap {
            uint32_t a, b, pat;
            uint8_t k;
        };
        std::vector<Cap> caps;
        TSQueryCursor* qc = ts_query_cursor_new();
        ts_query_cursor_set_byte_range(qc, vs, ve);
        ts_query_cursor_exec(qc, PL->hl.q, root);
        TSQueryMatch m;
        uint32_t ci;
        while (ts_query_cursor_next_capture(qc, &m, &ci)) {
            const TSQueryCapture& c = m.captures[ci];
            uint8_t k = PL->hl.capClass[c.index];
            if (g_tsDebug)
                fprintf(g_tsDebug, "%*s%s @%s [%u-%u] '%s'%s\n", depth * 2, "", PL->name.c_str(),
                        PL->hl.capNames[c.index].c_str(), ts_node_start_byte(c.node), ts_node_end_byte(c.node),
                        nodeText(c.node).substr(0, 30).c_str(), check(PL->hl, m) ? "" : " (predicate false)");
            if (k == 255 || !check(PL->hl, m)) continue;
            caps.push_back({ts_node_start_byte(c.node), ts_node_end_byte(c.node), m.pattern_index, k});
        }
        ts_query_cursor_delete(qc);
        // outer nodes first, inner nodes override; for the same node the later pattern wins
        std::stable_sort(caps.begin(), caps.end(), [](const Cap& x, const Cap& y) {
            if (x.a != y.a) return x.a < y.a;
            if (x.b - x.a != y.b - y.a) return x.b - x.a > y.b - y.a;
            return x.pat < y.pat;
        });
        for (auto& c : caps)
            for (uint32_t p = std::max(c.a, vs); p < std::min(c.b, ve); p++) out[p - vs] = c.k;
    }
    if (depth >= 4 || !PL->inj.q) return;
    std::vector<Inj> injs;
    collectInj(PL, root, vs, ve, injs);
    for (auto& d : injs) {
        if (g_tsDebug) {
            fprintf(g_tsDebug, "%*sINJECTION %s:", depth * 2, "", d.L->name.c_str());
            for (auto& r : d.ranges) fprintf(g_tsDebug, " [%u-%u]", r.start_byte, r.end_byte);
            fprintf(g_tsDebug, "\n");
        }
        // The embedded language is painted over the colors of the outer one (as in Neovim)
        if (TSTree* it = parseInj(d)) paint(d.L, it, vs, ve, out, depth + 1);
    }
}

void TsDoc::highlight(uint32_t vs, uint32_t ve, std::vector<uint8_t>& out) {
    out.assign(ve > vs ? ve - vs : 0, H_TEXT);
    if (tree && ve > vs) paint(L, tree, vs, ve, out, 0);
}

void tsCheckAll() {
    for (auto& g : TS_GRAMMARS) {
        auto t0 = Clock::now();
        TsLang* L = tsFind(g.name);
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        printf("%-16s highlights: %-7s %3u patterns, %d dropped   injections: %-3s indents: %-3s %d dropped %4.0f ms\n",
               g.name, L && L->hl.q ? "ok" : "MISSING", L && L->hl.q ? ts_query_pattern_count(L->hl.q) : 0,
               L ? L->hl.dropped : 0, L && L->inj.q ? "ok" : "-", L && L->ind.q ? "ok" : "-",
               L ? L->ind.dropped : 0, ms);
    }
}

// ---------------------------------------------------------------------------
// Indentation (a port of nvim-treesitter's lua/nvim-treesitter/indent.lua)
// ---------------------------------------------------------------------------

namespace {

struct IndMeta {
    bool immediate = false, startSameLine = false, avoidLast = false;
    std::string open, close;
    int increment = 1;
};

enum { IB, IE, IA, IBR, ID, II, IAU, IZ, INUM };

struct IndMaps {
    std::unordered_map<const void*, IndMeta> m[INUM];
    bool has(int k, TSNode n) const { return m[k].count(n.id) > 0; }
    const IndMeta& get(int k, TSNode n) const { return m[k].at(n.id); }
};

}  // namespace

bool TsDoc::indents(const std::vector<u32>& lines, int y1, int y2, int sw, int tabw, std::vector<int>& out) {
    if (!L || !L->ind.q || ts_query_pattern_count(L->ind.q) == 0) return false;
    sync(lines);
    if (!tree) return false;
    out.assign(size_t(std::max(0, y2 - y1 + 1)), -1);
    TSNode root = ts_tree_root_node(tree);
    const TsQ& Q = L->ind;
    // the captures of the whole tree around the range
    IndMaps maps;
    {
        static const char* const NAMES[INUM] = {"indent.begin", "indent.end", "indent.align", "indent.branch",
                                                "indent.dedent", "indent.ignore", "indent.auto", "indent.zero"};
        TSQueryCursor* qc = ts_query_cursor_new();
        uint32_t vs = y1 < int(ls.size()) ? ls[size_t(y1)] : uint32_t(text.size());
        uint32_t ve = y2 + 1 < int(ls.size()) ? ls[size_t(y2) + 1] : uint32_t(text.size());
        ts_query_cursor_set_byte_range(qc, vs, ve);
        ts_query_cursor_exec(qc, Q.q, root);
        TSQueryMatch m;
        while (ts_query_cursor_next_match(qc, &m)) {
            if (!check(Q, m)) continue;
            IndMeta meta;
            for (auto& d : Q.dirs[m.pattern_index]) {
                if (d.name != "set!") continue;
                std::vector<std::string> s;
                for (auto& a : d.args)
                    if (a.first < 0) s.push_back(a.second);
                if (s.empty()) continue;
                const std::string& k = s[0];
                std::string v = s.size() > 1 ? s[1] : "true";
                if (k == "indent.immediate") meta.immediate = true;
                else if (k == "indent.start_at_same_line") meta.startSameLine = true;
                else if (k == "indent.avoid_last_matching_next") meta.avoidLast = true;
                else if (k == "indent.open_delimiter") meta.open = v;
                else if (k == "indent.close_delimiter") meta.close = v;
                else if (k == "indent.increment") meta.increment = atoi(v.c_str());
            }
            for (uint16_t k = 0; k < m.capture_count; k++) {
                const std::string& cn = Q.capNames[m.captures[k].index];
                for (int i = 0; i < INUM; i++)
                    if (cn == NAMES[i]) maps.m[i][m.captures[k].node.id] = meta;
            }
        }
        ts_query_cursor_delete(qc);
    }

    // the line text and indentation helpers (the indentation of earlier lines may already be new)
    std::vector<int> newInd(lines.size(), -2);  // -2 = unchanged
    auto lineBytes = [&](int y) -> std::string { return toUtf8(lines[size_t(y)]); };
    auto leadChars = [&](int y) {
        const u32& l = lines[size_t(y)];
        size_t i = 0;
        while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
        return int(i);
    };
    auto widthOf = [&](const u32& s, int n) {
        int c = 0;
        for (int i = 0; i < n && i < int(s.size()); i++) c += s[size_t(i)] == '\t' ? tabw - c % tabw : 1;
        return c;
    };
    auto indentOf = [&](int y) {  // current indentation width of a line
        if (y >= 0 && y < int(newInd.size()) && newInd[size_t(y)] >= 0) return newInd[size_t(y)];
        return widthOf(lines[size_t(y)], leadChars(y));
    };
    // display column of the byte column bc in line y, after its new indentation
    auto colAfter = [&](int y, uint32_t bc) {
        const u32& l = lines[size_t(y)];
        uint32_t b = 0;
        int idx = 0;
        while (idx < int(l.size()) && b < bc) {
            char32_t ch = l[size_t(idx)];
            b += ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
            idx++;
        }
        int lead = leadChars(y);
        int col = idx <= lead ? indentOf(y) : indentOf(y) + int(strWidth(l.substr(size_t(lead), size_t(idx - lead))));
        return col;
    };
    auto nodeAt = [&](uint32_t row, uint32_t col) {
        TSPoint p{row, col};
        return ts_node_descendant_for_point_range(root, p, p);
    };
    auto firstNodeAt = [&](int y) {
        return nodeAt(uint32_t(y), uint32_t(leadChars(y)));  // the indentation is ASCII
    };
    // a child of the given type; last = nothing but white space and delimiters follow on its line
    auto findDelim = [&](TSNode n, const std::string& delim, TSNode& found, bool& last) {
        uint32_t cnt = ts_node_child_count(n);
        for (uint32_t i = 0; i < cnt; i++) {
            TSNode c = ts_node_child(n, i);
            if (delim != ts_node_type(c)) continue;
            TSPoint e = ts_node_end_point(c);
            std::string l = e.row < lines.size() ? lineBytes(int(e.row)) : std::string();
            std::string after = e.column < l.size() ? l.substr(e.column) : std::string();
            last = true;
            for (char ch : after)
                if (ch != ' ' && ch != '\t' && delim.find(ch) == std::string::npos) { last = false; break; }
            found = c;
            return true;
        }
        return false;
    };

    for (int y = y1; y <= y2 && y < int(lines.size()); y++) {
        const u32& lt = lines[size_t(y)];
        if (leadChars(y) == int(lt.size())) {  // an empty line
            out[size_t(y - y1)] = 0;
            newInd[size_t(y)] = 0;
            continue;
        }
        TSNode node = firstNodeAt(y);
        int indent = 0;
        int res = INT_MIN;
        std::unordered_map<uint32_t, bool> processedRow;
        if (maps.has(IZ, node)) res = 0;
        uint32_t lnum = uint32_t(y);
        while (res == INT_MIN && !ts_node_is_null(node)) {
            uint32_t srow = ts_node_start_point(node).row, erow = ts_node_end_point(node).row;
            bool begin = maps.has(IB, node), align = maps.has(IA, node);
            // keep the line as it is inside an @indent.auto / @indent.ignore node
            if (!begin && !align && maps.has(IAU, node) && srow < lnum && lnum <= erow) { res = -1; break; }
            if (!begin && maps.has(II, node) && srow < lnum && lnum <= erow) { res = -1; break; }
            bool processed = false;
            if (!processedRow[srow] &&
                ((maps.has(IBR, node) && srow == lnum) || (maps.has(ID, node) && srow != lnum))) {
                indent -= sw;
                processed = true;
            }
            bool shouldProcess = !processedRow[srow];
            bool inErr = false;
            if (shouldProcess) {
                TSNode p = ts_node_parent(node);
                inErr = !ts_node_is_null(p) && ts_node_has_error(p);
            }
            if (shouldProcess && begin) {
                const IndMeta& mt = maps.get(IB, node);
                if ((srow != erow || inErr || mt.immediate) && (srow != lnum || mt.startSameLine)) {
                    indent += sw;
                    processed = true;
                }
            }
            if (inErr && !align) {
                uint32_t cnt = ts_node_child_count(node);
                for (uint32_t i = 0; i < cnt; i++) {
                    TSNode ch = ts_node_child(node, i);
                    if (maps.has(IA, ch)) {
                        maps.m[IA][node.id] = maps.get(IA, ch);
                        align = true;
                        break;
                    }
                }
            }
            if (shouldProcess && align && (srow != erow || inErr) && srow != lnum) {
                IndMeta mt = maps.get(IA, node);
                TSNode od = node, cd = node;
                bool oLast = false, cLast = false, hasO = true, hasC = true;
                if (!mt.open.empty()) hasO = findDelim(node, mt.open, od, oLast);
                if (!mt.close.empty()) hasC = findDelim(node, mt.close, cd, cLast);
                if (hasO) {
                    TSPoint op = ts_node_start_point(od);
                    int cRow = hasC ? int(ts_node_start_point(cd).row) : -1;
                    bool absolute = false;
                    if (oLast) {
                        // hanging indent: the previous line ended with the opening delimiter
                        indent += sw;
                        if (cLast && cRow >= 0 && cRow < int(lnum)) indent = std::max(indent - sw, 0);
                    } else {
                        if (cLast && cRow >= 0 && int(op.row) != cRow && cRow < int(lnum)) {
                            indent = std::max(indent - sw, 0);
                        } else {
                            indent = colAfter(int(op.row), op.column) + mt.increment;
                            absolute = true;
                        }
                    }
                    if (cRow >= 0 && cRow != int(op.row) && cRow == int(lnum) && mt.avoidLast) {
                        if (indent <= indentOf(int(op.row)) + sw) indent += sw;
                    }
                    processed = true;
                    if (absolute) { res = indent; break; }
                }
            }
            processedRow[srow] = processedRow[srow] || processed;
            node = ts_node_parent(node);
        }
        if (res == INT_MIN) res = std::max(0, indent);
        out[size_t(y - y1)] = res;
        newInd[size_t(y)] = res;
    }
    return true;
}

bool TsDoc::insideString(uint32_t row, uint32_t col) const {
    if (!tree) return false;
    TSPoint p{row, col};
    for (TSNode n = ts_node_descendant_for_point_range(ts_tree_root_node(tree), p, p); !ts_node_is_null(n);
         n = ts_node_parent(n)) {
        std::string t = ts_node_type(n);
        if (t.find("string") != std::string::npos || t.find("heredoc") != std::string::npos ||
            t.find("template") != std::string::npos || t == "raw_text" || t.find("block_scalar") != std::string::npos)
            return true;
    }
    return false;
}
