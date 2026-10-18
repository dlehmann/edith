// Edith – a text editor for the terminal.
// Entry point: command line parsing and startup.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app.h"
#include "i18n.h"
#include "menus.h"
#include "paths.h"
#include "terminal.h"
#include "treesitter.h"
#include "version.h"

static void usage(const char* argv0) {
    printf(trs("Usage: %s [-n|-r] [-o|-O|-p] [--vim] [--lang=CODE] [file ...]\n\n"
               "Text editor for the terminal.\n"
               "Alt or F10 opens the menu, F1 shows the help.\n\n"
               "  -n            show line numbers\n"
               "  -r            show relative line numbers\n"
               "  -o / -O       several files in windows above each other / side by side\n"
               "  -p            several files in tabs (the default)\n"
               "  --vim         start with the Vim key bindings (Options > Vim Mode)\n"
               "  --lang=CODE   user interface language: %s\n                (default: from the locale)\n"
               "  --version     show the version\n"
               "  --ts-check    load and check all tree-sitter grammars\n\n"
               "The environment variable EDITH_COLORS=16 forces 16 colors instead of true color.\n")
               .c_str(),
           argv0, uiLanguageCodes().c_str());
}

int main(int argc, char** argv) {
    // The UI language comes first: every message below is translated.
    g_uiLang = detectUiLanguage();
    for (int i = 1; i < argc && strcmp(argv[i], "--") != 0; i++) {
        if (strncmp(argv[i], "--lang=", 7) != 0) continue;
        if (!parseUiLanguage(argv[i] + 7, g_uiLang)) {
            fprintf(stderr, trs("edith: unknown language \"%s\" (use one of: %s).\n").c_str(), argv[i] + 7,
                    uiLanguageCodes().c_str());
            return 2;
        }
    }
    buildMenus();

    std::vector<std::string> paths;
    char how = 't';  // several files: tabs, -o / -O: split windows
    bool options = true;  // false after "--": everything else is a file name
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (!options || a.size() < 2 || a[0] != '-') {
            paths.push_back(a);
            continue;
        }
        if (a == "--") { options = false; continue; }
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (a == "--version") {
            printf("edith %s\n", EDITH_VERSION);
            return 0;
        }
        if (a == "--ts-check") {
            tsCheckAll();
            return 0;
        }
        if (a == "-n") { S.lineNums = 1; continue; }
        if (a == "-r") { S.lineNums = 2; continue; }
        if (a == "--vim") { S.vim = true; continue; }
        if (a == "-o" || a == "-O" || a == "-p") { how = a == "-p" ? 't' : a[1]; continue; }
        if (a.compare(0, 7, "--lang=") == 0) continue;
        fprintf(stderr, trs("edith: unknown option \"%s\" (see edith --help).\n").c_str(), a.c_str());
        return 2;
    }

    const char* term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) {  // e.g. the shell buffer of Emacs: no cursor movement
        fputs(trs("edith: this terminal (TERM=dumb) cannot show a full-screen program.\n").c_str(), stderr);
        return 1;
    }
    if (!termInit()) {
        fputs(trs("edith: a terminal is required.\n").c_str(), stderr);
        return 1;
    }
    auto app = std::make_unique<App>();
    g_scr = &app->scr;
    // Use the original CGA colors when the terminal supports true color.
    const char* ct = getenv("COLORTERM");
    const char* force = getenv("EDITH_COLORS");
    bool linuxConsole = term && strncmp(term, "linux", 5) == 0;
    app->scr.truecolor = ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"));
    if (linuxConsole) app->scr.truecolor = false;
    app->scr.boldBright = linuxConsole;  // the console shows the bright colors only as bold
    if (force && strcmp(force, "16") == 0) app->scr.truecolor = false;
    if (force && strcmp(force, "24") == 0) app->scr.truecolor = true;
    app->scr.resize();
    for (auto& p : paths) p = absPath(p);
    if (!paths.empty()) app->openFiles(paths, how);
    app->run();
    termRestore();
    return 0;
}
