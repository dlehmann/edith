// Main loop, keyboard handling and menu actions.

#include "app.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clipboard.h"
#include "i18n.h"
#include "languages.h"
#include "menus.h"
#include "paths.h"
#include "utf8.h"
#include "version.h"

void App::setLang(int l) {
    Doc& d = doc();
    d.lang = l;
    d.hlValid = 0;
    const char* tn = tsNameFor(l);
    d.tsd.reset(tn ? tsFind(tn) : nullptr);
}

void App::autoLang() {
    if (!doc().langManual) setLang(detectLang(ed.filename, ed.lines[0]));
}

void App::consumeDirty(Editor& e, Doc& d) {
    if (e.dirty < INT_MAX) {
        d.hlValid = std::min(d.hlValid, e.dirty);
        d.tsd.dirty = true;
        e.dirty = INT_MAX;
    }
}

void App::copyToSystem(const u32& t) { clipboardCopy(t); }

u32 App::pasteText() {
    u32 t;
    return clipboardPaste(t) ? t : clip;
}

// ---- Vim host ----

int AppVimHost::viewHeight() { return app->th(); }
int AppVimHost::viewWidth() { return app->tw(); }
void AppVimHost::clipboardSet(const u32& t) {
    app->clip = t;
    app->copyToSystem(t);
}
u32 AppVimHost::clipboardGet() { return app->pasteText(); }
void AppVimHost::quit() { app->quit = true; }
void AppVimHost::bufferLoaded() {
    app->doc().langManual = false;
    app->autoLang();
}
void AppVimHost::showLines(const u32& title, const std::vector<u32>& lines) { app->linesDialog(title, lines); }
int AppVimHost::confirm(const u32& prompt) {
    app->ed.anch = false;
    int r = app->msgbox(tr(U"Change"), prompt, {tr(U"Yes"), tr(U"No"), tr(U"All"), tr(U"Quit"), tr(U"Last")});
    static const int ans[] = {'y', 'n', 'a', 'q', 'l'};
    return r >= 0 && r < 5 ? ans[r] : 27;
}
void AppVimHost::help() { app->helpDialog(); }
void AppVimHost::redraw() { app->scr.force = true; }
bool AppVimHost::setFiletype(const std::string& name) {
    u32 want = lower(U(name));
    for (int i = 0; i < NLANGS; i++) {
        if (lower(U(LANGS[i].name)) == want) {
            app->doc().langManual = true;
            app->setLang(i);
            return true;
        }
    }
    int l = detectLang("x." + name, u32());
    if (l < 0) return false;
    app->doc().langManual = true;
    app->setLang(l);
    return true;
}
void AppVimHost::setSyntax(bool on) { S.syntax = on; }

