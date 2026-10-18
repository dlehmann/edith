// Reformat Code: the built-in JSON formatter, choosing and running formatters, the
// configuration file, re-indenting by the syntax tree and the integration in Edith and Vim mode.

#include <cstdlib>
#include <sys/stat.h>

#include "apptest.h"
#include "format.h"
#include "process.h"
#include "i18n.h"
#include "languages.h"
#include "menus.h"
#include "treesitter.h"
#include "vimtest.h"

namespace {

// A clean environment: an empty configuration and a PATH with only the given fake programs
struct FormatEnv {
    std::string old;
    std::string dir, bin, cfg;
    explicit FormatEnv(bool keepSystemPath = false) {
        const char* p = getenv("PATH");
        old = p ? p : "";
        static int n = 0;
        dir = tempDir() + "/fmt" + std::to_string(n++);
        bin = dir + "/bin";
        cfg = dir + "/cfg";
        mkdir(dir.c_str(), 0700);
        mkdir(bin.c_str(), 0700);
        mkdir(cfg.c_str(), 0700);
        mkdir((cfg + "/edith").c_str(), 0700);
        setenv("XDG_CONFIG_HOME", cfg.c_str(), 1);
        S.expandtab = true;  // the expected texts use spaces (Edith's default is tabs)
        setenv("PATH", (keepSystemPath ? bin + ":" + old : bin).c_str(), 1);
    }
    ~FormatEnv() {
        setenv("PATH", old.c_str(), 1);
        unsetenv("XDG_CONFIG_HOME");
    }
    // a fake program: a shell script
    void tool(const std::string& name, const std::string& body) {
        std::string f = bin + "/" + name;
        writeFile(f, "#!/bin/sh\nPATH=/usr/bin:/bin\n" + body + "\n");
        chmod(f.c_str(), 0755);
    }
    void config(const std::string& text) { writeFile(cfg + "/edith/formatters", text); }
};

std::vector<u32> L(const std::string& text) {
    std::vector<u32> r;
    u32 t = U(text);
    size_t s = 0;
    for (;;) {
        size_t q = t.find('\n', s);
        r.push_back(t.substr(s, q == u32::npos ? u32::npos : q - s));
        if (q == u32::npos) break;
        s = q + 1;
    }
    return r;
}

std::string J(const std::vector<u32>& ls) {
    std::string r;
    for (size_t i = 0; i < ls.size(); i++) r += (i ? "\n" : "") + toUtf8(ls[i]);
    return r;
}

int langIndex(const char* name) {
    for (int i = 0; i < NLANGS; i++)
        if (std::string(LANGS[i].name) == name) return i;
    return -1;
}

// tree-sitter indentation of all lines of a text
std::string tsIndent(const char* tslang, const std::string& text, int sw = 4) {
    TsDoc d;
    d.reset(tsFind(tslang));
    std::vector<u32> ls = L(text);
    std::vector<int> w;
    if (!d.indents(ls, 0, int(ls.size()) - 1, sw, 4, w)) return "<no indents>";
    std::string r;
    for (size_t i = 0; i < ls.size(); i++) {
        u32 l = ls[i];
        size_t k = 0;
        while (k < l.size() && (l[k] == ' ' || l[k] == '\t')) k++;
        std::string body = toUtf8(l.substr(k));
        if (i) r += "\n";
        if (body.empty()) continue;
        r += w[i] < 0 ? toUtf8(l) : std::string(size_t(w[i]), ' ') + body;
    }
    return r;
}

}  // namespace

// ---- JSON ----

TEST(format_json_builtin) {
    std::vector<u32> out;
    u32 err;
    CHECK(formatJson(L("{\"a\":1,\"b\":[true,null,{\"c\":\"x, y\"}],\"d\":{}, \"e\":[]}"), 2, false, out, err));
    CHECK_EQ(J(out), "{\n  \"a\": 1,\n  \"b\": [\n    true,\n    null,\n    {\n      \"c\": \"x, y\"\n    }\n  ],\n"
                     "  \"d\": {},\n  \"e\": []\n}\n");
    CHECK(formatJson(L("[1.50e3, -2]"), 4, true, out, err));
    CHECK_EQ(J(out), "[\n\t1.50e3,\n\t-2\n]\n");  // numbers keep their spelling
    CHECK(formatJson(L("\"a\\\"b\""), 4, false, out, err));
    CHECK_EQ(J(out), "\"a\\\"b\"\n");
    CHECK(formatJson(L("   "), 4, false, out, err));  // empty stays empty
    CHECK_EQ(J(out), "   ");
    CHECK(formatJson(L("{\"ä\": \"日本\"}"), 4, false, out, err));
    CHECK_EQ(J(out), "{\n    \"ä\": \"日本\"\n}\n");
}

