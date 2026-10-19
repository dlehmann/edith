// Helpers and infrastructure: UTF-8, paths, base64, UI language and translations,
// menus, language detection, the fallback lexer and tree-sitter highlighting.

#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <set>

#include "i18n.h"
#include "languages.h"
#include "lexer.h"
#include "menus.h"
#include "paths.h"
#include "terminal.h"
#include "test.h"
#include "treesitter.h"
#include "version.h"

// ---- UTF-8 and characters ----

TEST(utf8_roundtrip) {
    u32 s = U"aä€😀";
    std::string u = toUtf8(s);
    CHECK_EQ(u, "a\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80");
    u32 back;
    CHECK(fromUtf8(u, back));
    CHECK_EQ(back, s);
    CHECK_EQ(U(u), s);
}

TEST(utf8_invalid_input) {
    u32 out;
    CHECK(!fromUtf8("\xFF", out));
    CHECK(!fromUtf8("\xC3", out));          // truncated
    CHECK(!fromUtf8("\xC0\x80", out));      // overlong
    CHECK(!fromUtf8("\xED\xA0\x80", out));  // surrogate
    CHECK(!fromUtf8("\xF4\x90\x80\x80", out));  // > U+10FFFF
    CHECK_EQ(U("gr\xFC\xDF"), u32(U"grüß"));     // Latin-1 fallback
    // code points without a UTF-8 form are written as U+FFFD, so the output stays valid UTF-8
    CHECK_EQ(toUtf8(u32(1, char32_t(0xD800))), "\xEF\xBF\xBD");
    CHECK_EQ(toUtf8(u32(1, char32_t(0x110000))), "\xEF\xBF\xBD");
    CHECK_EQ(toUtf8(u32(1, char32_t(0xFFFFFFFF))), "\xEF\xBF\xBD");
    CHECK_EQ(toUtf8(u32(1, char32_t(0x10FFFF))), "\xF4\x8F\xBF\xBF");
}

TEST(utf8_widths) {
    CHECK_EQ(cw('a'), 1);
    CHECK_EQ(cw(U'日'), 2);
    CHECK_EQ(cw(U'😀'), 2);
    CHECK_EQ(cw(0x301), 0);
    CHECK_EQ(cw(U'ä'), 1);
    CHECK_EQ(strWidth(U"a日b"), 4);
}

TEST(utf8_fold_and_word) {
    CHECK_EQ(fold('A'), char32_t('a'));
    CHECK_EQ(fold(U'Ä'), char32_t(U'ä'));
    CHECK_EQ(fold(U'Ω'), char32_t(U'ω'));
    CHECK_EQ(fold(U'Ж'), char32_t(U'ж'));
    CHECK_EQ(fold('1'), char32_t('1'));
    CHECK(isWord('a') && isWord('_') && isWord('9') && isWord(U'ü') && isWord(U'日'));
    CHECK(!isWord(' ') && !isWord('-') && !isWord(U'×') && !isWord(U'—'));
    CHECK_EQ(toUtf8(fmtU("%05d:%03d", 7, 12)), "00007:012");
}

// ---- paths ----

TEST(paths_helpers) {
    CHECK_EQ(normPath("/a/./b/../c//d/"), "/a/c/d");
    CHECK_EQ(normPath("/.."), "/");
    CHECK_EQ(baseOf("/x/y.txt"), "y.txt");
    CHECK_EQ(baseOf("y.txt"), "y.txt");
    CHECK_EQ(dirOf("/x/y.txt"), "/x");
    CHECK_EQ(dirOf("/y.txt"), "/");
    CHECK(fileExists(tempDir()));
    CHECK(!fileExists(tempDir() + "/nope"));
    setenv("HOME", "/home/test", 1);
    CHECK_EQ(absPath("~/f"), "/home/test/f");
    CHECK_EQ(absPath("~"), "/home/test");
    CHECK(absPath("~user/f").find("/~user/f") != std::string::npos);  // other users' homes are not expanded
    CHECK_EQ(absPath("/a/../b"), "/b");
    CHECK(absPath("rel").find("/rel") != std::string::npos);
    CHECK(!getCwd().empty());
}

