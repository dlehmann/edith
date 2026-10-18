// Reformat Code: the standard formatter of the language, or the built-in formatting.

#include "app.h"

#include <algorithm>
#include <climits>

#include "format.h"
#include "i18n.h"
#include "languages.h"
#include "paths.h"
#include "process.h"
#include "utf8.h"

void App::notify(const u32& m, bool err) {
    if (S.vim) {
        if (err) vim.error(m);
        else vim.message(m);
        vim.msgKeep = false;
        return;
    }
    statusMsg = m;
    statusErr = err;
}

bool App::reindentLines(int y1, int y2, std::vector<int>& widths) {
    Doc& d = doc();
    std::string lname = d.lang >= 0 ? LANGS[d.lang].name : "";
    consumeDirty();
    if (d.tsd.L && d.tsd.indents(ed.lines, y1, y2, S.tabw, S.tabw, widths)) return true;
    // braces for the C family and similar languages
    if (d.lang >= 0 && LANGS[d.lang].kind == LK_CODE &&
        std::string(LANGS[d.lang].lineCmt).find("//") != std::string::npos) {
        widths = braceIndents(ed.lines, y1, y2, S.tabw, S.tabw);
        return true;
    }
    return false;
}

static u32 makeIndentStr(int w) {
    w = std::max(0, w);
    if (S.expandtab) return u32(size_t(w), ' ');
    return u32(size_t(w / S.tabw), '\t') + u32(size_t(w % S.tabw), ' ');
}

// Without a formatter: JSON is formatted, other languages are re-indented (unless their
// indentation has a meaning) and lose white space at the ends of lines.
std::vector<u32> App::builtinFormat(u32& msg, bool& reindented) {
    Doc& d = doc();
    std::string lname = d.lang >= 0 ? LANGS[d.lang].name : "";
    std::vector<u32> out = ed.lines;
    reindented = false;
    std::vector<int> widths;
    bool sig = lname.empty() || indentSignificant(lname);
    if (!sig && reindentLines(0, ed.n() - 1, widths)) reindented = true;
    bool tsOk = d.tsd.L && d.tsd.tree;
    for (int y = 0; y < ed.n(); y++) {
        u32 l = ed.lines[size_t(y)];
        // trailing white space (not in Markdown, where two spaces are a line break, and not in strings)
        size_t e = l.size();
        while (e > 0 && (l[e - 1] == ' ' || l[e - 1] == '\t')) e--;
        if (e < l.size() && lname != "Markdown") {
            bool str = false;
            if (tsOk) str = d.tsd.insideString(uint32_t(y), uint32_t(toUtf8(l.substr(0, e)).size()));
            if (!str) l.resize(e);
        }
        if (reindented && size_t(y) < widths.size() && widths[size_t(y)] >= 0) {
            size_t i = 0;
            while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
            l = i == l.size() ? u32() : makeIndentStr(widths[size_t(y)]) + l.substr(i);
        }
        out[size_t(y)] = l;
    }
    (void)msg;
    return out;
}

void App::reformatCode() {
    if (S.vim) vim.leaveWindow();
    Doc& d = doc();
    std::string lname = d.lang >= 0 ? LANGS[d.lang].name : "";
    std::string suggestion;
    FormatTool tool;
    if (!lname.empty()) tool = findFormatter(lname, ed.filename, S.tabw, S.expandtab, &suggestion);
    std::vector<u32> out;
    u32 msg;
    if (!tool.command.empty()) {
        std::string input;
        for (int y = 0; y < ed.n(); y++) {
            if (y) input += '\n';
            input += toUtf8(ed.lines[size_t(y)]);
        }
        std::string o, e;
        int st;
        std::string cwd = ed.filename.empty() ? getCwd() : dirOf(ed.filename);
        bool ran = runCommand(tool.command, cwd, input, 20000, o, e, st);
        u32 prog = U(tool.program);
        if (!ran || st != 0 || (o.empty() && !input.empty())) {
            u32 head = !ran        ? tr(U"The formatter %s did not finish in time.")
                       : st == 127 ? tr(U"The formatter %s could not be started.")
                                   : tr(U"The formatter %s reported an error:");
            size_t p = head.find(U"%s");
            if (p != u32::npos) head.replace(p, 2, prog);
            std::vector<u32> errLines;
            u32 et = U(e);
            size_t s = 0;
            while (s < et.size() && errLines.size() < 12) {
                size_t q = et.find('\n', s);
                if (q == u32::npos) q = et.size();
                u32 l = et.substr(s, q - s);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                if (!l.empty()) errLines.push_back(l.substr(0, size_t(std::max(20, scr.w - 12))));
                s = q + 1;
            }
            if (S.vim) {
                // the status line is narrow: "program: first line of the error"
                notify(errLines.empty() ? head : prog + U": " + errLines[0], true);
            } else {
                u32 t = head;
                for (auto& l : errLines) t += U"\n" + l;
                error(t);
            }
            return;
        }
        u32 ot = U(o);
        size_t s = 0;
        for (;;) {
            size_t q = ot.find('\n', s);
            u32 l = ot.substr(s, q == u32::npos ? u32::npos : q - s);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            out.push_back(l);
            if (q == u32::npos) break;
            s = q + 1;
        }
        msg = tr(U"Formatted with ") + prog;
    } else if (tool.builtin) {
        u32 err;
        if (!formatJson(ed.lines, S.tabw, !S.expandtab, out, err)) {
            notify(err, true);
            return;
        }
        msg = tr(U"Formatted with the built-in JSON formatter");
    } else {
        bool reind;
        out = builtinFormat(msg, reind);
        // short messages: the status line is narrow
        msg = reind ? tr(U"Re-indented") : tr(U"Indentation kept");
        if (!suggestion.empty()) msg += tr(U" (full formatting with ") + U(suggestion) + U")";
    }
    if (out.empty()) out.push_back(u32());
    if (out == ed.lines) {
        notify(tr(U"Already formatted"), false);
        return;
    }
    // apply as one change, keep the cursor in its line and at its place in the text
    int cy = std::min(ed.cy, int(out.size()) - 1);
    auto lead = [](const u32& l) {
        size_t i = 0;
        while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
        return int(i);
    };
    int rel = ed.cx - lead(ed.lines[size_t(ed.cy)]);
    int top = ed.top;
    if (S.vim) {
        vim.beginChange();
        vim.replaceLines(0, ed.n() - 1, out);
        vim.endChange();
    } else {
        ed.checkpoint(0);
        ed.lines = out;
        ed.touch(0);
        ed.modified = true;
        ed.anch = false;
    }
    ed.cy = cy;
    ed.cx = std::clamp(lead(ed.lines[size_t(cy)]) + std::max(0, rel), 0, int(ed.lines[size_t(cy)].size()));
    ed.top = top;
    if (S.vim) vim.afterExternal();
    ed.scrollTo(th(), tw());
    notify(msg, false);
}

bool AppVimHost::reindent(int y1, int y2, std::vector<int>& widths) { return app->reindentLines(y1, y2, widths); }

void AppVimHost::format() { app->reformatCode(); }
