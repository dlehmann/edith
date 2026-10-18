// Message boxes, the file dialog, find/replace and the other dialogs,
// plus opening and saving files.

#include "app.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include "i18n.h"
#include "languages.h"
#include "menus.h"
#include "paths.h"
#include "search.h"
#include "utf8.h"
#include "widgets.h"

int App::msgbox(const u32& title, const u32& text, std::vector<u32> btns, int y) {
    if (btns.empty()) btns.push_back(tr(U"OK"));
    std::vector<u32> lines;
    size_t s = 0;
    for (;;) {
        size_t q = text.find('\n', s);
        lines.push_back(text.substr(s, q == u32::npos ? u32::npos : q - s));
        if (q == u32::npos) break;
        s = q + 1;
    }
    int tw2 = 0, bw = 0;
    for (auto& l : lines) tw2 = std::max(tw2, strWidth(l));
    for (auto& b : btns) bw += int(b.size()) + 6;
    int w = std::min(scr.w - 2, std::max({tw2 + 6, bw + 4, int(title.size()) + 6, 30}));
    int h = int(lines.size()) + 6;
    Dialog d(scr, title, w, h);
    d.y = y;
    d.background = [this] { drawAll(); };
    for (size_t i = 0; i < lines.size(); i++) {
        int lw = strWidth(lines[i]);
        d.add<Label>(std::max(2, (w - lw) / 2), 2 + int(i), lines[i]);
    }
    std::vector<std::pair<u32, int>> bs;
    for (size_t i = 0; i < btns.size(); i++) bs.push_back({btns[i], int(i) + 1});
    d.addButtons(bs);
    d.def = 1;
    int r = d.run();
    return r > 0 ? r - 1 : -1;
}

void App::error(const u32& text) { msgbox(tr(U"Error"), text); }

std::string App::fileDialog(const u32& title, const std::string& initName) {
    std::string cwd = ed.filename.empty() ? getCwd() : dirOf(ed.filename);
    int W = std::min(72, scr.w - 4), H = std::min(22, scr.h - 2);
    Dialog d(scr, title, W, H);
    d.background = [this] { drawAll(); };
    // the input field starts after the label (translations differ in length)
    int ix = 2 + strWidth(tr(U"File Name:")) + 2;
    d.add<Label>(2, 2, tr(U"File Name:"));
    Input* inp = d.add<Input>(ix, 2, W - ix - 3, U(initName));
    Label* pathLbl = d.add<Label>(2, 4, u32());
    ListBox* lb = d.add<ListBox>(2, 5, W - 4, H - 8, true);
    std::vector<std::string> names;
    std::vector<bool> isDir;

    auto refresh = [&] {
        names.clear();
        isDir.clear();
        std::vector<std::string> dirs, files;
        if (DIR* dp = opendir(cwd.c_str())) {
            while (dirent* de = readdir(dp)) {
                std::string nm = de->d_name;
                if (nm == "." || nm == "..") continue;
                struct stat st;
                bool dir = stat((cwd + "/" + nm).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
                (dir ? dirs : files).push_back(nm);
            }
            closedir(dp);
        }
        auto ci = [](const std::string& a, const std::string& b) {
            bool ha = !a.empty() && a[0] == '.', hb = !b.empty() && b[0] == '.';
            if (ha != hb) return hb;  // hidden entries go last
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        };
        std::sort(dirs.begin(), dirs.end(), ci);
        std::sort(files.begin(), files.end(), ci);
        std::vector<u32> items;
        if (cwd != "/") { names.push_back(".."); isDir.push_back(true); items.push_back(U"../"); }
        for (auto& s : dirs) { names.push_back(s); isDir.push_back(true); items.push_back(U(s) + U"/"); }
        for (auto& s : files) { names.push_back(s); isDir.push_back(false); items.push_back(U(s)); }
        lb->setItems(items);
        u32 p = U(cwd);
        int maxw = W - 4;
        if (int(p.size()) > maxw) p = U"…" + p.substr(p.size() - maxw + 1);
        pathLbl->text = p;
    };
    lb->onChange = [&](int i) {
        if (i < int(names.size()) && !isDir[i]) inp->set(U(names[i]));
    };
    lb->onActivate = [&](int i) -> int {
        if (i >= int(names.size())) return 0;
        if (isDir[i]) {
            cwd = normPath(cwd + "/" + names[i]);
            refresh();
            return 0;
        }
        inp->set(U(names[i]));
        return 1;
    };
    d.addButtons({{tr(U"OK"), 1}, {tr(U"Cancel"), -1}});
    d.def = 1;
    refresh();
    for (;;) {
        if (d.run() != 1) return "";
        std::string name = toUtf8(inp->text);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name[0] == ' ') name.erase(0, 1);
        if (name.empty()) continue;
        std::string p;
        if (name[0] == '~') p = absPath(name);
        else p = normPath(name[0] == '/' ? name : cwd + "/" + name);
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            cwd = p;
            inp->set(u32());
            refresh();
            d.focusOn(lb);
            continue;
        }
        return p;
    }
}