TEST(paths_base64) {
    CHECK_EQ(base64(""), "");
    CHECK_EQ(base64("f"), "Zg==");
    CHECK_EQ(base64("fo"), "Zm8=");
    CHECK_EQ(base64("foo"), "Zm9v");
    CHECK_EQ(base64("foobar"), "Zm9vYmFy");
}

// ---- UI language ----

TEST(i18n_parse_and_detect) {
    UiLang l;
    CHECK(parseUiLanguage("de", l) && l == UiLang::German);
    CHECK(parseUiLanguage("de_DE.UTF-8", l) && l == UiLang::German);
    CHECK(parseUiLanguage("EN", l) && l == UiLang::English);
    CHECK(parseUiLanguage("fr_CA", l) && l == UiLang::French);
    CHECK(parseUiLanguage("es", l) && l == UiLang::Spanish);
    CHECK(parseUiLanguage("it_IT@euro", l) && l == UiLang::Italian);
    CHECK(parseUiLanguage("hu", l) && l == UiLang::Hungarian);
    CHECK(parseUiLanguage("pl_PL.UTF-8", l) && l == UiLang::Polish);
    CHECK(parseUiLanguage("tlh", l) && l == UiLang::Klingon);
    CHECK(!parseUiLanguage("pt", l));
    CHECK(!parseUiLanguage("deu", l));
    CHECK(!parseUiLanguage("t", l));
    CHECK(!parseUiLanguage("", l));
    unsetenv("LANGUAGE");
    unsetenv("LC_ALL");
    unsetenv("LC_MESSAGES");
    setenv("LANG", "de_AT.UTF-8", 1);
    CHECK(detectUiLanguage() == UiLang::German);
    setenv("LANG", "pt_BR.UTF-8", 1);
    CHECK(detectUiLanguage() == UiLang::English);  // English is the fallback
    setenv("LANG", "hu_HU.UTF-8", 1);
    CHECK(detectUiLanguage() == UiLang::Hungarian);
    setenv("LC_ALL", "de_DE", 1);
    CHECK(detectUiLanguage() == UiLang::German);
    setenv("LANGUAGE", "it:de", 1);
    CHECK(detectUiLanguage() == UiLang::Italian);
    setenv("LC_ALL", "C", 1);
    setenv("LANGUAGE", "de", 1);  // LANGUAGE is ignored for the C locale
    CHECK(detectUiLanguage() == UiLang::English);
    unsetenv("LANGUAGE");
    unsetenv("LC_ALL");
    CHECK_EQ(uiLanguages().size(), size_t(8));
}

TEST(i18n_translate) {
    g_uiLang = UiLang::English;
    CHECK_EQ(u32(tr(U"&File")), u32(U"&File"));
    g_uiLang = UiLang::German;
    CHECK_EQ(u32(tr(U"&File")), u32(U"&Datei"));
    CHECK_EQ(trs("Read error."), "Lesefehler.");
    CHECK_EQ(u32(tr(U"no translation here")), u32(U"no translation here"));
    CHECK_EQ(errnoText(ENOENT), "Datei oder Verzeichnis nicht gefunden.");
    g_uiLang = UiLang::French;
    CHECK(errnoText(EACCES) != "Permission denied.");
    g_uiLang = UiLang::English;
    CHECK_EQ(errnoText(ENOENT), "No such file or directory.");
    CHECK(!errnoText(EBADF).empty());  // other errors: the system's text
}

// The string literals passed to tr() / trs() in the sources (adjacent literals joined)
static std::set<std::string> uiStrings(const std::string& src) {
    std::set<std::string> out;
    for (const char* fn : {"tr(", "trs("}) {
        size_t p = 0;
        while ((p = src.find(fn, p)) != std::string::npos) {
            if (p > 0 && (isalnum((unsigned char)src[p - 1]) || src[p - 1] == '_')) { p++; continue; }
            size_t i = p + strlen(fn);
            std::string lit;
            bool any = false;
            for (;;) {
                while (i < src.size() && isspace((unsigned char)src[i])) i++;
                if (i < src.size() && src[i] == 'U') i++;
                if (i >= src.size() || src[i] != '"') break;
                i++;
                while (i < src.size() && src[i] != '"') {
                    if (src[i] == '\\') lit += src[i++];
                    lit += src[i++];
                }
                i++;
                any = true;
            }
            if (any) out.insert(lit);
            p = i;
        }
    }
    return out;
}

