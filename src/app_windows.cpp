// Buffers, split windows and their tabs.
//
// The screen has a layout tree of rows and columns whose leaves are windows. Every window has
// its own tabs, and every tab shows a buffer (Doc); several tabs can show the same buffer (Vim's
// :split). The buffer of the active tab is checked out into App::ed, so that the editing code
// and the Vim engine always work on App::ed; switching to a tab of another buffer swaps the
// texts (cheap moves).

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
    root = std::make_unique<Node>();
    root->win = std::make_unique<Win>();
    win = root->win.get();
    win->setDoc(docs[0].get());
    layoutAll();
}

App::~App() { g_clipboardPaste = nullptr; }

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

// The smallest size of a node: a window needs its tab line, one text line and one column
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

void App::layoutNode(Node* n, int x, int y, int w, int h) {
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    if (n->kind == Node::LEAF) {
        // the first row is the tab line of the window
        Win& wi = *n->win;
        wi.x = x;
        wi.y = y + 1;
        wi.w = std::max(1, w);
        wi.h = std::max(1, h - 1);
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
        if (rows) layoutNode(n->kids[size_t(i)].get(), x, pos, w, sz[size_t(i)]);
        else layoutNode(n->kids[size_t(i)].get(), pos, y, sz[size_t(i)], h);
        pos += sz[size_t(i)] + (rows ? 0 : 1);
    }
}

void App::layoutAll() {
    // the windows fill the frame from its top line (the first tab line) to the line above the bottom
    layoutNode(root.get(), 1, 1, std::max(1, scr.w - 2), std::max(2, scr.h - 3));
}

static void collect(Node* n, std::vector<Win*>& out) {
    if (n->kind == Node::LEAF) {
        out.push_back(n->win.get());
        return;
    }
    for (auto& c : n->kids) collect(c.get(), out);
}

std::vector<Win*> App::windows() const {
    std::vector<Win*> r;
    collect(root.get(), r);
    return r;
}

static Node* findNode(Node* n, Win* w) {
    if (n->kind == Node::LEAF) return n->win.get() == w ? n : nullptr;
    for (auto& c : n->kids)
        if (Node* f = findNode(c.get(), w)) return f;
    return nullptr;
}

Node* App::nodeOf(Win* w) const { return findNode(root.get(), w); }

int App::refs(Doc* d) const {
    int n = 0;
    for (Win* w : windows())
        for (size_t t = 0; t < w->tabs.size(); t++) n += w->docOf(int(t)) == d;
    return n;
}

bool App::shown(Doc* d, Win* except) const { return refs(d) > (except && except->doc == d ? 1 : 0); }

// ---------------------------------------------------------------------------
// Switching windows and tabs
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

// Shows tab t of a window that is not active: the state of its shown tab goes into tabs[cur]
static void showStored(Win* w, int t) {
    if (t == w->cur) return;
    w->tabs[size_t(w->cur)] = Tab{w->doc, w->v, w->vim};
    w->cur = t;
    Tab& n = w->tabs[size_t(t)];
    w->doc = n.doc;
    w->v = n.v;
    w->vim = n.vim;
}