void App::doAct(int a) {
    Pos sa, sb;
    if (S.vim && a != A_VIM) {
        // Edit menu actions on a Visual mode selection
        switch (a) {
            case A_CUT: case A_COPY:
                vim.menuCopy(a == A_CUT);
                ed.scrollTo(th(), tw());
                return;
            case A_PASTE:
                vim.pasteText(pasteText());
                ed.scrollTo(th(), tw());
                return;
            case A_CLEAR:
                if (vim.visualActive()) vim.feed(U"\"_d");
                ed.scrollTo(th(), tw());
                return;
        }
    }
    switch (a) {
        case A_NEW:
            if (confirmDiscard()) {
                if (shown(&doc(), win)) useNewDoc();
                ed = Editor();
                doc().langManual = false;
                setLang(-1);
                vim.reset();
            }
            break;
        case A_OPEN: doOpen(); break;
        case A_SAVE: doSave(); break;
        case A_SAVEAS: doSaveAs(); break;
        case A_EXIT:
            if (confirmDiscardAll()) quit = true;
            break;
        case A_SPLIT: case A_VSPLIT: {
            u32 err;
            if (!splitWin(a == A_VSPLIT, nullptr, &err)) error(err);
            break;
        }
        case A_CLOSEWIN: closeWin(win, false); break;
        case A_ONLY:
            for (Win* w : windows())
                if (w != win && confirmClose(w)) removeWin(w);
            break;
        case A_NEXTWIN: case A_PREVWIN: {
            std::vector<Win*> ws = windows();
            size_t i = std::find(ws.begin(), ws.end(), win) - ws.begin();
            activate(ws[(i + (a == A_NEXTWIN ? 1 : ws.size() - 1)) % ws.size()]);
            break;
        }
        case A_NEWTAB: newTab(nullptr); break;
        case A_OPENTAB: openInNewTab(); break;
        case A_CLOSETAB:
            if (tabs.size() == 1) {
                if (confirmDiscardAll()) quit = true;
            } else {
                closeTab(curTab, false);
            }
            break;
        case A_FORMAT: reformatCode(); break;
        case A_NEXTTAB: gotoTab(curTab + 1); break;
        case A_PREVTAB: gotoTab(curTab - 1); break;
        case A_UNDO: ed.doUndo(); break;
        case A_REDO: ed.doRedo(); break;
        case A_CUT:
        case A_COPY:
            if (ed.sel(sa, sb)) {
                clip = ed.getText(sa, sb);
                copyToSystem(clip);
                if (a == A_CUT) { ed.checkpoint(0); ed.deleteSel(); }
            }
            break;
        case A_PASTE: ed.paste(pasteText()); break;
        case A_CLEAR:
            if (ed.hasSel()) { ed.checkpoint(0); ed.deleteSel(); }
            break;
        case A_SELALL: ed.selectAll(); break;
        case A_FIND: findDialog(); break;
        case A_FINDNEXT: findNext(); break;
        case A_REPLACE: replaceDialog(); break;
        case A_GOTO: gotoDialog(); break;
        case A_OVERWRITE: S.overwrite = !S.overwrite; break;
        case A_AUTOINDENT: S.autoindent = !S.autoindent; break;
        case A_EXPANDTAB: S.expandtab = !S.expandtab; break;
        case A_TABWIDTH: {
            int v = numberDialog(tr(U"Tab Width"), tr(U"Tab width:"), S.tabw);
            if (v >= 1 && v <= 32) S.tabw = v;
            else if (v != -1) error(tr(U"Please enter a number from 1 to 32."));
            break;
        }
        case A_HELP: helpDialog(); break;
        case A_SYNTAX: S.syntax = !S.syntax; break;
        case A_LINENUM: S.lineNums = S.lineNums == 1 ? 0 : 1; break;
        case A_RELNUM: S.lineNums = S.lineNums == 2 ? 0 : 2; break;
        case A_INVISIBLES: S.invisibles = !S.invisibles; break;
        case A_LANG: langDialog(); break;
        case A_ABOUT: {
            char about[512];
            snprintf(about, sizeof about,
                     trs("Edith %s\n"
                         "A user-friendly console editor\n\n"
                         "Written in 2026.\n"
                         "Released under the MIT License.")
                         .c_str(),
                     EDITH_VERSION);
            msgbox(tr(U"About Edith"), U(about));
            break;
        }
        case A_REDRAW: scr.force = true; break;
        case A_VIM: {
            // a selection stays selected: in Vim mode as Visual mode
            Pos anchor = ed.anchor, cur{ed.cy, ed.cx};
            bool sel = S.vim ? vim.selectionFromVisual(anchor, cur) : ed.hasSel();
            S.vim = !S.vim;
            vim.reset();
            if (sel && S.vim) {
                vim.visualFromSelection(anchor, cur);
            } else if (sel) {
                ed.anch = true;
                ed.anchor = anchor;
                ed.cy = cur.y;
                ed.cx = cur.x;
            }
            break;
        }
    }
    if (S.vim && a != A_VIM) {
        // A found match is selected by Edith; in Vim mode the cursor goes to its start instead
        if ((a == A_FIND || a == A_FINDNEXT || a == A_REPLACE || a == A_GOTO) && ed.anch) {
            ed.cy = ed.anchor.y;
            ed.cx = ed.anchor.x;
            ed.anch = false;
        }
        vim.afterExternal();
    }
    if (!quit) ed.scrollTo(th(), tw());
}