bool App::confirmDiscardAll() {
    for (size_t i = 0; i < docs.size(); i++) {
        Doc* d = docs[i].get();
        if (!textOf(d).modified) continue;
        // show the buffer before asking
        for (size_t t = 0; t < tabs.size() && win->doc != d; t++)
            for (Win* w : windows(int(t)))
                if (w->doc == d) { activate(w); break; }
        ed.scrollTo(th(), tw());
        int r = msgbox(U"Edith", tr(U"The file has been modified.\nDo you want to save the changes?"),
                       {tr(U"Yes"), tr(U"No"), tr(U"Cancel")});
        if (r == 0 && !doSave()) return false;
        if (r != 0 && r != 1) return false;
    }
    return true;
}

bool App::confirmDiscard() {
    if (!ed.modified || shown(&doc(), win)) return true;
    int r = msgbox(U"Edith", tr(U"The file has been modified.\nDo you want to save the changes?"),
                   {tr(U"Yes"), tr(U"No"), tr(U"Cancel")});
    if (r == 0) return doSave();
    return r == 1;
}

bool App::writeTo(const std::string& path) {
    std::string err;
    bool conv;
    if (!ed.save(path, err, conv)) {
        error(tr(U"The file could not be saved:\n") + U(err));
        return false;
    }
    if (conv)
        msgbox(tr(U"Notice"),
               tr(U"The file contained characters outside of Latin-1\nand was therefore saved as UTF-8."));
    return true;
}

bool App::doSave() {
    if (ed.filename.empty()) return doSaveAs();
    return writeTo(ed.filename);
}

bool App::doSaveAs() {
    std::string p = fileDialog(tr(U"Save As"), ed.filename.empty() ? "" : baseOf(ed.filename));
    if (p.empty()) return false;
    if (p != ed.filename && fileExists(p)) {
        if (msgbox(tr(U"Save As"), U(baseOf(p)) + tr(U" already exists.\nDo you want to overwrite it?"),
                   {tr(U"Yes"), tr(U"No")}) != 0)
            return false;
    }
    if (!writeTo(p)) return false;
    autoLang();
    return true;
}

void App::openPath(const std::string& p) {
    doc().langManual = false;
    if (!fileExists(p)) {
        ed = Editor();
        ed.filename = p;
        autoLang();
        vim.reset();
        return;
    }
    std::string err;
    Editor e;
    if (!e.load(p, err)) {
        error(tr(U"The file could not be opened:\n") + U(err));
        return;
    }
    ed = std::move(e);
    autoLang();
    vim.reset();
}

void App::doOpen() {
    if (!confirmDiscard()) return;
    std::string p = fileDialog(tr(U"Open"), "");
    if (p.empty()) return;
    if (!fileExists(p) && msgbox(tr(U"Open"), U(baseOf(p)) + tr(U" does not exist.\nDo you want to create a new file?"),
                                 {tr(U"Yes"), tr(U"No")}) != 0)
        return;
    if (shown(&doc(), win)) useNewDoc();  // the other window keeps its file
    openPath(p);
}

void App::openFiles(const std::vector<std::string>& paths, char how) {
    if (paths.empty()) return;
    if (how == 't') {
        for (size_t i = 0; i < paths.size(); i++) {
            if (i > 0) newTab(nullptr);
            openPath(paths[i]);
        }
        gotoTab(0);
        return;
    }
    // splits: the new window appears above / left, so the files are opened from the last one
    openPath(paths.back());
    for (size_t i = paths.size() - 1; i-- > 0;) {
        if (!splitWin(how == 'O')) break;
        useNewDoc();
        openPath(paths[i]);
    }
    equalize(tab().root.get());
    layoutAll();
}