TEST(format_json_errors) {
    std::vector<u32> out;
    u32 err;
    CHECK(!formatJson(L("{\"a\": 1,}"), 2, false, out, err));
    CHECK(toUtf8(err).find("trailing comma") != std::string::npos);
    CHECK(!formatJson(L("{\"a\"\n 1}"), 2, false, out, err));
    CHECK_EQ(toUtf8(err), "Invalid JSON in line 2: ':' is expected");
    CHECK(!formatJson(L("[1] 2"), 2, false, out, err));
    CHECK(!formatJson(L("{a: 1}"), 2, false, out, err));
    CHECK(!formatJson(L("[nope]"), 2, false, out, err));
    CHECK(!formatJson(L("// c\n{}"), 2, false, out, err));
    CHECK(toUtf8(err).find("comments") != std::string::npos);
    CHECK(!formatJson(L("[\"open"), 2, false, out, err));
    g_uiLang = UiLang::German;
    CHECK(!formatJson(L("[1,]"), 2, false, out, err));
    CHECK_EQ(toUtf8(err), "Ungültiges JSON in Zeile 1: ein Komma am Ende ist nicht erlaubt");
    g_uiLang = UiLang::English;
}

// ---- choosing formatters ----

TEST(format_find_standard_tools) {
    FormatEnv env;
    std::string sugg;
    FormatTool t = findFormatter("Python", "/x/a.py", 4, true, &sugg);
    CHECK(t.command.empty());
    CHECK_EQ(sugg, "ruff");
    env.tool("black", "cat");
    t = findFormatter("Python", "/x/a.py", 4, true, &sugg);
    CHECK_EQ(t.program, "black");
    CHECK(t.command.find("--stdin-filename '/x/a.py'") != std::string::npos);
    env.tool("ruff", "cat");
    t = findFormatter("Python", "/x/a.py", 4, true);
    CHECK_EQ(t.program, "ruff");  // the first one of the list wins
    env.tool("clang-format", "cat");
    t = findFormatter("C", "/nowhere/a.c", 4, true);
    CHECK_EQ(t.program, "clang-format");
    CHECK(t.command.find("IndentWidth: 4") != std::string::npos);  // without .clang-format: the editor's settings
    CHECK(t.command.find("UseTab: Never") != std::string::npos);
    t = findFormatter("C", "/nowhere/a.c", 8, false);
    CHECK(t.command.find("UseTab: ForIndentation") != std::string::npos);
    writeFile(env.dir + "/.clang-format", "BasedOnStyle: Google\n");
    mkdir((env.dir + "/sub").c_str(), 0700);
    t = findFormatter("C++", env.dir + "/sub/b.cpp", 4, true);
    CHECK(t.command.find("--style=file") != std::string::npos);  // the project's style
    t = findFormatter("JSON", "", 4, true);
    CHECK(t.builtin);
    t = findFormatter("BASIC", "", 4, true, &sugg);
    CHECK(t.command.empty() && !t.builtin);
    CHECK(sugg.empty());
    env.tool("xmllint", "cat");
    t = findFormatter("XML", "", 2, true);
    CHECK_EQ(t.program, "xmllint");
    CHECK(t.command.find("XMLLINT_INDENT='  '") == 0);
    t = findFormatter("JavaScript", "", 4, true);  // no prettier: clang-format
    CHECK_EQ(t.program, "clang-format");
    t = findFormatter("C", "", 4, true);  // an unnamed file: a name with the right extension
    CHECK(t.command.find("untitled.c'") != std::string::npos);
}