// The first strings of the entries of a table in a language file
static std::set<std::string> tableKeys(const std::string& src) {
    std::set<std::string> keys;
    size_t p = src.find("T[][2] = {");
    if (p == std::string::npos) return keys;
    size_t end = src.find("\n};", p);
    std::string body = src.substr(p, end - p);
    size_t i = 0;
    while ((i = body.find("{U\"", i)) != std::string::npos) {
        i += 2;
        std::string lit;
        for (;;) {
            while (i < body.size() && isspace((unsigned char)body[i])) i++;
            if (i < body.size() && body[i] == 'U') i++;
            if (i >= body.size() || body[i] != '"') break;
            i++;
            while (i < body.size() && body[i] != '"') {
                if (body[i] == '\\') lit += body[i++];
                lit += body[i++];
            }
            i++;
        }
        keys.insert(lit);
    }
    return keys;
}

TEST(i18n_every_ui_string_is_translated_in_every_language) {
    // the strings used in the sources
    std::set<std::string> used;
    DIR* d = opendir("src");
    CHECK(d != nullptr);
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() < 4 || n.substr(n.size() - 4) != ".cpp" || n.compare(0, 5, "i18n_") == 0) continue;
        for (auto& s : uiStrings(readFile("src/" + n))) used.insert(s);
    }
    closedir(d);
    CHECK(used.size() > 250);
    for (auto& l : uiLanguages()) {
        if (l.id == UiLang::English) continue;
        std::string file = std::string("src/i18n_") + l.code + ".cpp";
        std::set<std::string> keys = tableKeys(readFile(file));
        std::vector<std::string> missing, unused;
        for (auto& s : used)
            if (!keys.count(s)) missing.push_back(s);
        for (auto& k : keys)
            if (!used.count(k)) unused.push_back(k);
        if (!missing.empty())
            testFail(__FILE__, __LINE__, file + ": " + std::to_string(missing.size()) + " missing, e.g. " + missing[0]);
        if (!unused.empty()) testFail(__FILE__, __LINE__, file + ": unused translation " + unused[0]);
        g_checks++;
    }
}

// The printf placeholders of a string ("%d", "%s", "%*d" ...)
static std::string placeholders(const u32& s) {
    std::string r;
    for (size_t i = 0; i + 1 < s.size(); i++) {
        if (s[i] != '%') continue;
        if (s[i + 1] == '%') { i++; continue; }
        if (i > 0 && s[i - 1] == '\\') continue;  // "\%" in a Vim pattern message
        size_t j = i + 1;
        while (j < s.size() && (s[j] == '*' || isdigit(int(s[j] < 128 ? s[j] : 'x')))) j++;
        if (j < s.size()) r += toUtf8(s.substr(i, j - i + 1)) + " ";
        i = j;
    }
    return r;
}

TEST(i18n_translations_are_consistent) {
    for (auto& l : uiLanguages()) {
        const LangTable* t = langTable(l.id);
        if (!t) continue;
        CHECK(t->count > 250);
        for (size_t i = 0; i < t->count; i++) {
            u32 en = t->entries[i][0], tr = t->entries[i][1];
            std::string where = std::string(l.code) + ": " + toUtf8(en);
            if (tr.empty()) testFail(__FILE__, __LINE__, where + " is empty");
            if (placeholders(en) != placeholders(tr))
                testFail(__FILE__, __LINE__, where + ": placeholders differ: " + toUtf8(tr));
            bool ampEn = en.find('&') != u32::npos, ampTr = tr.find('&') != u32::npos;
            bool menuLabel = ampEn && en.find(' ') != 0 && en.size() < 40 && en.find(U"&&") == u32::npos;
            if (menuLabel && !ampTr) testFail(__FILE__, __LINE__, where + ": the hotkey marker & is missing");
            size_t nlEn = std::count(en.begin(), en.end(), U'\n'), nlTr = std::count(tr.begin(), tr.end(), U'\n');
            if ((nlEn == 0) != (nlTr == 0)) testFail(__FILE__, __LINE__, where + ": line breaks differ");
            g_checks++;
        }
    }
}

