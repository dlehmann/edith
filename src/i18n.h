#pragma once
// User interface language.
//
// All user-visible strings are written in US English in the source code and wrapped
// in tr(). For the other languages tr() returns the translation from the language's table
// in src/i18n_<code>.cpp. English is the fallback for every other locale and for any
// string that has no translation.
//
// Adding a language: add it to UiLang, create src/i18n_<code>.cpp with its table (copy an
// existing one) and add one line to the list in uiLanguages() (i18n.cpp). See CONTRIBUTING.md.

#include <string>
#include <vector>

#include "common.h"

enum class UiLang { English, German, French, Spanish, Italian, Hungarian, Polish, Klingon };

extern UiLang g_uiLang;

// A translation table: English -> translation, and the help text in the language
struct LangTable {
    const char32_t* const (*entries)[2];
    size_t count;
    const std::vector<u32>* help;
};

struct UiLangInfo {
    UiLang id;
    const char* code;         // as for --lang= and in locale names ("tlh" for Klingon)
    const char* name;         // in the language itself
    const LangTable* table;   // nullptr for English
};
// All interface languages
const std::vector<UiLangInfo>& uiLanguages();
// The language codes separated by spaces ("en de fr ..."), for messages
std::string uiLanguageCodes();
// The table of a language (nullptr for English)
const LangTable* langTable(UiLang l);

// Picks the UI language from the environment (LANGUAGE, LC_ALL, LC_MESSAGES, LANG).
UiLang detectUiLanguage();
// Parses a language code such as "de", "fr_FR.UTF-8" or "tlh"; returns false for unknown values.
bool parseUiLanguage(const std::string& s, UiLang& out);

// Translates an English UI string (UTF-32). The returned pointer stays valid.
const char32_t* tr(const char32_t* en);
// Translates an English UI string given in UTF-8 (for printf-style output).
std::string trs(const char* en);
// The help text in the current language (nullptr = English)
const std::vector<u32>* translatedHelp();
// Localized message for an errno value.
std::string errnoText(int e);