void App::handleKey(const Key& k) {
    if (k.code != K_NONE && k.code != K_TIMEOUT && k.code != K_RESIZE && !(k.code == K_MOUSE && g_mouse.motion))
        statusMsg.clear();
    // a key ends a drag: it may close or rearrange windows and so free the node dragNode points to
    if (k.code != K_MOUSE && k.code != K_NONE && k.code != K_RESIZE) {
        dragging = 0;
        dragNode = nullptr;
    }
    switch (k.code) {
        case K_MOUSE:
            handleMouse();
            if (S.vim) {
                // like Vim: scrolling with the wheel keeps the cursor in the view
                if (g_mouse.wheel)
                    ed.cy = std::clamp(ed.cy, ed.top, std::max(ed.top, std::min(ed.n() - 1, ed.top + th() - 1)));
                vim.afterExternal();
            }
            return;
        case K_RESIZE: layoutAll(); ed.scrollTo(th(), tw()); return;
        case K_NONE: return;
        case K_TIMEOUT:
            if (S.vim) { vim.timeout(); ed.scrollTo(th(), tw()); }
            return;
        case K_PASTE:
            if (S.vim) vim.pasteText(g_paste);
            else ed.paste(g_paste);
            ed.scrollTo(th(), tw());
            return;
    }
    if (k.code == K_F1 + 9 || k.code == K_ALTMENU) { doAct(menuLoop(0, false)); return; }
    // Cmd shortcuts belong to the application, also in Vim mode
    if ((k.mods & M_CMD) && (k.code == K_CHAR || k.code == K_BKSP)) {
        if (int a = shortcut(k)) doAct(a);
        else if (k.code == K_BKSP && !S.vim) { ed.backspaceWord(true); ed.scrollTo(th(), tw()); }
        return;
    }
    if (k.code == K_CHAR && (k.mods & M_ALT)) {
        for (size_t i = 0; i < MENUS.size(); i++)
            if (hotkeyOf(MENUS[i].title) == fold(k.ch)) { doAct(menuLoop(int(i), true)); return; }
        // Esc quickly followed by a key arrives as Alt+key: in Vim mode it is Esc and the key
        if (S.vim && !(k.mods & M_CTRL)) {
            vim.feed(u32(1, VK::ESC) + u32(1, k.ch));
            ed.scrollTo(th(), tw());
        }
        return;
    }
    if (S.vim && vim.handleKey(k)) {
        ed.scrollTo(th(), tw());
        return;
    }
    if (int a = shortcut(k)) { doAct(a); return; }
    switch (k.code) {
        case K_UP: case K_DOWN: case K_LEFT: case K_RIGHT:
        case K_HOME: case K_END: case K_PGUP: case K_PGDN:
            ed.moveKey(k.code, k.mods, th());
            break;
        case K_ENTER: ed.newline(); break;
        case K_BKSP:
            if (k.mods & (M_CTRL | M_ALT)) ed.backspaceWord(false);
            else ed.backspace();
            break;
        case K_DEL: ed.delFwd(); break;
        case K_TAB: ed.tab(k.mods & M_SHIFT); break;
        case K_CHAR:
            if (!(k.mods & (M_CTRL | M_ALT)) && k.ch >= 32) ed.typeChar(k.ch);
            break;
        default: return;
    }
    ed.scrollTo(th(), tw());
}

void App::run() {
    while (!quit) {
        drawAll();
        placeCursor();
        scr.flush();
        if (quit) break;
        // an ambiguous Vim mapping waits at most 'timeoutlen' for the next key
        g_keyTimeout = (S.vim && vim.waiting()) ? (vim.opt.timeout ? vim.opt.timeoutlen : -1) : -1;
        Key k = getKey();
        g_keyTimeout = -1;
        handleKey(k);
        // handle all pending key presses first, then redraw once
        while (!quit && inputPending()) handleKey(getKey());
    }
}
