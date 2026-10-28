// Pull-down menus and keyboard shortcuts.

#include "app.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "clipboard.h"
#include "i18n.h"
#include "menus.h"
#include "utf8.h"

bool App::enabled(int act) const {
    if (hexMode()) {
        const HexBuf& h = *doc().hex;
        switch (act) {
            case A_UNDO: return !h.undo.empty();
            case A_REDO: return !h.redo.empty();
            case A_CUT: case A_COPY: case A_CLEAR: return h.hasSel();
            // the text options and commands do not apply to bytes
            case A_FORMAT: case A_REPLACE: case A_AUTOINDENT: case A_EXPANDTAB: case A_TABWIDTH:
            case A_LINENUM: case A_RELNUM: case A_INVISIBLES: case A_SYNTAX: case A_LANG: case A_VIM:
                return false;
        }
    }
    switch (act) {
        case A_UNDO: return !ed.undo.empty();
        case A_REDO: return !ed.redo.empty();
        case A_CUT: case A_COPY: case A_CLEAR: return S.vim ? vim.visualActive() : ed.hasSel();
        case A_PASTE: return !clip.empty() || clipboardReadable();
        case A_ONLY: case A_NEXTWIN: case A_PREVWIN: return windows().size() > 1;
        case A_NEXTTAB: case A_PREVTAB: return tabs.size() > 1;
    }
    return true;
}

bool App::checked(int act) const {
    switch (act) {
        case A_OVERWRITE: return hexMode() ? !doc().hex->insert : S.overwrite;
        case A_HEX: return hexMode();
        case A_AUTOINDENT: return S.autoindent;
        case A_EXPANDTAB: return S.expandtab;
        case A_SYNTAX: return S.syntax;
        case A_LINENUM: return S.lineNums == 1;
        case A_RELNUM: return S.lineNums == 2;
        case A_INVISIBLES: return S.invisibles;
        case A_VIM: return S.vim;
    }
    return false;
}

int App::menuLoop(int mi, bool dropped) {
    dragging = 0;
    int nm = int(MENUS.size());
    auto step = [&](int m, int i, int d) {
        const auto& it = MENUS[m].items;
        int cnt = int(it.size());
        for (int k = 0; k < cnt; k++) {
            i = (i + d + cnt) % cnt;
            if (it[i].label) return i;
        }
        return 0;
    };
    int cur = step(mi, -1, 1);
    for (;;) {
        const auto& items = MENUS[mi].items;
        u32 st = dropped ? u32(items[cur].help) : u32(tr(U"Enter=Open menu  Esc=Cancel  ←→=Switch menu"));
        drawAll(&st, mi);
        if (dropped && !tooSmall()) drawDropdown(mi, cur);
        scr.cvis = false;
        scr.flush();

        Key k = getKey();
        if (k.code == K_CHAR && !k.mods) {  // h j k l move like the arrow keys (also without Vim mode)
            switch (k.ch) {
                case 'h': k.code = K_LEFT; break;
                case 'j': k.code = K_DOWN; break;
                case 'k': k.code = K_UP; break;
                case 'l': k.code = K_RIGHT; break;
            }
        }
        switch (k.code) {
            case K_ESC: case K_ALTMENU: return A_NONE;
            case K_RESIZE: continue;
            case K_LEFT: mi = (mi - 1 + nm) % nm; cur = step(mi, -1, 1); continue;
            case K_RIGHT: mi = (mi + 1) % nm; cur = step(mi, -1, 1); continue;
            case K_UP:
                if (dropped) cur = step(mi, cur, -1);
                else dropped = true;
                continue;
            case K_DOWN:
                if (dropped) cur = step(mi, cur, 1);
                else dropped = true;
                continue;
            case K_HOME: if (dropped) cur = step(mi, -1, 1); continue;
            case K_END: if (dropped) cur = step(mi, 0, -1); continue;
            case K_ENTER:
                if (!dropped) dropped = true;
                else if (enabled(items[cur].act)) return items[cur].act;
                continue;
            case K_MOUSE: break;
            case K_CHAR: break;
            default:
                if (k.code == K_F1 + 9) return A_NONE;  // F10 closes the menu
                continue;
        }
        if (k.code == K_CHAR) {
            // entries and menus are chosen with Alt+letter only: plain letters are not hotkeys here
            if (!(k.mods & M_ALT)) continue;
            char32_t ch = fold(k.ch);
            if (dropped) {  // an entry of the open menu first, then another menu
                for (size_t i = 0; i < items.size(); i++)
                    if (items[i].label && hotkeyOf(items[i].label) == ch && enabled(items[i].act)) return items[i].act;
            }
            for (int j = 0; j < nm; j++)
                if (hotkeyOf(MENUS[j].title) == ch) { mi = j; dropped = true; cur = step(mi, -1, 1); }
            continue;
        }
        // Mouse
        const MouseEv& m = g_mouse;
        if (m.wheel) continue;
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (dropped) dropRect(mi, rx, ry, rw, rh);
        bool inItems = dropped && m.x > rx && m.x < rx + rw - 1 && m.y > ry && m.y < ry + rh - 1;
        int idx = inItems ? m.y - ry - 1 : -1;
        if (m.release) {
            if (inItems && items[idx].label && enabled(items[idx].act)) return items[idx].act;
            continue;
        }
        if (!(m.press || m.motion) || m.btn != 0) continue;
        if (m.y == 0) {
            int mm = menuAt(m.x);
            if (mm >= 0) {
                if (m.press && mm == mi && dropped) return A_NONE;  // clicking the open menu again closes it
                if (mm != mi || !dropped) { mi = mm; dropped = true; cur = step(mi, -1, 1); }
            } else if (m.press) {
                return A_NONE;
            }
        } else if (inItems) {
            if (items[idx].label) cur = idx;
        } else if (m.press) {
            return A_NONE;
        }
    }
}

