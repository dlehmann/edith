// Buffers, split windows and tab pages.
//
// Every tab page has a layout tree of rows and columns whose leaves are windows. A window
// shows a buffer (Doc); several windows can show the same buffer. The buffer of the active
// window is checked out into App::ed, so that the editing code and the Vim engine always
// work on App::ed; switching to a window of another buffer swaps the texts (cheap moves).

#include "app.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "i18n.h"
#include "widgets.h"
#include "languages.h"
#include "paths.h"
#include "utf8.h"

App::App() {
    vimHost.app = this;
    g_clipboardPaste = [this] { return pasteText(); };
    docs.push_back(std::make_unique<Doc>());
    auto t = std::make_unique<Tab>();
    t->root = std::make_unique<Node>();
    t->root->win = std::make_unique<Win>();
    t->root->win->doc = docs[0].get();
    win = t->root->win.get();
    t->cur = win;
    tabs.push_back(std::move(t));
    layoutAll();
}

App::~App() { g_clipboardPaste = nullptr; }

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

// The smallest size of a node: a window needs one text line plus its status line and one column
int minSize(const Node* n, bool rows) {
    if (n->kind == Node::LEAF) return rows ? 2 : 1;
    bool along = (n->kind == Node::ROWS) == rows;
    int r = 0;
    for (auto& c : n->kids) {
        int m = minSize(c.get(), rows);
        r = along ? r + m : std::max(r, m);
    }
    if (along && !rows) r += int(n->kids.size()) - 1;  // the separator columns
    return r;
}

// Splits total into parts proportional to the weights, each at least its minimum (if possible)
static std::vector<int> distribute(int total, const std::vector<double>& wts, const std::vector<int>& mins) {
    size_t k = wts.size();
    std::vector<int> r(k, 0);
    if (k == 0) return r;
    double sum = 0;
    for (double w : wts) sum += std::max(w, 0.0001);
    int used = 0;
    for (size_t i = 0; i < k; i++) {
        r[i] = int(std::floor(total * std::max(wts[i], 0.0001) / sum));
        used += r[i];
    }
    for (size_t i = 0; used < total; i = (i + 1) % k) { r[i]++; used++; }
    // enforce the minimums by taking from the parts with the most room
    for (size_t i = 0; i < k; i++) {
        while (r[i] < mins[i]) {
            size_t big = k;
            for (size_t j = 0; j < k; j++)
                if (r[j] > mins[j] && (big == k || r[j] - mins[j] > r[big] - mins[big])) big = j;
            if (big == k) break;
            r[big]--;
            r[i]++;
        }
    }
    return r;
}

void App::layoutNode(Node* n, int x, int y, int w, int h, bool multi) {
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    if (n->kind == Node::LEAF) {
        Win& wi = *n->win;
        wi.x = x;
        wi.y = y;
        wi.w = std::max(1, w);
        if (multi) {
            wi.h = std::max(1, h - 1);
            wi.sy = y + std::max(1, h - 1);
        } else {
            wi.h = std::max(1, h);
            wi.sy = -1;
        }
        return;
    }
    int k = int(n->kids.size());
    bool rows = n->kind == Node::ROWS;
    std::vector<double> wts;
    std::vector<int> mins;
    for (auto& c : n->kids) {
        wts.push_back(c->weight);
        mins.push_back(minSize(c.get(), rows));
    }
    int total = rows ? h : w - (k - 1);
    std::vector<int> sz = distribute(std::max(total, k), wts, mins);
    int pos = rows ? y : x;
    for (int i = 0; i < k; i++) {
        if (rows) layoutNode(n->kids[size_t(i)].get(), x, pos, w, sz[size_t(i)], multi);
        else layoutNode(n->kids[size_t(i)].get(), pos, y, sz[size_t(i)], h, multi);
        pos += sz[size_t(i)] + (rows ? 0 : 1);
    }
}