TEST(format_config_file) {
    FormatEnv env;
    env.tool("clang-format", "cat");
    env.config(
        "# comment\n python =  my-fmt --x {file}  \nrust = none\nJSON=builtin\nC = clang-format --style=Google\n");
    FormatTool t = findFormatter("Python", "/p/a.py", 4, true);
    CHECK_EQ(t.program, "my-fmt");
    CHECK_EQ(t.command, "my-fmt --x '/p/a.py'");
    t = findFormatter("Rust", "", 4, true);
    CHECK(t.command.empty());
    CHECK_EQ(t.program, "none");
    t = findFormatter("JSON", "", 4, true);
    CHECK(t.builtin);
    t = findFormatter("C", "", 4, true);
    CHECK_EQ(t.command, "clang-format --style=Google");
    CHECK(formatConfigPath() == env.cfg + "/edith/formatters");
}

// ---- re-indenting by the syntax tree ----

TEST(format_ts_indent_c) {
    CHECK_EQ(tsIndent("c", "int main(void) {\nif (x) {\ny();\n} else {\nz();\n}\nreturn 0;\n}"),
             "int main(void) {\n    if (x) {\n        y();\n    } else {\n        z();\n    }\n    return 0;\n}");
    CHECK_EQ(tsIndent("c", "struct s {\nint a;\nint b;\n};"), "struct s {\n    int a;\n    int b;\n};");
    CHECK_EQ(tsIndent("c", "void f() {\nswitch (x) {\ncase 1:\nbreak;\n}\n}"),
             "void f() {\n    switch (x) {\n        case 1:\n            break;\n    }\n}");
    // a comment block keeps its own indentation
    CHECK_EQ(tsIndent("c", "void f() {\n/* a\n      b */\n}"), "void f() {\n    /* a\n      b */\n}");
    CHECK_EQ(tsIndent("c", "void f() {\n  x();\n}", 2), "void f() {\n  x();\n}");
}

TEST(format_ts_indent_other_languages) {
    CHECK_EQ(tsIndent("lua", "function f()\nif a then\nb()\nend\nend"),
             "function f()\n    if a then\n        b()\n    end\nend");
    CHECK_EQ(tsIndent("javascript", "const o = {\na: 1,\nb: [\n2,\n],\n};"),
             "const o = {\n    a: 1,\n    b: [\n        2,\n    ],\n};");
    CHECK_EQ(tsIndent("rust", "fn main() {\nlet v = vec![\n1,\n];\n}"),
             "fn main() {\n    let v = vec![\n        1,\n    ];\n}");
    CHECK_EQ(tsIndent("python", "def f():\n    x = [\n1,\n]"), "def f():\n    x = [\n        1,\n    ]");
    CHECK_EQ(tsIndent("json", "{\n\"a\": [\n1\n]\n}"), "{\n    \"a\": [\n        1\n    ]\n}");
    CHECK_EQ(tsIndent("css", "a {\ncolor: red;\n}"), "a {\n    color: red;\n}");
    CHECK_EQ(tsIndent("go", "func f() {\nx := 1\n}"), "func f() {\n    x := 1\n}");
    CHECK_EQ(tsIndent("haskell", "x"), "<no indents>");  // no indents query
}

TEST(format_ts_indent_keeps_multiline_strings) {
    // the content of a multi-line string must not change
    CHECK_EQ(tsIndent("javascript", "function f() {\nconst s = `a\n   b`;\n}"),
             "function f() {\n    const s = `a\n   b`;\n}");
    CHECK_EQ(tsIndent("python", "def f():\n    s = \"\"\"x\n  y\"\"\""), "def f():\n    s = \"\"\"x\n  y\"\"\"");
}

TEST(format_brace_fallback) {
    std::vector<int> w = braceIndents(L("a {\nb(\nc\n)\n}\n\nd"), 0, 6, 4, 4);
    CHECK_EQ(w, std::vector<int>({0, 4, 8, 4, 0, -1, 0}));
}

TEST(format_indent_significant_languages) {
    CHECK(indentSignificant("Python"));
    CHECK(indentSignificant("YAML"));
    CHECK(indentSignificant("Makefile"));
    CHECK(indentSignificant("Markdown"));
    CHECK(!indentSignificant("C"));
    CHECK(!indentSignificant("JSON"));
}

// ---- in the application ----

