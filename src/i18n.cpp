// UI language detection and the lookup of translations (see i18n.h).

#include "i18n.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "utf8.h"

UiLang g_uiLang = UiLang::English;

UiLang detectUiLanguage() {
    // Same precedence as GNU gettext: LANGUAGE (only if the locale is not "C"),
    // then LC_ALL, LC_MESSAGES and LANG.
    auto env = [](const char* name) -> std::string {
        const char* v = getenv(name);
        return v ? v : "";
    };
    std::string locale;
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        locale = env(name);
        if (!locale.empty()) break;
    }
    bool cLocale = locale.empty() || locale == "C" || locale == "POSIX" || locale.compare(0, 2, "C.") == 0;
    std::string pick = locale;
    std::string language = env("LANGUAGE");
    if (!language.empty() && !cLocale) pick = language.substr(0, language.find(':'));
    UiLang l;
    return parseUiLanguage(pick, l) ? l : UiLang::English;
}

bool parseUiLanguage(const std::string& s, UiLang& out) {
    // the language part of a locale name: letters up to "_", "-", "." or "@"
    std::string code;
    for (char c : s) {
        if (c == '_' || c == '-' || c == '.' || c == '@') break;
        code += char(tolower((unsigned char)c));
    }
    for (auto& l : uiLanguages())
        if (code == l.code) { out = l.id; return true; }
    return false;
}

extern const LangTable LANG_DE, LANG_FR, LANG_ES, LANG_IT, LANG_HU, LANG_PL, LANG_TLH;

const std::vector<UiLangInfo>& uiLanguages() {
    static const std::vector<UiLangInfo> L = {
        {UiLang::English, "en", "English", nullptr},
        {UiLang::German, "de", "Deutsch", &LANG_DE},
        {UiLang::French, "fr", "Français", &LANG_FR},
        {UiLang::Spanish, "es", "Español", &LANG_ES},
        {UiLang::Italian, "it", "Italiano", &LANG_IT},
        {UiLang::Hungarian, "hu", "Magyar", &LANG_HU},
        {UiLang::Polish, "pl", "Polski", &LANG_PL},
        {UiLang::Klingon, "tlh", "tlhIngan Hol", &LANG_TLH},
    };
    return L;
}

std::string uiLanguageCodes() {
    std::string r;
    for (auto& l : uiLanguages()) r += (r.empty() ? "" : " ") + std::string(l.code);
    return r;
}

const LangTable* langTable(UiLang l) {
    for (auto& li : uiLanguages())
        if (li.id == l) return li.table;
    return nullptr;
}

const char32_t* tr(const char32_t* en) {
    const LangTable* t = langTable(g_uiLang);
    if (!t) return en;
    static std::unordered_map<int, std::unordered_map<u32, const char32_t*>> maps;
    auto& map = maps[int(g_uiLang)];
    if (map.empty())
        for (size_t i = 0; i < t->count; i++) map.emplace(t->entries[i][0], t->entries[i][1]);
    auto it = map.find(en);
    return it != map.end() ? it->second : en;
}

std::string trs(const char* en) {
    u32 s = U(en);
    return toUtf8(tr(s.c_str()));
}

const std::vector<u32>* translatedHelp() {
    const LangTable* t = langTable(g_uiLang);
    return t ? t->help : nullptr;
}

const std::vector<u32>* translatedHelpMac() {
    const LangTable* t = langTable(g_uiLang);
    return t ? t->helpMac : nullptr;
}

std::string errnoText(int e) {
    const char32_t* m = nullptr;
    switch (e) {
        case ENOENT: m = tr(U"No such file or directory."); break;
        case EACCES: m = tr(U"Permission denied."); break;
        case EPERM: m = tr(U"Operation not permitted."); break;
        case EISDIR: m = tr(U"That is a directory."); break;
        case ENOTDIR: m = tr(U"A part of the path is not a directory."); break;
        case ENOSPC: m = tr(U"No space left on the device."); break;
        case EROFS: m = tr(U"The file system is read-only."); break;
        case EEXIST: m = tr(U"The file already exists."); break;
        case ENAMETOOLONG: m = tr(U"The file name is too long."); break;
    }
    return m ? toUtf8(m) : std::string(strerror(e));
}
