// Mouse handling: clicking, dragging, wheel, scroll bars, windows and tabs.

#include "app.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "i18n.h"
#include "menus.h"
#include "utf8.h"

int minSize(const Node* n, bool rows);

// The vertical scroll bar covers the whole editor area and belongs to the active window
static int areaY() { return 2; }
static int areaH(const Screen& s) { return std::max(1, s.h - 4); }

void App::vscrollTo(int y) {
    int tl = areaH(scr) - 2;
    if (tl <= 1) return;
    int nl = ed.n();
    int pos = std::clamp(y - (areaY() + 1), 0, tl - 1);
    if (hexMode()) {
        hexVscroll(pos, tl);
        return;
    }
    ed.top = std::max(0, nl - th()) * pos / (tl - 1);
    ed.anch = false;
    ed.cy = std::clamp(ed.cy, ed.top, std::min(nl - 1, ed.top + th() - 1));
    ed.clampCursor();
}

void App::hscrollTo(int x) {
    int tl = scr.w - 4;
    if (tl <= 1) return;
    int pos = std::clamp(x - 2, 0, tl - 1);
    ed.left = std::max(0, maxLineWidth() - tw()) * pos / (tl - 1);
}

// The window whose text area or tab line contains the point
static Win* winAt(const std::vector<Win*>& ws, int x, int y) {
    for (Win* w : ws)
        if (x >= w->x && x < w->x + w->w && y >= w->y - 1 && y < w->y + w->h) return w;
    return nullptr;
}

// A border between windows at the point (a separator column or the tab line of a lower window):
// the container and the index of the child before it
static bool borderAt(Node* n, int x, int y, Node*& p, int& idx) {
    if (n->kind == Node::LEAF) return false;
    for (size_t i = 0; i < n->kids.size(); i++) {
        Node* c = n->kids[i].get();
        if (i + 1 < n->kids.size()) {
            if (n->kind == Node::ROWS && y == c->y + c->h && x >= c->x && x < c->x + c->w) {
                p = n;
                idx = int(i);
                return true;
            }
            if (n->kind == Node::COLS && x == c->x + c->w && y >= n->y && y < n->y + n->h) {
                p = n;
                idx = int(i);
                return true;
            }
        }
        if (borderAt(c, x, y, p, idx)) return true;
    }
    return false;
}