TEST(format_app_with_formatter_and_undo) {
    FormatEnv env;
    env.tool("upper", "tr a-z A-Z");
    env.config("C = upper\n");
    AppT t("int x;\nchar c;");
    t.a().setLang(langIndex("C"));
    t.keys("\x1b[19~");  // F8
    CHECK_EQ(t.text(), "INT X;\nCHAR C;");
    CHECK(t.ed().modified);
    CHECK(t.row(24).find("Formatted with upper") != std::string::npos);
    t.keys(ctrl('z'));  // one undo step
    CHECK_EQ(t.text(), "int x;\nchar c;");
    t.keys("x");  // the message disappears with the next key
    CHECK(t.row(24).find("Formatted") == std::string::npos);
}

TEST(format_app_formatter_error) {
    FormatEnv env;
    env.tool("broken", "echo 'line 1: syntax error' >&2; exit 2");
    env.tool("silent", "cat >/dev/null");
    env.config("C = broken\nC++ = silent\nJava = missing-tool\n");
    AppT t("int x;");
    t.a().setLang(langIndex("C"));
    sendKeys("\r");  // closes the message box
    t.a().reformatCode();
    CHECK_EQ(t.text(), "int x;");  // unchanged
    CHECK(!t.ed().modified);
    t.a().setLang(langIndex("C++"));
    sendKeys("\r");
    t.a().reformatCode();  // no output for a non-empty file is an error
    CHECK_EQ(t.text(), "int x;");
    t.a().setLang(langIndex("Java"));
    sendKeys("\r");
    t.a().reformatCode();  // not installed: exit status 127
    CHECK_EQ(t.text(), "int x;");
}

TEST(format_app_builtin_fallbacks) {
    FormatEnv env;  // no formatters at all
    {
        AppT t("int f() {\nreturn 1;   \n}\nchar *s = \"a  \";");
        t.a().setLang(langIndex("C"));
        t.a().reformatCode();
        t.draw();
        CHECK_EQ(t.text(), "int f() {\n    return 1;\n}\nchar *s = \"a  \";");
        CHECK(t.row(24).find("Re-indented (full formatting with clang-format)") != std::string::npos);
    }
    {
        S.expandtab = false;
        AppT t("int f() {\nreturn 1;\n}");
        t.a().setLang(langIndex("C"));
        t.a().reformatCode();
        CHECK_EQ(t.text(), "int f() {\n\treturn 1;\n}");  // tabs when 'Tabs as Spaces' is off
        S.expandtab = true;
    }
    {
        AppT t("def f():\n  return 1   \n");
        t.a().setLang(langIndex("Python"));
        t.a().reformatCode();
        t.draw();
        CHECK_EQ(t.text(), "def f():\n  return 1\n");  // the indentation of Python is kept
        CHECK(t.row(24).find("Indentation kept (full formatting with ruff)") != std::string::npos);
    }
    {
        AppT t("line  \nnext");
        t.a().setLang(langIndex("Markdown"));
        t.a().reformatCode();
        CHECK_EQ(t.text(), "line  \nnext");  // two spaces are a line break in Markdown
    }
    {
        AppT t("{\"a\":[1,2]}");
        t.a().setLang(langIndex("JSON"));
        t.a().reformatCode();
        t.draw();
        CHECK_EQ(t.text(), "{\n    \"a\": [\n        1,\n        2\n    ]\n}\n");
        CHECK(t.row(24).find("built-in JSON") != std::string::npos);
        t.a().reformatCode();
        t.draw();
        CHECK(t.row(24).find("Already formatted") != std::string::npos);
        t.ed().lines = {U"{\"a\":}"};
        t.a().reformatCode();
        t.draw();
        CHECK(t.row(24).find("Invalid JSON") != std::string::npos);
        CHECK_EQ(int(t.cell(1, 24).bg), int(Col::ERRMSG.bg));
    }
    {
        AppT t("  text   ");  // plain text: only trailing white space
        t.a().reformatCode();
        CHECK_EQ(t.text(), "  text");
    }
}