TEST(i18n_help_and_status_in_every_language) {
    for (auto& l : uiLanguages()) {
        g_uiLang = l.id;
        buildMenus();
        size_t plain = helpText().size();
        g_macKeys = true;  // with the macOS sections
        const std::vector<u32>& h = helpText();
        CHECK(h.size() > plain + 10);
        CHECK(h.size() > 60);
        bool mac = false;
        for (auto& line : h) {
            if (strWidth(line) > 70)
                testFail(__FILE__, __LINE__, std::string(l.code) + ": help line too long: " + toUtf8(line));
            mac |= line == U"MACOS";
        }
        CHECK(mac);
        g_macKeys = false;  // (resetGlobals() sets it for the next test)
        u32 hint = statusHint();
        CHECK(hint.compare(0, 4, U"<F1=") == 0 && hint.find('>') != u32::npos);
        CHECK(strWidth(hint) < 60);
    }
    g_uiLang = UiLang::English;
    buildMenus();
}

TEST(i18n_menu_hotkeys_unique_in_every_language) {
    for (auto& li : uiLanguages()) {
        UiLang l = li.id;
        g_uiLang = l;
        buildMenus();
        std::set<char32_t> bar;
        for (auto& m : MENUS) {
            CHECK(hotkeyOf(m.title) != 0);
            if (!bar.insert(hotkeyOf(m.title)).second)
                testFail(__FILE__, __LINE__,
                         std::string(li.code) + ": duplicate hotkey in the menu bar: " + toUtf8(stripAmp(m.title)));
            std::set<char32_t> items;
            for (auto& it : m.items) {
                if (!it.label) continue;
                char32_t h = hotkeyOf(it.label);
                CHECK(h != 0);
                if (!items.insert(h).second)
                    testFail(__FILE__, __LINE__,
                             std::string(li.code) + ": duplicate hotkey in " + toUtf8(stripAmp(m.title)) + ": " +
                                 toUtf8(stripAmp(it.label)));
            }
        }
    }
    g_uiLang = UiLang::English;
    buildMenus();
}

TEST(i18n_menus_and_help) {
    g_uiLang = UiLang::German;
    buildMenus();
    CHECK_EQ(toUtf8(stripAmp(MENUS[0].title)), "Datei");
    bool vim = false;
    for (auto& it : MENUS[3].items)
        if (it.act == A_VIM) vim = toUtf8(stripAmp(it.label)) == "Vim-Modus";
    CHECK(vim);
    bool vimHelp = false;
    for (auto& l : helpText()) vimHelp |= toUtf8(l) == "VIM-MODUS";
    CHECK(vimHelp);
    CHECK(toUtf8(statusHint()).find("Hilfe") != std::string::npos);
    g_uiLang = UiLang::English;
    buildMenus();
    vimHelp = false;
    for (auto& l : helpText()) vimHelp |= toUtf8(l) == "VIM MODE";
    CHECK(vimHelp);
    // the section headings of the help are upper case (e.g. "EDIT" for the Edit menu)
    for (auto& l : helpText())
        if (!l.empty() && l[0] != ' ')
            for (char32_t c : l) CHECK(!(c >= 'a' && c <= 'z'));
    CHECK_EQ(hotkeyOf(U"Save &As..."), char32_t('a'));
    CHECK_EQ(toUtf8(stripAmp(U"Save &As...")), "Save As...");
}

// ---- languages and highlighting ----

static std::string langName(int i) { return i < 0 ? "-" : LANGS[i].name; }

TEST(languages_detection) {
    CHECK_EQ(langName(detectLang("/x/main.c", u32())), "C");
    CHECK_EQ(langName(detectLang("a.py", u32())), "Python");
    CHECK_EQ(langName(detectLang("README.MD", u32())), "Markdown");
    CHECK_EQ(langName(detectLang("Makefile", u32())), "Makefile");
    CHECK_EQ(langName(detectLang("Dockerfile.dev", u32())), "Dockerfile");
    CHECK_EQ(langName(detectLang("run", U"#!/usr/bin/env python3")), "Python");
    CHECK_EQ(langName(detectLang("run", U"#!/bin/bash -e")), "Shell");
    CHECK_EQ(langName(detectLang("x.unknownext", u32())), "-");
    CHECK_EQ(langName(detectLang("", u32())), "-");
    CHECK(NLANGS > 50);
}

TEST(languages_words) {
    auto w = splitWords("a  bb c");
    CHECK_EQ(w.size(), size_t(3));
    CHECK_EQ(toUtf8(lower(U"AbÄ")), "abä");
}