void App::handleMouse() {
    const MouseEv& m = g_mouse;
    if (m.wheel) {
        if (Win* w = winAt(windows(), m.x, m.y)) activate(w);
        if (hexMode()) {
            hexWheel(3 * m.wheel);
            return;
        }
        int nl = ed.n();
        ed.top = std::clamp(ed.top + 3 * m.wheel, 0, std::max(0, nl - th()));
        return;
    }
    if (m.release) { dragging = 0; dragNode = nullptr; return; }
    if (m.motion) {
        if (m.btn != 0) return;
        int nl = ed.n();
        if (dragging == 1 && hexMode()) {
            hexMouseAt(m.x, m.y, true);
        } else if (dragging == 1) {
            int row = m.y - ty(), col = m.x - tx();
            if (row < 0) { ed.top = std::max(0, ed.top - 1); row = 0; }
            else if (row >= th()) { ed.top = std::min(ed.top + 1, std::max(0, nl - th())); row = th() - 1; }
            if (col < 0) { ed.left = std::max(0, ed.left - 1); col = 0; }
            else if (col >= tw()) { ed.left++; col = tw() - 1; }
            int ly = std::min(ed.top + row, nl - 1);
            ed.setCursor(ly, ed.idxOf(ed.lines[size_t(ly)], ed.left + col), true);
            ed.wantCol = -1;
        } else if (dragging == 2) {
            vscrollTo(m.y);
        } else if (dragging == 3) {
            hscrollTo(m.x);
        } else if (dragging == 4 && dragNode && size_t(dragKid) + 1 < dragNode->kids.size()) {
            // moving the border after child dragKid
            Node* c = dragNode->kids[size_t(dragKid)].get();
            bool rows = dragNode->kind == Node::ROWS;
            int size = rows ? m.y - c->y : m.x - c->x;
            for (size_t i = 0; i < dragNode->kids.size(); i++) {
                Node* k = dragNode->kids[i].get();
                k->weight = rows ? k->h : k->w;
            }
            Node* next = dragNode->kids[size_t(dragKid) + 1].get();
            int both = (rows ? c->h + next->h : c->w + next->w);
            int mn = minSize(c, rows), mn2 = minSize(next, rows);
            size = std::clamp(size, mn, std::max(mn, both - mn2));
            c->weight = size;
            next->weight = both - size;
            layoutAll();
        }
        return;
    }
    if (!m.press || m.btn != 0) return;

    if (m.y == 0) {
        int mi = menuAt(m.x);
        if (mi >= 0) doAct(menuLoop(mi, true));
        return;
    }
    if (m.y == scr.h - 1) {
        if (m.x >= 1 && m.x < 1 + int(statusHint().find(U'>')) + 1) doAct(A_HELP);
        return;
    }
    // the tabs of a window: a click shows the tab, a click on its × closes it
    if (m.x < scr.w - 1)
        if (Win* w = winAt(windows(), m.x, m.y); w && m.y == w->y - 1) {
            auto lay = tabLayout(*w);
            for (size_t t = 0; t < lay.size(); t++)
                if (lay[t].second && m.x >= lay[t].first && m.x < lay[t].first + lay[t].second) {
                    if (m.x != lay[t].first + lay[t].second - 2) activate(w, int(t));
                    else closeTab(w, int(t), false);  // the last tab closes the window, the very last one exits
                    return;
                }
        }
    // borders between windows can be dragged
    Node* bp = nullptr;
    int bi = 0;
    if (m.x < scr.w - 1 && borderAt(root.get(), m.x, m.y, bp, bi)) {
        bool tabLine = bp->kind == Node::ROWS;
        if (tabLine)
            if (Win* w = winAt(windows(), m.x, m.y)) activate(w);
        layoutAll();
        dragging = 4;
        dragNode = bp;
        dragKid = bi;
        return;
    }
    if (m.x < scr.w - 1) {
        Win* w = winAt(windows(), m.x, m.y);
        if (w && w != win) activate(w);
        if (w && m.y == w->y - 1) return;  // the tab line of a window
    }
    if (hexMode()) {
        int ay = areaY(), ah = areaH(scr);
        if (m.y >= ty() && m.y < ty() + th() && m.x >= tx() && m.x < tx() + tw()) {
            hexMouseAt(m.x, m.y, (m.mods & M_SHIFT) != 0);
            dragging = 1;
        } else if (m.x == scr.w - 1 && m.y >= ay && m.y < ay + ah) {
            if (m.y == ay) hexWheel(-1);
            else if (m.y == ay + ah - 1) hexWheel(1);
            else { vscrollTo(m.y); dragging = 2; }
        }
        return;
    }
    int nl = ed.n();
    if (gutter() && m.y >= ty() && m.y < ty() + th() && m.x >= win->x && m.x < tx()) {
        int ly = std::min(ed.top + m.y - ty(), nl - 1);
        ed.setCursor(ly, 0, false);
        ed.setCursor(ly + 1 < nl ? ly + 1 : ly, ly + 1 < nl ? 0 : int(ed.lines[size_t(ly)].size()), true);
        dragging = 1;
        return;
    }
    if (m.y >= ty() && m.y < ty() + th() && m.x >= tx() && m.x < tx() + tw()) {
        int ly = std::min(ed.top + m.y - ty(), nl - 1);
        int lx = ed.idxOf(ed.lines[size_t(ly)], ed.left + m.x - tx());
        auto now = Clock::now();
        if (lastClickP == Pos{ly, lx} && now - lastClickT < std::chrono::milliseconds(400)) {
            ed.selectWord(ly, lx);
            if (S.vim && ed.hasSel() && ed.cx > 0) ed.cx--;  // Visual mode includes the last character
            lastClickP = {-1, -1};
            return;
        }
        lastClickT = now;
        lastClickP = {ly, lx};
        ed.setCursor(ly, lx, (m.mods & M_SHIFT) != 0);
        ed.wantCol = -1;
        dragging = 1;
        return;
    }
    int ay = areaY(), ah = areaH(scr);
    if (m.x == scr.w - 1 && m.y >= ay && m.y < ay + ah) {
        if (m.y == ay) ed.top = std::max(0, ed.top - 1);
        else if (m.y == ay + ah - 1) ed.top = std::min(std::max(0, nl - th()), ed.top + 1);
        else { vscrollTo(m.y); dragging = 2; }
        return;
    }
    if (m.y == scr.h - 2 && m.x >= 1 && m.x <= scr.w - 2) {
        if (m.x == 1) ed.left = std::max(0, ed.left - 1);
        else if (m.x == scr.w - 2) ed.left++;
        else { hscrollTo(m.x); dragging = 3; }
    }
}