TEST(format_app_cursor_is_kept) {
    FormatEnv env;
    AppT t("int f() {\nreturn 1;\n}");
    t.a().setLang(langIndex("C"));
    t.ed().cy = 1;
    t.ed().cx = 7;  // on "1"
    t.a().reformatCode();
    CHECK_EQ(t.cur(), Pos({1, 11}));
    CHECK_EQ(t.ed().lines[1][size_t(t.ed().cx)], char32_t('1'));
}

TEST(format_app_menu_german) {
    FormatEnv env;
    g_uiLang = UiLang::German;
    buildMenus();
    AppT t("int f() {\nreturn 1;\n}");
    t.a().setLang(langIndex("C"));
    t.keys(alt('b') + alt('f'));  // Bearbeiten > Code formatieren
    t.draw();
    CHECK_EQ(t.text(), "int f() {\n    return 1;\n}");
    CHECK(t.row(24).find("Neu eingerückt (vollständig mit clang-format)") != std::string::npos);
}

TEST(format_app_real_clang_format) {
    FormatEnv env(true);  // the system PATH
    FormatTool tool = findFormatter("C", "", 4, true);
    std::string o, e;
    int st;
    if (tool.program != "clang-format" || !runCommand(tool.command, "", "int x;", 10000, o, e, st) || st != 0)
        return;  // not installed or not working here
    AppT t("int main(){int x=1;if(x){return 0;}}");
    t.a().setLang(langIndex("C"));
    t.a().reformatCode();
    CHECK_EQ(t.text(), "int main() {\n    int x = 1;\n    if (x) {\n        return 0;\n    }\n}");
}

TEST(format_app_real_tool_failing) {
    // a real program that fails (like the rustup proxy without toolchain) shows its message
    FormatEnv env;
    env.tool("rustfmt", "echo 'error: no default toolchain' >&2; exit 1");
    AppT t("fn main(){}");
    t.a().setLang(langIndex("Rust"));
    sendKeys("\r");
    t.a().reformatCode();
    CHECK_EQ(t.text(), "fn main(){}");
    S.vim = true;
    t.a().reformatCode();  // Vim mode: in the status line
    t.draw();
    CHECK(t.row(24).find("error: no default toolchain") != std::string::npos);
}

TEST(format_app_real_rustfmt) {
    FormatEnv env(true);
    FormatTool tool = findFormatter("Rust", "", 4, true);
    std::string o, e;
    int st;
    if (tool.program != "rustfmt" || !runCommand(tool.command, "", "fn f() {}", 10000, o, e, st) || st != 0)
        return;  // e.g. only the rustup proxy without a toolchain
    AppT t("fn main(){let x=1;}");
    t.a().setLang(langIndex("Rust"));
    t.a().reformatCode();
    CHECK_EQ(t.text(), "fn main() {\n    let x = 1;\n}\n");
}

// ---- Vim mode ----

TEST(format_vim_equal_uses_syntax_tree) {
    FormatEnv env;
    AppT t("int f() {\nif (a) {\nb();\n}\n}");
    S.vim = true;
    t.a().setLang(langIndex("C"));
    t.keys("gg=G");
    CHECK_EQ(t.text(), "int f() {\n    if (a) {\n        b();\n    }\n}");
    t.keys("u");
    CHECK_EQ(t.text(), "int f() {\nif (a) {\nb();\n}\n}");
    t.keys("3G==");  // a single line
    CHECK_EQ(toUtf8(t.ed().lines[2]), "        b();");
}

TEST(format_vim_format_command) {
    FormatEnv env;
    env.tool("upper", "tr a-z A-Z");
    env.config("C = upper\n");
    AppT t("abc");
    S.vim = true;
    t.a().setLang(langIndex("C"));
    t.keys(":Format\r");
    CHECK_EQ(t.text(), "ABC");
    CHECK(t.row(24).find("Formatted with upper") != std::string::npos);
    t.keys("u");
    CHECK_EQ(t.text(), "abc");
    t.keys("i\x1b[19~");  // F8 in Insert mode ends it first
    CHECK_EQ(t.text(), "ABC");
    CHECK_EQ(t.a().vim.mode, VM_NORMAL);
}

TEST(format_vim_equal_without_application) {
    // the engine alone keeps its brace rules
    VT t("‸{\nx;\n}");
    t.k(":set sw=2 et<CR>=G");
    CHECK_EQ(t.text(), "{\n  x;\n}");
}