void App::layoutAll() {
    int ax = 1, ay = 2, aw = std::max(1, scr.w - 2), ah = std::max(2, scr.h - 4);
    for (auto& t : tabs) {
        bool multi = t->root->kind != Node::LEAF;
        layoutNode(t->root.get(), ax, ay, aw, ah, multi);
    }
}

static void collect(Node* n, std::vector<Win*>& out) {
    if (n->kind == Node::LEAF) {
        out.push_back(n->win.get());
        return;
    }
    for (auto& c : n->kids) collect(c.get(), out);
}

std::vector<Win*> App::windows(int t) const {
    std::vector<Win*> r;
    collect(tabs[size_t(t < 0 ? curTab : t)]->root.get(), r);
    return r;
}

static Node* findNode(Node* n, Win* w) {
    if (n->kind == Node::LEAF) return n->win.get() == w ? n : nullptr;
    for (auto& c : n->kids)
        if (Node* f = findNode(c.get(), w)) return f;
    return nullptr;
}

Node* App::nodeOf(Win* w) const {
    for (auto& t : tabs)
        if (Node* n = findNode(t->root.get(), w)) return n;
    return nullptr;
}

int App::tabOf(Win* w) const {
    for (size_t i = 0; i < tabs.size(); i++)
        if (findNode(tabs[i]->root.get(), w)) return int(i);
    return -1;
}