void App::findDialog() {
    u32 init = findText;
    Pos a, b;
    if (ed.sel(a, b) && a.y == b.y) init = ed.getText(a, b);
    int w = std::min(64, scr.w - 4);
    Dialog d(scr, tr(U"Find"), w, 10);
    d.background = [this] { drawAll(); };
    int ix = std::max(16, 2 + strWidth(tr(U"Find What:")) + 2);
    d.add<Label>(2, 1, tr(U"Find What:"));
    Input* inp = d.add<Input>(ix, 1, w - ix - 3, init);
    Check* c1 = d.add<Check>(2, 3, tr(U"Match Upper/Lowercase"), findCase);
    Check* c2 = d.add<Check>(2, 4, tr(U"Whole Word"), findWhole);
    Check* c3 = d.add<Check>(2, 5, tr(U"Regular Expression"), findRegex);
    d.addButtons({{tr(U"OK"), 1}, {tr(U"Cancel"), -1}});
    d.def = 1;
    if (d.run() != 1 || inp->text.empty()) return;
    findText = inp->text;
    findCase = c1->val;
    findWhole = c2->val;
    findRegex = c3->val;
    findNext();
}

bool App::compileFind(VimRegex& rx) {
    u32 err;
    if (compileSearch(rx, findText, findCase, findWhole, err)) return true;
    msgbox(tr(U"Find"), err);
    return false;
}

bool App::findMatch(const VimRegex& rx, Pos from, RxMatch& m) {
    if (findRegex) return findRx(ed, rx, from, m);
    return ed.find(findText, findCase, findWhole, from, m.s, m.e);
}

bool App::findNext() {
    if (findText.empty()) { findDialog(); return false; }
    VimRegex rx;
    if (findRegex && !compileFind(rx)) return false;
    RxMatch m;
    if (!findMatch(rx, {ed.cy, ed.cx}, m)) {
        msgbox(tr(U"Find"), tr(U"Match not found."));
        return false;
    }
    Pos s = m.s, e = m.e;
    ed.anch = true;
    ed.anchor = s;
    ed.cy = e.y; ed.cx = e.x;
    ed.lastEdit = 0; ed.wantCol = -1;
    ed.scrollTo(th(), tw(), true);
    return true;
}

void App::replaceDialog() {
    int w = std::min(70, scr.w - 4);
    Dialog d(scr, tr(U"Change"), w, 12);
    d.background = [this] { drawAll(); };
    u32 init = findText;
    Pos a, b;
    if (ed.sel(a, b) && a.y == b.y) init = ed.getText(a, b);
    int ix = std::max({16, 2 + strWidth(tr(U"Find What:")) + 2, 2 + strWidth(tr(U"Change To:")) + 2});
    d.add<Label>(2, 1, tr(U"Find What:"));
    Input* f = d.add<Input>(ix, 1, w - ix - 3, init);
    d.add<Label>(2, 3, tr(U"Change To:"));
    Input* r = d.add<Input>(ix, 3, w - ix - 3, replText);
    Check* c1 = d.add<Check>(2, 5, tr(U"Match Upper/Lowercase"), findCase);
    Check* c2 = d.add<Check>(2, 6, tr(U"Whole Word"), findWhole);
    Check* c3 = d.add<Check>(2, 7, tr(U"Regular Expression"), findRegex);
    d.addButtons({{tr(U"Find and Verify"), 1}, {tr(U"Change All"), 2}, {tr(U"Cancel"), -1}});
    d.def = 1;
    int res = d.run();
    if ((res != 1 && res != 2) || f->text.empty()) return;
    findText = f->text;
    replText = r->text;
    findCase = c1->val;
    findWhole = c2->val;
    findRegex = c3->val;
    VimRegex rx;
    if (findRegex && !compileFind(rx)) return;

    if (res == 2) {
        int cnt = findRegex ? replaceAllRx(ed, rx, replText) : ed.replaceAll(findText, replText, findCase, findWhole);
        msgbox(tr(U"Change"), cnt ? fmtU(trs("%d occurrence(s) changed.").c_str(), cnt) : u32(tr(U"Match not found.")));
        return;
    }
    // with confirmation
    int cnt = 0;
    bool any = false, wrapped = false;
    Pos start{ed.cy, ed.cx}, prev = start;
    for (;;) {
        RxMatch m;
        if (!findMatch(rx, {ed.cy, ed.cx}, m)) break;
        Pos s = m.s, e = m.e;
        if (s < prev) wrapped = true;
        if (wrapped && !(s < start)) break;
        prev = s;
        any = true;
        ed.anch = true;
        ed.anchor = s;
        ed.cy = e.y; ed.cx = e.x;
        ed.scrollTo(th(), tw(), true);
        int row = ty() + s.y - ed.top;
        int dy = row > scr.h / 2 ? 2 : scr.h - 9;
        int ans =
            msgbox(tr(U"Change"), tr(U"Change this occurrence?"), {tr(U"Change"), tr(U"Skip"), tr(U"Cancel")}, dy);
        if (ans == 0) {
            u32 rep = findRegex ? expandReplacement(ed, m, replText) : replText;
            ed.checkpoint(0);
            ed.anch = false;
            ed.deleteRange(s, e);
            ed.cy = s.y;
            ed.cx = s.x;
            ed.insert(rep);  // the cursor ends after the replacement
            ed.modified = true;
            // the start moves with the text when the change was before it
            if (e.y < start.y) start.y += ed.cy - e.y;
            else if (s.y == start.y && e.y == s.y && ed.cy == s.y && s.x < start.x) start.x += ed.cx - e.x;
            prev = {ed.cy, ed.cx};
            cnt++;
            if (s == e) {  // an empty match: go on after the next character
                if (ed.cx < int(ed.lines[size_t(ed.cy)].size())) ed.cx++;
                else if (ed.cy + 1 < ed.n()) { ed.cy++; ed.cx = 0; }
                else break;
            }
        } else if (ans == 1) {
            ed.anch = false;
            ed.cy = e.y; ed.cx = e.x;  // an empty match at the cursor is skipped by findRx
        } else {
            ed.anch = false;
            return;
        }
    }
    ed.anch = false;
    msgbox(tr(U"Change"), any ? fmtU(trs("%d occurrence(s) changed.").c_str(), cnt) : u32(tr(U"Match not found.")));
}