void App::activate(Win* w, int t) {
    if (!w) return;
    if (t < 0 || t >= int(w->tabs.size())) t = w->cur;
    if (w == win && t == w->cur) return;
    if (S.vim) vim.leaveWindow();
    saveView();
    vim.swapWinState(win->vim);
    Doc* od = win->doc;
    Doc* nd = w->docOf(t);
    if (od != nd) {
        consumeDirty();
        std::swap(ed, od->store);
        vim.swapBufState(od->vim);
        std::swap(ed, nd->store);
        vim.swapBufState(nd->vim);
    }
    showStored(w, t);
    if (w != win) prev = win;
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

void App::dropDoc(Doc* d) {
    if (d == win->doc || refs(d) > 0) return;
    for (size_t i = 0; i < docs.size(); i++)
        if (docs[i].get() == d) { docs.erase(docs.begin() + long(i)); break; }
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
    win->setDoc(nd);
    vim.reset();
    dropDoc(od);
}

// ---------------------------------------------------------------------------
// Splitting and closing
// ---------------------------------------------------------------------------

bool App::splitWin(bool vertical, bool empty, u32* err) {
    // both windows need room for their tab lines and at least one line / a few columns
    if ((!vertical && win->h + 1 < 4) || (vertical && win->w < 7) || scr.w < 40 || scr.h < 10) {
        if (err) *err = tr(U"E36: Not enough room");
        return false;
    }
    saveView();
    Node* ln = nodeOf(win);
    int kind = vertical ? Node::COLS : Node::ROWS;
    auto nw = std::make_unique<Win>();
    if (empty) {
        nw->setDoc(newDoc());
    } else {
        // Vim's :split: the same buffer in a window with one tab
        nw->setDoc(win->doc);
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

bool App::askSave() {
    ed.scrollTo(th(), tw());
    int r = msgbox(U"Edith", tr(U"The file has been modified.\nDo you want to save the changes?"),
                   {tr(U"Yes"), tr(U"No"), tr(U"Cancel")});
    if (r == 0) return doSave();
    return r == 1;
}

bool App::confirmCloseTab(Win* w, int t) {
    Doc* d = w->docOf(t);
    if (!textOf(d).modified || refs(d) > 1) return true;
    activate(w, t);
    return askSave();
}

bool App::confirmCloseWin(Win* w) {
    for (size_t t = 0; t < w->tabs.size(); t++) {
        Doc* d = w->docOf(int(t));
        if (!textOf(d).modified) continue;
        // only once for a buffer in several of its tabs, not at all when another window shows it
        int mine = 0;
        bool first = true;
        for (size_t o = 0; o < w->tabs.size(); o++) {
            if (w->docOf(int(o)) != d) continue;
            mine++;
            if (o < t) first = false;
        }
        if (!first || refs(d) > mine) continue;
        activate(w, int(t));
        if (!askSave()) return false;
    }
    return true;
}

void App::removeTab(Win* w, int t) {
    if (w->tabs.size() == 1) {
        removeWin(w);
        return;
    }
    Doc* d = w->docOf(t);
    if (t == w->cur) {
        // like Vim's :tabclose: the tab on the right becomes the current one
        int n = size_t(t) + 1 < w->tabs.size() ? t + 1 : t - 1;
        if (w == win) activate(w, n);
        else showStored(w, n);
    }
    w->tabs.erase(w->tabs.begin() + t);
    if (w->cur > t) w->cur--;
    dropDoc(d);
    layoutAll();
}

void App::removeWin(Win* w) {
    std::vector<Win*> ws = windows();
    if (ws.size() == 1) return;
    if (w == win) {
        Win* next = prev && prev != w && nodeOf(prev) ? prev : nullptr;
        if (!next) {
            size_t i = std::find(ws.begin(), ws.end(), w) - ws.begin();
            next = ws[i + 1 < ws.size() ? i + 1 : i - 1];
        }
        activate(next);
    }
    if (prev == w) prev = nullptr;
    std::vector<Doc*> ds;
    for (size_t t = 0; t < w->tabs.size(); t++) ds.push_back(w->docOf(int(t)));
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
    for (Doc* d : ds) dropDoc(d);
    layoutAll();
}

bool App::closeWin(Win* w, bool force) {
    if (!force && !confirmCloseWin(w)) return false;
    if (windows().size() == 1) {
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
        bool lost = false;
        for (size_t t = 0; t < w->tabs.size() && !force; t++) {
            Doc* d = w->docOf(int(t));
            if (!textOf(d).modified) continue;
            int mine = 0;
            for (size_t o = 0; o < w->tabs.size(); o++) mine += w->docOf(int(o)) == d;
            lost |= refs(d) == mine;
        }
        if (lost) {
            all = false;
            continue;
        }
        removeWin(w);
    }
    return all;
}

void App::newTab(Doc* d) {
    saveView();
    Tab t;
    t.doc = d ? d : newDoc();
    if (d == win->doc) t.v = win->v;
    win->tabs.insert(win->tabs.begin() + win->cur + 1, t);
    activate(win, win->cur + 1);
}

bool App::closeTab(Win* w, int t, bool force) {
    if (!force && !confirmCloseTab(w, t)) return false;
    if (w->tabs.size() == 1 && windows().size() == 1) {
        quit = true;
        return true;
    }
    removeTab(w, t);
    return true;
}

void App::gotoTab(int t) {
    int n = int(win->tabs.size());
    activate(win, ((t % n) + n) % n);
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
            // the rectangles include the tab lines
            int oy1 = o->y - 1, oy2 = o->y + o->h - 1, wy1 = w->y - 1, wy2 = w->y + w->h - 1;
            bool adj;
            int dist;
            switch (dir) {
                case 'h':
                    adj = o->x + o->w + 1 == w->x;
                    dist = row < oy1 ? oy1 - row : row > oy2 ? row - oy2 : 0;
                    break;
                case 'l':
                    adj = w->x + w->w + 1 == o->x;
                    dist = row < oy1 ? oy1 - row : row > oy2 ? row - oy2 : 0;
                    break;
                case 'k':
                    adj = oy2 + 1 == wy1;
                    dist = col < o->x ? o->x - col : col >= o->x + o->w ? col - o->x - o->w + 1 : 0;
                    break;
                default:
                    adj = wy2 + 1 == oy1;
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
    int extra = vertical ? 0 : 1;  // a height counts the text lines, not the tab line
    int target = relative ? cur + size : (size == INT_MAX ? INT_MAX / 2 : size + extra);
    setKidSize(p, idx, target);
    layoutAll();
}

void App::equalize(Node* n) {
    n->weight = 1;
    for (auto& c : n->kids) equalize(c.get());
}

void App::moveToNewTab() {
    std::vector<Win*> ws = windows();
    if (ws.size() < 2) return;
    Win* old = win;
    Win* to = prev && prev != old && nodeOf(prev) ? prev : ws[ws.front() == old ? 1 : 0];
    activate(to);
    // old is not active now: its shown tab is saved in doc, v and vim
    old->tabs[size_t(old->cur)] = Tab{old->doc, old->v, old->vim};
    int at = int(to->tabs.size()) + old->cur;
    to->tabs.insert(to->tabs.end(), old->tabs.begin(), old->tabs.end());
    removeWin(old);
    activate(to, at);
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
        if (!a.splitWin(v, cmd == "new" || cmd == "vnew", &err)) return false;
        openArg();
        if (count > 0) a.resizeWin(v, count, false);
        return true;
    }
    if (cmd == "newbuf") {
        a.useNewDoc();
        return true;
    }
    if (cmd == "close") {
        // the window with all its tabs
        if (a.windows().size() == 1) {
            err = tr(U"E444: Cannot close last window");
            return false;
        }
        if (!bang)
            for (size_t t = 0; t < a.win->tabs.size(); t++) {
                Doc* d = a.win->docOf(int(t));
                int mine = 0;
                for (size_t o = 0; o < a.win->tabs.size(); o++) mine += a.win->docOf(int(o)) == d;
                if (a.textOf(d).modified && a.refs(d) == mine) {
                    a.activate(a.win, int(t));
                    err = tr(U"E37: No write since last change (add ! to override)");
                    return false;
                }
            }
        a.closeWin(a.win, true);
        return true;
    }
    if (cmd == "quit") {
        // the current tab; the last one closes the window, the very last one exits
        if (!bang && unsaved(&a.doc(), a.win)) {
            err = tr(U"E37: No write since last change (add ! to override)");
            return false;
        }
        a.closeTab(a.win, a.win->cur, true);
        return true;
    }
    if (cmd == "qall") {
        if (!bang)
            for (auto& d : a.docs)
                if (a.textOf(d.get()).modified) {
                    for (Win* w : a.windows())
                        for (size_t t = 0; t < w->tabs.size(); t++)
                            if (w->docOf(int(t)) == d.get()) { a.activate(w, int(t)); goto found; }
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
        if (!a.prev || !a.nodeOf(a.prev)) return false;
        a.activate(a.prev);
        return true;
    }
    if (cmd == "equal") {
        a.equalize(a.root.get());
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
    // Vim's tab pages are the tabs of the current window
    Win* w = a.win;
    int nt = int(w->tabs.size());
    if (cmd == "tabnew") {
        a.newTab(nullptr);
        if (!arg.empty()) a.openPath(absPath(toUtf8(arg)));
        return true;
    }
    if (cmd == "tabclose" || cmd == "tabonly") {
        if (cmd == "tabclose" && nt == 1 && a.windows().size() == 1) {
            err = tr(U"E784: Cannot close last tab page");
            return false;
        }
        int keep = cmd == "tabonly" ? (count > 0 ? count - 1 : w->cur) : -1;
        int close = cmd == "tabclose" ? (count > 0 ? count - 1 : w->cur) : -1;
        if (keep >= nt || close >= nt) {
            err = tr(U"E16: Invalid range");
            return false;
        }
        std::vector<int> victims;
        for (int t = 0; t < nt; t++)
            if (t == close || (keep >= 0 && t != keep)) victims.push_back(t);
        if (!bang)
            for (int t : victims) {
                Doc* d = w->docOf(t);
                int lost = 0;
                for (int o : victims) lost += w->docOf(o) == d;
                if (a.textOf(d).modified && a.refs(d) == lost) {
                    err = tr(U"E445: Other window contains changes");
                    return false;
                }
            }
        for (size_t i = victims.size(); i-- > 0;) a.removeTab(w, victims[i]);
        return true;
    }
    if (cmd == "tabnext") {
        if (count > 0) {
            if (count > nt) return false;
            a.gotoTab(count - 1);
        } else {
            a.gotoTab(w->cur + 1);
        }
        return true;
    }
    if (cmd == "tabprev") {
        a.gotoTab(w->cur - std::max(1, count));
        return true;
    }
    if (cmd == "tabfirst") { a.gotoTab(0); return true; }
    if (cmd == "tablast") { a.gotoTab(nt - 1); return true; }
    if (cmd == "tabmove") {
        int to;
        if (!arg.empty() && (arg[0] == '+' || arg[0] == '-')) {
            int d = arg.size() > 1 ? std::stoi(toUtf8(arg.substr(1))) : 1;
            to = w->cur + (arg[0] == '+' ? d : -d);
        } else if (!arg.empty()) {
            to = std::stoi(toUtf8(arg));  // after tab page N (0 = first)
            to = to > w->cur ? to - 1 : to;
        } else {
            to = nt - 1;
        }
        if (to < 0 || to >= nt) {
            err = tr(U"E474: Invalid argument");
            return false;
        }
        Tab t = w->tabs[size_t(w->cur)];
        w->tabs.erase(w->tabs.begin() + w->cur);
        w->tabs.insert(w->tabs.begin() + to, t);
        w->cur = to;
        return true;
    }
    if (cmd == "tabs") {
        a.saveView();
        std::vector<u32> out;
        std::vector<Win*> ws = a.windows();
        for (size_t i = 0; i < ws.size(); i++) {
            out.push_back(fmtU(trs("Window %d").c_str(), int(i) + 1));
            for (size_t t = 0; t < ws[i]->tabs.size(); t++) {
                Editor& e = a.textOf(ws[i]->docOf(int(t)));
                u32 nm = e.filename.empty() ? u32(tr(U"[No Name]")) : U(e.filename);
                bool cur = ws[i] == a.win && int(t) == a.win->cur;
                out.push_back(u32(cur ? U"> " : U"  ") + (e.modified ? U"+ " : U"  ") + nm);
            }
        }
        showLines(tr(U"Windows and Tabs"), out);
        return true;
    }
    err = tr(U"E476: Invalid command");
    return false;
}