bool App::shown(Doc* d, Win* except) const {
    for (size_t t = 0; t < tabs.size(); t++)
        for (Win* w : windows(int(t)))
            if (w != except && w->doc == d) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Switching windows
// ---------------------------------------------------------------------------

void App::saveView() {
    View& v = win->v;
    v.cy = ed.cy;
    v.cx = ed.cx;
    v.top = ed.top;
    v.left = ed.left;
    v.anch = ed.anch;
    v.anchor = ed.anchor;
    v.wantCol = ed.wantCol;
    v.curswant = vim.curswant;
    if (const HexBuf* h = win->doc->hex.get()) {
        v.hexPos = h->pos;
        v.hexTop = h->top;
        v.hexChars = h->chars;
    }
}

static void loadView(Editor& ed, Vim& vim, const View& v) {
    ed.cy = std::clamp(v.cy, 0, ed.n() - 1);
    ed.cx = std::clamp(v.cx, 0, int(ed.lines[size_t(ed.cy)].size()));
    ed.top = std::clamp(v.top, 0, std::max(0, ed.n() - 1));
    ed.left = std::max(0, v.left);
    ed.anch = v.anch;
    ed.anchor = v.anchor;
    ed.anchor.y = std::clamp(ed.anchor.y, 0, ed.n() - 1);
    ed.anchor.x = std::clamp(ed.anchor.x, 0, int(ed.lines[size_t(ed.anchor.y)].size()));
    ed.wantCol = v.wantCol;
    ed.lastEdit = 0;
    vim.curswant = v.curswant;
}

void App::activate(Win* w) {
    if (!w || w == win) return;
    if (S.vim) vim.leaveWindow();
    saveView();
    vim.swapWinState(win->vim);
    Doc* od = win->doc;
    Doc* nd = w->doc;
    if (od != nd) {
        consumeDirty();
        std::swap(ed, od->store);
        vim.swapBufState(od->vim);
        std::swap(ed, nd->store);
        vim.swapBufState(nd->vim);
    }
    int t = tabOf(w);
    if (t != curTab) curTab = t;
    else tab().prev = win;
    tab().cur = w;
    win = w;
    vim.swapWinState(w->vim);
    loadView(ed, vim, w->v);
    if (HexBuf* h = w->doc->hex.get()) {
        h->pos = std::min(w->v.hexPos, h->size());
        h->top = w->v.hexTop;
        h->chars = w->v.hexChars;
        h->anch = false;
        h->nib = 0;
        h->lastKind = 0;
    }
    if (S.vim && !hexMode()) vim.afterExternal();
    layoutAll();
}

Doc* App::newDoc() {
    docs.push_back(std::make_unique<Doc>());
    return docs.back().get();
}

void App::useNewDoc() {
    if (S.vim) vim.leaveWindow();
    Doc* od = win->doc;
    Doc* nd = newDoc();
    consumeDirty();
    std::swap(ed, od->store);
    vim.swapBufState(od->vim);
    std::swap(ed, nd->store);
    vim.swapBufState(nd->vim);
    win->doc = nd;
    vim.reset();
    if (!shown(od, nullptr))
        for (size_t i = 0; i < docs.size(); i++)
            if (docs[i].get() == od) { docs.erase(docs.begin() + long(i)); break; }
}

// ---------------------------------------------------------------------------
// Splitting and closing
// ---------------------------------------------------------------------------

bool App::splitWin(bool vertical, Doc* d, u32* err) {
    // both windows need room for at least one line / a few columns
    if ((!vertical && win->h + (win->sy >= 0 ? 1 : 0) < 4) || (vertical && win->w < 7) ||
        scr.w < 40 || scr.h < 10) {
        if (err) *err = tr(U"E36: Not enough room");
        return false;
    }
    saveView();
    Node* ln = nodeOf(win);
    int kind = vertical ? Node::COLS : Node::ROWS;
    auto nw = std::make_unique<Win>();
    nw->doc = d ? d : win->doc;
    if (!d) {
        nw->v = win->v;
        nw->vim = VimWinState{vim.jumps, vim.jumpIdx};
    }
    Win* nwp = nw.get();
    auto leaf = std::make_unique<Node>();
    leaf->win = std::move(nw);
    if (ln->parent && ln->parent->kind == kind) {
        // a sibling in the same direction: share the space of the split window
        Node* p = ln->parent;
        size_t i = 0;
        while (p->kids[i].get() != ln) i++;
        leaf->weight = ln->weight / 2;
        ln->weight /= 2;
        leaf->parent = p;
        p->kids.insert(p->kids.begin() + long(i), std::move(leaf));
    } else {
        // the leaf becomes a container with the new window first (above / left)
        auto old = std::make_unique<Node>();
        old->win = std::move(ln->win);
        old->parent = ln;
        leaf->parent = ln;
        ln->kind = kind;
        ln->kids.clear();
        ln->kids.push_back(std::move(leaf));
        ln->kids.push_back(std::move(old));
    }
    layoutAll();
    activate(nwp);
    return true;
}

bool App::confirmClose(Win* w) {
    Doc* d = w->doc;
    Editor& e = textOf(d);
    if (!e.modified || shown(d, w)) return true;
    activate(w);
    return confirmDiscard();
}

void App::removeWin(Win* w) {
    int t = tabOf(w);
    if (t < 0) return;
    Tab& tb = *tabs[size_t(t)];
    std::vector<Win*> ws = windows(t);
    Doc* d = w->doc;
    if (ws.size() == 1) {
        // the last window of a tab page: the tab page goes away
        if (tabs.size() == 1) return;
        if (w == win) gotoTab(size_t(t) + 1 < tabs.size() ? t + 1 : t - 1);
        tabs.erase(tabs.begin() + t);
        if (curTab > t) curTab--;
    } else {
        if (w == win) {
            Win* next = tb.prev && tb.prev != w && findNode(tb.root.get(), tb.prev) ? tb.prev : nullptr;
            if (!next) {
                size_t i = std::find(ws.begin(), ws.end(), w) - ws.begin();
                next = ws[i + 1 < ws.size() ? i + 1 : i - 1];
            }
            activate(next);
        }
        if (tb.prev == w) tb.prev = nullptr;
        if (tb.cur == w) {
            for (Win* o : ws)
                if (o != w) { tb.cur = o; break; }
        }
        Node* ln = nodeOf(w);
        Node* p = ln->parent;
        size_t i = 0;
        while (p->kids[i].get() != ln) i++;
        double wt = ln->weight;
        p->kids.erase(p->kids.begin() + long(i));
        // the neighbor gets the space
        p->kids[std::min(i, p->kids.size() - 1)]->weight += wt;
        if (p->kids.size() == 1) {
            // a container with one child is replaced by the child
            std::unique_ptr<Node> only = std::move(p->kids[0]);
            p->kids.clear();
            p->kind = only->kind;
            p->win = std::move(only->win);
            p->kids = std::move(only->kids);
            for (auto& c : p->kids) c->parent = p;
            // ... and merged into a parent of the same direction
            Node* g = p->parent;
            if (g && p->kind != Node::LEAF && g->kind == p->kind) {
                size_t j = 0;
                while (g->kids[j].get() != p) j++;
                double sum = 0;
                for (auto& c : p->kids) sum += c->weight;
                std::vector<std::unique_ptr<Node>> moved = std::move(p->kids);
                double pw = p->weight;
                g->kids.erase(g->kids.begin() + long(j));
                for (size_t k = 0; k < moved.size(); k++) {
                    moved[k]->weight = pw * moved[k]->weight / std::max(sum, 0.0001);
                    moved[k]->parent = g;
                    g->kids.insert(g->kids.begin() + long(j + k), std::move(moved[k]));
                }
            }
        }
    }
    if (!shown(d, nullptr))
        for (size_t k = 0; k < docs.size(); k++)
            if (docs[k].get() == d) { docs.erase(docs.begin() + long(k)); break; }
    layoutAll();
}

bool App::closeWin(Win* w, bool force) {
    if (!force && !confirmClose(w)) return false;
    if (tabs.size() == 1 && windows().size() == 1) {
        quit = true;
        return true;
    }
    removeWin(w);
    return true;
}

bool App::onlyWin(bool force) {
    bool all = true;
    for (Win* w : windows()) {
        if (w == win) continue;
        Editor& e = textOf(w->doc);
        if (!force && e.modified && !shown(w->doc, w)) {
            all = false;
            continue;
        }
        removeWin(w);
    }
    return all;
}

void App::newTab(Doc* d) {
    saveView();
    auto t = std::make_unique<Tab>();
    t->root = std::make_unique<Node>();
    t->root->win = std::make_unique<Win>();
    Win* w = t->root->win.get();
    w->doc = d ? d : newDoc();
    if (d == win->doc) w->v = win->v;
    t->cur = w;
    tabs.insert(tabs.begin() + curTab + 1, std::move(t));
    layoutAll();
    activate(w);
}

bool App::closeTab(int t, bool force) {
    if (tabs.size() == 1) return false;
    std::vector<Win*> ws = windows(t);
    if (!force)
        for (Win* w : ws)
            if (!confirmClose(w)) return false;
    for (Win* w : windows(t)) removeWin(w);
    return true;
}

void App::gotoTab(int t) {
    if (tabs.empty()) return;
    t = ((t % int(tabs.size())) + int(tabs.size())) % int(tabs.size());
    activate(tabs[size_t(t)]->cur);
}

Win* App::neighbor(char32_t dir, int count) {
    Win* w = win;
    for (int k = 0; k < std::max(1, count); k++) {
        int row = ty() + std::clamp(ed.cy - ed.top, 0, th() - 1);
        int col = tx() + std::clamp(ed.colOf(ed.lines[size_t(ed.cy)], ed.cx) - ed.left, 0, tw() - 1);
        if (w != win) {
            row = w->y;
            col = w->x;
        }
        Win* best = nullptr;
        int bestDist = INT_MAX;
        for (Win* o : windows()) {
            if (o == w) continue;
            int oy2 = o->sy >= 0 ? o->sy : o->y + o->h - 1, wy2 = w->sy >= 0 ? w->sy : w->y + w->h - 1;
            bool adj;
            int dist;
            switch (dir) {
                case 'h':
                    adj = o->x + o->w + 1 == w->x;
                    dist = row < o->y ? o->y - row : row > oy2 ? row - oy2 : 0;
                    break;
                case 'l':
                    adj = w->x + w->w + 1 == o->x;
                    dist = row < o->y ? o->y - row : row > oy2 ? row - oy2 : 0;
                    break;
                case 'k':
                    adj = oy2 + 1 == w->y;
                    dist = col < o->x ? o->x - col : col >= o->x + o->w ? col - o->x - o->w + 1 : 0;
                    break;
                default:
                    adj = wy2 + 1 == o->y;
                    dist = col < o->x ? o->x - col : col >= o->x + o->w ? col - o->x - o->w + 1 : 0;
                    break;
            }
            if (adj && dist < bestDist) {
                best = o;
                bestDist = dist;
            }
        }
        if (!best) break;
        w = best;
    }
    return w;
}

// The container that splits in the given direction and the index of the child on the way to
// the current window
static Node* splitParent(Node* leaf, int kind, size_t& idx) {
    Node* c = leaf;
    for (Node* p = leaf->parent; p; c = p, p = p->parent) {
        if (p->kind != kind) continue;
        idx = 0;
        while (p->kids[idx].get() != c) idx++;
        return p;
    }
    return nullptr;
}

void setKidSize(Node* p, size_t idx, int size) {
    bool rows = p->kind == Node::ROWS;
    int k = int(p->kids.size());
    int total = rows ? p->h : p->w - (k - 1);
    int others = 0;
    for (size_t i = 0; i < p->kids.size(); i++)
        if (i != idx) others += minSize(p->kids[i].get(), rows);
    int mn = minSize(p->kids[idx].get(), rows);
    size = std::clamp(size, mn, std::max(mn, total - others));
    int cur = 0;
    for (size_t i = 0; i < p->kids.size(); i++)
        if (i != idx) cur += rows ? p->kids[i]->h : p->kids[i]->w;
    int rest = total - size;
    for (size_t i = 0; i < p->kids.size(); i++) {
        if (i == idx) p->kids[i]->weight = size;
        else p->kids[i]->weight = cur > 0 ? double(rows ? p->kids[i]->h : p->kids[i]->w) * rest / cur : 1;
    }
}

void App::resizeWin(bool vertical, int size, bool relative) {
    Node* leaf = nodeOf(win);
    size_t idx;
    Node* p = splitParent(leaf, vertical ? Node::COLS : Node::ROWS, idx);
    if (!p) return;
    Node* c = p->kids[idx].get();
    int cur = vertical ? c->w : c->h;
    int extra = vertical ? 0 : (win->sy >= 0 ? 1 : 0);  // a height counts the text lines
    int target = relative ? cur + size : (size == INT_MAX ? INT_MAX / 2 : size + extra);
    setKidSize(p, idx, target);
    layoutAll();
}

void App::equalize(Node* n) {
    n->weight = 1;
    for (auto& c : n->kids) equalize(c.get());
}

void App::moveToNewTab() {
    if (windows().size() < 2) return;
    Win* old = win;
    newTab(old->doc);
    removeWin(old);
}

void App::openInNewTab() {
    std::string p = fileDialog(tr(U"Open in New Tab"), "");
    if (p.empty()) return;
    newTab(nullptr);
    openPath(p);
}

// ---------------------------------------------------------------------------
// Vim commands for windows and tab pages
// ---------------------------------------------------------------------------

bool AppVimHost::bufferShownElsewhere() { return app->shown(&app->doc(), app->win); }

bool AppVimHost::layout(const std::string& cmd, int count, const u32& arg, bool bang, u32& err) {
    App& a = *app;
    auto openArg = [&] {
        if (arg.empty()) return;
        a.useNewDoc();
        a.openPath(absPath(toUtf8(arg)));
    };
    auto unsaved = [&](Doc* d, Win* except) {
        return a.textOf(d).modified && !a.shown(d, except);
    };
    if (cmd == "split" || cmd == "vsplit" || cmd == "new" || cmd == "vnew") {
        bool v = cmd[0] == 'v';
        if (!a.splitWin(v, nullptr, &err)) return false;
        if (cmd == "new" || cmd == "vnew") a.useNewDoc();
        openArg();
        if (count > 0) a.resizeWin(v, count, false);
        return true;
    }
    if (cmd == "newbuf") {
        a.useNewDoc();
        return true;
    }
    if (cmd == "close" || cmd == "quit") {
        if (cmd == "close" && a.tabs.size() == 1 && a.windows().size() == 1) {
            err = tr(U"E444: Cannot close last window");
            return false;
        }
        if (!bang && unsaved(&a.doc(), a.win)) {
            err = tr(U"E37: No write since last change (add ! to override)");
            return false;
        }
        a.closeWin(a.win, true);
        return true;
    }
    if (cmd == "qall") {
        if (!bang)
            for (auto& d : a.docs)
                if (a.textOf(d.get()).modified) {
                    for (size_t t = 0; t < a.tabs.size(); t++)
                        for (Win* w : a.windows(int(t)))
                            if (w->doc == d.get()) { a.activate(w); goto found; }
                found:
                    u32 nm = a.ed.filename.empty() ? u32(tr(U"[No Name]")) : U(a.ed.filename);
                    err = tr(U"E162: No write since last change for buffer \"") + nm + U"\"";
                    return false;
                }
        a.quit = true;
        return true;
    }
    if (cmd == "wall") {
        for (auto& d : a.docs) {
            Editor& e = a.textOf(d.get());
            if (!e.modified) continue;
            if (e.filename.empty()) {
                err = tr(U"E141: No file name for buffer");
                return false;
            }
            std::string er;
            bool conv;
            if (!a.saveDoc(d.get(), e.filename, er, conv)) {
                err = tr(U"E212: Can't open file for writing: ") + U(er);
                return false;
            }
        }
        return true;
    }
    if (cmd == "only") {
        if (!a.onlyWin(bang)) {
            err = tr(U"E445: Other window contains changes");
            return false;
        }
        return true;
    }
    if (cmd == "next" || cmd == "prev") {
        std::vector<Win*> ws = a.windows();
        size_t i = std::find(ws.begin(), ws.end(), a.win) - ws.begin();
        if (count > 0) i = size_t(std::min(count, int(ws.size())) - 1);
        else if (cmd == "next") i = (i + 1) % ws.size();
        else i = (i + ws.size() - 1) % ws.size();
        a.activate(ws[i]);
        return true;
    }
    if (cmd == "left" || cmd == "down" || cmd == "up" || cmd == "right") {
        char32_t d = cmd == "left" ? 'h' : cmd == "down" ? 'j' : cmd == "up" ? 'k' : 'l';
        a.activate(a.neighbor(d, count));
        return true;
    }
    if (cmd == "top" || cmd == "bottom") {
        std::vector<Win*> ws = a.windows();
        a.activate(cmd == "top" ? ws.front() : ws.back());
        return true;
    }
    if (cmd == "last") {
        Win* p = a.tab().prev;
        if (!p || a.tabOf(p) != a.curTab) return false;
        a.activate(p);
        return true;
    }
    if (cmd == "equal") {
        a.equalize(a.tab().root.get());
        a.layoutAll();
        return true;
    }
    if (cmd == "height") { a.resizeWin(false, count > 0 ? count : INT_MAX, false); return true; }
    if (cmd == "taller") { a.resizeWin(false, std::max(1, count), true); return true; }
    if (cmd == "shorter") { a.resizeWin(false, -std::max(1, count), true); return true; }
    if (cmd == "width") { a.resizeWin(true, count > 0 ? count : INT_MAX, false); return true; }
    if (cmd == "wider") { a.resizeWin(true, std::max(1, count), true); return true; }
    if (cmd == "narrower") { a.resizeWin(true, -std::max(1, count), true); return true; }
    if (cmd == "tonewtab") {
        a.moveToNewTab();
        return true;
    }
    if (cmd == "tabnew") {
        a.newTab(nullptr);
        if (!arg.empty()) a.openPath(absPath(toUtf8(arg)));
        return true;
    }
    if (cmd == "tabclose" || cmd == "tabonly") {
        if (cmd == "tabclose" && a.tabs.size() == 1) {
            err = tr(U"E784: Cannot close last tab page");
            return false;
        }
        int keep = cmd == "tabonly" ? (count > 0 ? count - 1 : a.curTab) : -1;
        int close = cmd == "tabclose" ? (count > 0 ? count - 1 : a.curTab) : -1;
        if (keep >= int(a.tabs.size()) || close >= int(a.tabs.size())) {
            err = tr(U"E16: Invalid range");
            return false;
        }
        std::vector<Tab*> victims;
        for (size_t t = 0; t < a.tabs.size(); t++)
            if ((close >= 0 && int(t) == close) || (keep >= 0 && int(t) != keep)) victims.push_back(a.tabs[t].get());
        if (!bang)
            for (Tab* t : victims) {
                std::vector<Win*> ws;
                collect(t->root.get(), ws);
                for (Win* w : ws) {
                    bool elsewhere = false;
                    for (size_t o = 0; o < a.tabs.size(); o++)
                        if (std::find(victims.begin(), victims.end(), a.tabs[o].get()) == victims.end())
                            for (Win* x : a.windows(int(o))) elsewhere |= x->doc == w->doc;
                    if (a.textOf(w->doc).modified && !elsewhere) {
                        err = tr(U"E445: Other window contains changes");
                        return false;
                    }
                }
            }
        for (Tab* t : victims) {
            for (size_t i = 0; i < a.tabs.size(); i++)
                if (a.tabs[i].get() == t) { a.closeTab(int(i), true); break; }
        }
        return true;
    }
    if (cmd == "tabnext") {
        if (count > 0) {
            if (count > int(a.tabs.size())) return false;
            a.gotoTab(count - 1);
        } else {
            a.gotoTab(a.curTab + 1);
        }
        return true;
    }
    if (cmd == "tabprev") {
        a.gotoTab(a.curTab - std::max(1, count));
        return true;
    }
    if (cmd == "tabfirst") { a.gotoTab(0); return true; }
    if (cmd == "tablast") { a.gotoTab(int(a.tabs.size()) - 1); return true; }
    if (cmd == "tabmove") {
        int n = int(a.tabs.size());
        int to;
        if (!arg.empty() && (arg[0] == '+' || arg[0] == '-')) {
            int d = arg.size() > 1 ? std::stoi(toUtf8(arg.substr(1))) : 1;
            to = a.curTab + (arg[0] == '+' ? d : -d);
        } else if (!arg.empty()) {
            to = std::stoi(toUtf8(arg));  // after tab page N (0 = first)
            to = to > a.curTab ? to - 1 : to;
        } else {
            to = n - 1;
        }
        if (to < 0 || to >= n) {
            err = tr(U"E474: Invalid argument");
            return false;
        }
        std::unique_ptr<Tab> t = std::move(a.tabs[size_t(a.curTab)]);
        a.tabs.erase(a.tabs.begin() + a.curTab);
        a.tabs.insert(a.tabs.begin() + to, std::move(t));
        a.curTab = to;
        return true;
    }
    if (cmd == "tabs") {
        a.saveView();
        std::vector<u32> out;
        for (size_t t = 0; t < a.tabs.size(); t++) {
            out.push_back(fmtU(trs("Tab page %d").c_str(), int(t) + 1));
            for (Win* w : a.windows(int(t))) {
                Editor& e = a.textOf(w->doc);
                u32 nm = e.filename.empty() ? u32(tr(U"[No Name]")) : U(e.filename);
                out.push_back(u32(w == a.win ? U"> " : U"  ") + (e.modified ? U"+ " : U"  ") + nm);
            }
        }
        showLines(tr(U"Tab pages"), out);
        return true;
    }
    err = tr(U"E476: Invalid command");
    return false;
}