int App::shortcut(const Key& k) const {
    // Cmd (macOS): like Ctrl, with the Mac's own keys for Find Next, Go to Line, Redo, Change and tabs
    if (k.code == K_CHAR && (k.mods & M_CMD)) {
        int m = k.mods & ~M_CMD;
        char32_t c = fold(k.ch);
        if (m == M_SHIFT && c == 'z') return A_REDO;
        if (m == M_ALT && c == 'f') return A_REPLACE;
        if ((m & ~M_SHIFT) == 0 && (c == ']' || c == '}')) return A_NEXTTAB;
        if ((m & ~M_SHIFT) == 0 && (c == '[' || c == '{')) return A_PREVTAB;
        if (m) return A_NONE;
        if (c == 'g') return A_FINDNEXT;
        if (c == 'l') return A_GOTO;
        Key ck = k;
        ck.mods = M_CTRL;
        ck.ch = c;
        return shortcut(ck);
    }
    if (k.code == K_CHAR && k.mods == M_CTRL) {
        switch (k.ch) {
            case 'n': return A_NEW;
            case 'o': return A_OPEN;
            case 's': return A_SAVE;
            case 'q': return A_EXIT;
            case 'z': return A_UNDO;
            case 'y': return A_REDO;
            case 'x': return A_CUT;
            case 'c': return A_COPY;
            case 'v': return A_PASTE;
            case 'a': return A_SELALL;
            case 'f': return A_FIND;
            case 'r': return A_REPLACE;
            case 'g': return A_GOTO;
            case 'l': return A_REDRAW;
        }
    }
    if (k.code == K_CHAR && k.mods == M_CTRL) {
        if (k.ch == 't') return A_NEWTAB;
        if (k.ch == 'w') return A_CLOSEWIN;
    }
    if (k.code == K_F1 + 5) return (k.mods & M_SHIFT) ? A_PREVWIN : A_NEXTWIN;
    if (k.code == K_PGDN && k.mods == M_CTRL) return A_NEXTTAB;
    if (k.code == K_PGUP && k.mods == M_CTRL) return A_PREVTAB;
    if (k.code == K_F1 + 3 && !k.mods) return A_CLOSETAB;
    if (k.code == K_F1 + 7 && !k.mods) return A_FORMAT;
    if (k.code == K_F1 + 11 && !k.mods) return A_VIM;
    if (k.code == K_F1) return A_HELP;
    if (k.code == K_F1 + 2) return A_FINDNEXT;
    if (k.code == K_DEL && k.mods == M_SHIFT) return A_CUT;
    if (k.code == K_INS && k.mods == M_CTRL) return A_COPY;
    if (k.code == K_INS && k.mods == M_SHIFT) return A_PASTE;
    if (k.code == K_INS && k.mods == 0) return A_OVERWRITE;
    return A_NONE;
}