int App::numberDialog(const u32& title, const u32& label, int val) {
    Dialog d(scr, title, std::max(40, strWidth(label) + 18), 7);
    d.background = [this] { drawAll(); };
    d.add<Label>(2, 2, label);
    Input* inp = d.add<Input>(strWidth(label) + 3, 2, 10, fmtU("%d", val));
    d.addButtons({{tr(U"OK"), 1}, {tr(U"Cancel"), -1}});
    d.def = 1;
    if (d.run() != 1) return -1;
    std::string s = toUtf8(inp->text);
    char* end;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return -1;
    return int(std::clamp<long>(v, 0, INT_MAX));
}

void App::gotoDialog() {
    int v = numberDialog(tr(U"Go to Line"), tr(U"Line number:"), ed.cy + 1);
    if (v < 1) return;
    ed.setCursor(v - 1, 0, false);
    ed.scrollTo(th(), tw(), true);
}

void App::langDialog() {
    int w = std::min(40, scr.w - 4), h = std::min(scr.h - 2, 22);
    Dialog d(scr, tr(U"Language"), w, h);
    d.background = [this] { drawAll(); };
    ListBox* lb = d.add<ListBox>(1, 1, w - 2, h - 4, true);
    std::vector<u32> items{tr(U"Detect automatically"), tr(U"None (plain text)")};
    for (int i = 0; i < NLANGS; i++) items.push_back(U(LANGS[i].name));
    lb->setItems(items);
    lb->sel = doc().langManual ? (doc().lang < 0 ? 1 : doc().lang + 2) : 0;
    lb->onActivate = [](int) { return 1; };
    d.addButtons({{tr(U"OK"), 1}, {tr(U"Cancel"), -1}});
    d.def = 1;
    if (d.run() != 1) return;
    if (lb->sel == 0) {
        doc().langManual = false;
        autoLang();
    } else {
        doc().langManual = true;
        setLang(lb->sel - 2);
    }
    S.syntax = true;
}

void App::linesDialog(const u32& title, const std::vector<u32>& lines) {
    int wid = 0;
    for (auto& l : lines) wid = std::max(wid, strWidth(l));
    int w = std::min(scr.w - 4, std::max(40, wid + 6));
    int h = std::min(scr.h - 2, int(lines.size()) + 7);
    Dialog d(scr, title, w, h);
    d.background = [this] { drawAll(); };
    ListBox* lb = d.add<ListBox>(1, 1, w - 2, h - 4, false);
    lb->setItems(lines);
    d.addButtons({{tr(U"Close"), 1}});
    d.def = 1;
    d.focusOn(lb);
    d.run();
}

void App::helpDialog() {
    int longest = 0;
    for (auto& l : helpText()) longest = std::max(longest, strWidth(l));
    int w = std::min(longest + 6, scr.w - 4);
    int h = std::min(scr.h - 2, int(helpText().size()) + 7);
    Dialog d(scr, tr(U"Help: Keyboard and Mouse"), w, h);
    d.background = [this] { drawAll(); };
    ListBox* lb = d.add<ListBox>(1, 1, w - 2, h - 4, false);
    lb->setItems(helpText());
    d.addButtons({{tr(U"Close"), 1}});
    d.def = 1;
    d.focusOn(lb);
    d.run();
}