TEST(lexer_fallback_highlighting) {
    int c = -1;
    for (int i = 0; i < NLANGS; i++)
        if (std::string(LANGS[i].name) == "C") c = i;
    CHECK(c >= 0);
    u32 l = U"return \"s\"; // c";
    std::vector<uint8_t> cls(l.size());
    int st = highlightLine(c, l, 0, cls.data());
    CHECK_EQ(int(cls[0]), int(H_KEYWORD));
    CHECK_EQ(int(cls[8]), int(H_STRING));
    CHECK_EQ(int(cls[l.size() - 1]), int(H_COMMENT));
    CHECK_EQ(st, 0);
    u32 b = U"/* open";
    std::vector<uint8_t> c2(b.size());
    int st2 = highlightLine(c, b, 0, c2.data());
    CHECK(st2 != 0);  // a block comment continues on the next line
    u32 n = U"x */ 42";
    std::vector<uint8_t> c3(n.size());
    highlightLine(c, n, st2, c3.data());
    CHECK_EQ(int(c3[0]), int(H_COMMENT));
    CHECK_EQ(int(c3[5]), int(H_NUMBER));
}

TEST(treesitter_mapping_and_highlighting) {
    int c = -1;
    for (int i = 0; i < NLANGS; i++)
        if (std::string(LANGS[i].name) == "C") c = i;
    CHECK_EQ(std::string(tsNameFor(c)), "c");
    CHECK(tsNameFor(-1) == nullptr);
    TsLang* L = tsFind("c");
    CHECK(L && L->hl.q);
    CHECK(tsFind("py") == tsFind("python"));
    CHECK(tsFind("no-such-language") == nullptr);
    TsDoc d;
    d.reset(L);
    std::vector<u32> lines{U"int main(void) {", U"  return 42; // x", U"}"};
    d.sync(lines);
    std::vector<uint8_t> out;
    d.highlight(0, uint32_t(d.text.size()), out);
    CHECK_EQ(out.size(), d.text.size());
    size_t ret = d.text.find("return"), num = d.text.find("42"), cm = d.text.find("// x");
    CHECK_EQ(int(out[ret]), int(H_KEYWORD));
    CHECK_EQ(int(out[num]), int(H_NUMBER));
    CHECK_EQ(int(out[cm]), int(H_COMMENT));
    // incremental update
    lines[1] = U"  return \"s\";";
    d.dirty = true;  // set by the application after a change
    d.sync(lines);
    d.highlight(0, uint32_t(d.text.size()), out);
    CHECK_EQ(int(out[d.text.find("\"s\"")]), int(H_STRING));
}

TEST(treesitter_injections) {
    TsLang* L = tsFind("html");
    CHECK(L != nullptr);
    if (!L) return;
    TsDoc d;
    d.reset(L);
    std::vector<u32> lines{U"<script>", U"var x = 1;", U"</script>"};
    d.sync(lines);
    std::vector<uint8_t> out;
    d.highlight(0, uint32_t(d.text.size()), out);
    CHECK_EQ(int(out[d.text.find("var")]), int(H_KEYWORD));  // JavaScript inside HTML
}

TEST(treesitter_all_grammars_load_without_dropped_patterns) {
    // the same check as "edith --ts-check" for every language with a grammar
    std::vector<std::string> bad;
    for (int i = 0; i < NLANGS; i++) {
        const char* tn = tsNameFor(i);
        if (!tn) continue;
        TsLang* L = tsFind(tn);
        if (!L || !L->hl.q) bad.push_back(std::string(tn) + ": missing");
        else if (L->hl.dropped) bad.push_back(std::string(tn) + ": " + std::to_string(L->hl.dropped) + " dropped");
    }
    CHECK_EQ(bad, std::vector<std::string>());
}

// The version number appears in several files; they must agree with src/version.h.
TEST(version_is_consistent) {
    std::string v = EDITH_VERSION;
    CHECK(readFile("docs/edith.1").find("\"edith " + v + "\"") != std::string::npos);
    CHECK(readFile("CHANGELOG.md").find("## [" + v + "]") != std::string::npos);
    CHECK(readFile("packaging/arch/PKGBUILD").find("\npkgver=" + v + "\n") != std::string::npos);
}
