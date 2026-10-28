// Hex mode: HexBuf (src/hex.cpp), the byte offsets of Editor and hex mode in the application
// (src/app_hex.cpp): binary files, the menu, keys, mouse, Find, Go to, saving and windows.

#include "apptest.h"
#include "app.h"
#include "i18n.h"
#include "hex.h"
#include "menus.h"
#include "test.h"
#include "utf8.h"

// ---- HexBuf and helpers ----

TEST(hex_looks_binary) {
    CHECK(!looksBinary(""));
    CHECK(!looksBinary("int main() {\n\treturn 0;\r\n}\n"));
    CHECK(!looksBinary("Gr\xfc\xdf" "e aus K\xf6ln\n"));             // Latin-1 text
    CHECK(!looksBinary("\x1b[31mred\x1b[0m and \x1b[1mbold\x1b[0m\n"));  // a log with colors
    CHECK(looksBinary(std::string("ELF\0\1\2", 6)));
    CHECK(looksBinary("\x01\x02\x03\x04 abc"));  // many control characters
    std::string big(20000, 'a');
    big[10000] = '\0';  // only the start is looked at
    CHECK(!looksBinary(big));
}

TEST(hex_parse_and_format) {
    std::string b;
    CHECK(parseHex(U"4F 0a", b));
    CHECK_EQ(b, std::string("\x4F\x0A"));
    CHECK(parseHex(U"4f0A", b));
    CHECK_EQ(b, std::string("\x4F\x0A"));
    CHECK(parseHex(U"0x4F, 0x0A", b));
    CHECK_EQ(b, std::string("\x4F\x0A"));
    CHECK(!parseHex(U"4F 0", b));    // an odd number of digits
    CHECK(!parseHex(U"4 F", b));     // a byte split by a space
    CHECK(!parseHex(U"hello", b));
    CHECK(!parseHex(U"", b));
    CHECK(hexText(std::string("\x00\xAB\x7F", 3)) == U"00 AB 7F");
}

TEST(hex_layout) {
    HexLayout l(78, 100);
    CHECK_EQ(l.per, 16);
    CHECK_EQ(l.digits, 8);
    CHECK_EQ(l.hexCol(0), 10);
    CHECK_EQ(l.hexCol(8), 35);  // a gap after eight bytes
    CHECK(l.charCol(15) < 78);
    CHECK_EQ(HexLayout(40, 100).per, 4);
    CHECK_EQ(HexLayout(78, size_t(1) << 32).digits, 9);
    bool chars;
    CHECK_EQ(l.at(l.hexCol(3) + 1, chars), 3);
    CHECK(!chars);
    CHECK_EQ(l.at(l.charCol(5), chars), 5);
    CHECK(chars);
    CHECK_EQ(l.at(0, chars), -1);
}

TEST(hex_typing_overwrite_insert_and_undo) {
    HexBuf h;
    h.data = std::string(2, '\0');
    h.typeHex(0xA);
    CHECK_EQ(h.nib, 1);
    h.typeHex(0xB);
    CHECK_EQ(h.data, std::string("\xAB\x00", 2));
    CHECK_EQ(h.pos, size_t(1));
    h.typeHex(0xC);
    h.typeHex(0xD);
    CHECK_EQ(h.data, std::string("\xAB\xCD"));
    CHECK(h.modified && h.stale);
    h.typeHex(0x1);  // at the end: appends a byte
    h.typeHex(0x2);
    CHECK_EQ(h.data, std::string("\xAB\xCD\x12"));
    CHECK_EQ(h.undo.size(), size_t(1));  // consecutive typing is one undo step
    CHECK(h.doUndo());
    CHECK_EQ(h.data, std::string(2, '\0'));
    CHECK_EQ(h.pos, size_t(0));
    CHECK(h.doRedo());
    CHECK_EQ(h.data, std::string("\xAB\xCD\x12"));
    // insert mode
    h.insert = true;
    h.setCursor(1, false);
    h.typeHex(0xE);
    CHECK_EQ(h.data, std::string("\xAB\xE0\xCD\x12"));
    h.typeHex(0xF);
    CHECK_EQ(h.data, std::string("\xAB\xEF\xCD\x12"));
    CHECK_EQ(h.pos, size_t(2));
    CHECK(h.doUndo());
    CHECK_EQ(h.data, std::string("\xAB\xCD\x12"));
    // characters: UTF-8, overwriting or inserting
    h.chars = true;
    h.insert = false;
    h.setCursor(0, false);
    h.typeChar(U'ä');
    CHECK_EQ(h.data, std::string("\xC3\xA4\x12"));
    h.insert = true;
    h.typeChar('x');
    CHECK_EQ(h.data, std::string("\xC3\xA4x\x12"));
}

TEST(hex_delete_select_paste_find) {
    HexBuf h;
    h.data = "0123456789";
    h.setCursor(5, false);
    h.backspace();
    h.backspace();
    CHECK_EQ(h.data, "01256789");
    CHECK_EQ(h.undo.size(), size_t(1));
    h.delFwd();
    h.delFwd();
    CHECK_EQ(h.data, "012789");
    CHECK_EQ(h.undo.size(), size_t(2));
    h.doUndo();
    h.doUndo();
    CHECK_EQ(h.data, "0123456789");
    h.setCursor(2, false);
    h.setCursor(5, true);
    CHECK(h.hasSel());
    CHECK_EQ(h.selected(), "234");
    h.paste("ab");  // replaces the selection
    CHECK_EQ(h.data, "01ab56789");
    CHECK_EQ(h.pos, size_t(4));
    h.paste("XY");  // overwrites
    CHECK_EQ(h.data, "01abXY789");
    h.insert = true;
    h.paste("-");
    CHECK_EQ(h.data, "01abXY-789");
    h.typeHex(4);  // typing over a selection in insert mode deletes it
    h.setCursor(0, false);
    h.setCursor(2, true);
    h.typeHex(4);
    h.typeHex(1);
    CHECK_EQ(h.data.substr(0, 3), "Aab");
    h.selectAll();
    CHECK(h.deleteSel());
    CHECK_EQ(h.data, "");
    // Find: case-insensitive for letters, wrapping around
    h.data = std::string("xxAbCxx\0abc", 11);
    size_t at;
    CHECK(h.find("abc", false, 0, at));
    CHECK_EQ(at, size_t(2));
    CHECK(h.find("abc", true, 0, at));
    CHECK_EQ(at, size_t(8));
    CHECK(h.find("abc", false, 9, at));  // wraps around
    CHECK_EQ(at, size_t(2));
    CHECK(h.find(std::string("\0a", 2), true, 0, at));
    CHECK_EQ(at, size_t(7));
    CHECK(!h.find("abcd", true, 0, at));
}

TEST(hex_move_keys_and_scrolling) {
    HexBuf h;
    h.data = std::string(100, 'x');
    h.moveKey(K_RIGHT, 0, 16, 4);
    CHECK_EQ(h.pos, size_t(1));
    h.moveKey(K_DOWN, 0, 16, 4);
    CHECK_EQ(h.pos, size_t(17));
    h.moveKey(K_END, 0, 16, 4);
    CHECK_EQ(h.pos, size_t(31));
    h.moveKey(K_HOME, M_SHIFT, 16, 4);
    CHECK_EQ(h.pos, size_t(16));
    CHECK_EQ(h.selected(), std::string(15, 'x'));
    h.moveKey(K_PGDN, 0, 16, 4);
    CHECK(!h.anch);
    CHECK_EQ(h.pos, size_t(64));
    h.moveKey(K_END, M_CTRL, 16, 4);
    CHECK_EQ(h.pos, size_t(100));  // after the last byte: room to append
    h.moveKey(K_UP, 0, 16, 4);
    CHECK_EQ(h.pos, size_t(84));
    h.moveKey(K_DOWN, 0, 16, 4);
    CHECK_EQ(h.pos, size_t(100));
    h.scrollTo(16, 4);
    CHECK_EQ(h.top, size_t(3));
    h.moveKey(K_HOME, M_CTRL, 16, 4);
    h.scrollTo(16, 4);
    CHECK_EQ(h.top, size_t(0));
    h.moveKey(K_DOWN, M_CTRL, 16, 4);  // Ctrl+↓ scrolls and keeps the cursor in the view
    CHECK_EQ(h.top, size_t(1));
    CHECK_EQ(h.pos, size_t(16));
    h.moveKey(K_LEFT, 0, 16, 4);
    h.moveKey(K_LEFT, 0, 16, 4);
    CHECK_EQ(h.pos, size_t(14));
}

TEST(editor_encode_and_byte_offsets) {
    Editor e;
    std::string data = "a\xC3\xA4" "b\r\nxy";
    e.setData(data);
    CHECK(e.crlf && !e.latin1 && !e.bom);
    CHECK_EQ(e.n(), 2);
    bool conv;
    CHECK_EQ(e.encode(conv), data);
    CHECK(!conv);
    CHECK_EQ(e.byteOffset({0, 2}), size_t(3));
    CHECK_EQ(e.byteOffset({1, 1}), size_t(7));
    CHECK_EQ(e.posOfByte(7), Pos({1, 1}));
    CHECK_EQ(e.posOfByte(2), Pos({0, 1}));  // inside the ä
    CHECK_EQ(e.posOfByte(5), Pos({0, 3}));  // the line break
    CHECK_EQ(e.posOfByte(100), Pos({1, 2}));
    e.setData("\xEF\xBB\xBFok");
    CHECK(e.bom);
    CHECK_EQ(e.byteOffset({0, 1}), size_t(4));
    CHECK_EQ(e.encode(conv), "\xEF\xBB\xBFok");
    e.setData("K\xF6ln");
    CHECK(e.latin1);
    CHECK_EQ(e.byteOffset({0, 2}), size_t(2));
    e.lines[0] += U'€';  // no longer Latin-1: saved as UTF-8
    CHECK_EQ(e.encode(conv), "K\xC3\xB6ln\xE2\x82\xAC");
    CHECK(conv);
    CHECK_EQ(e.byteOffset({0, 2}), size_t(3));
}

// ---- the application ----

static std::string hexData(App& a) { return a.doc().hex ? a.doc().hex->data : std::string("(text mode)"); }

TEST(app_hex_binary_file_opens_in_hex_mode) {
    std::string f = tempDir() + "/hex_open.bin";
    writeFile(f, std::string("\0\1ABC", 5));
    AppT t;
    t.a().openPath(f);
    CHECK(t.a().hexMode());
    t.draw();
    CHECK(t.row(2).find("00000000  00 01 41 42 43") != std::string::npos);
    CHECK(t.row(2).find("..ABC") != std::string::npos);
    CHECK(t.row(24).find("Hex") != std::string::npos);
    CHECK(t.row(24).find("OVR 00000000") != std::string::npos);
    t.keys("ff");
    CHECK_EQ(hexData(t.a()), std::string("\xFF\1ABC", 5));
    CHECK(t.ed().modified);
    CHECK(t.row(1).find("hex_open.bin *") != std::string::npos);
    t.keys(ctrl('s'));
    CHECK_EQ(readFile(f), std::string("\xFF\1ABC", 5));
    CHECK(!t.ed().modified);
    // a text file is opened as text, also into a buffer in hex mode
    std::string g = tempDir() + "/hex_text.txt";
    writeFile(g, "plain\n");
    t.a().openPath(g);
    CHECK(!t.a().hexMode());
    CHECK_EQ(t.text(), "plain\n");
    // edith --hex: every file of the command line in hex mode, also new ones
    t.a().hexStart = true;
    t.a().openPath(g);
    CHECK(t.a().hexMode());
    CHECK_EQ(hexData(t.a()), "plain\n");
    t.a().openPath(tempDir() + "/hex_new.bin");
    CHECK(t.a().hexMode());
    CHECK_EQ(hexData(t.a()), "");
}

TEST(app_hex_toggle_from_text_and_back) {
    AppT t("abc\ndef");
    t.keys(std::string(DOWN) + RIGHT);
    t.keys(alt('o') + alt('x'));  // Options > Hex Mode
    CHECK(t.a().hexMode());
    CHECK(t.a().checked(A_HEX));
    CHECK_EQ(hexData(t.a()), "abc\ndef");
    CHECK_EQ(t.a().doc().hex->pos, size_t(5));  // the cursor stays on the "e"
    // switching without a change leaves the text and its undo history alone
    t.keys(alt('o') + alt('x'));
    CHECK(!t.a().hexMode());
    CHECK_EQ(t.cur(), Pos({1, 1}));
    CHECK(!t.a().enabled(A_UNDO));
    t.keys(alt('o') + alt('x') + "7a");
    CHECK_EQ(hexData(t.a()), "abc\ndzf");
    t.keys("\t" + std::string("!"));  // Tab: the character area
    CHECK_EQ(hexData(t.a()), "abc\ndz!");
    t.keys(alt('o') + alt('x'));
    CHECK_EQ(t.text(), "abc\ndz!");
    CHECK_EQ(t.cur(), Pos({1, 3}));
    CHECK(t.ed().modified);
    t.keys(ctrl('z'));  // the changes in hex mode are one undo step
    CHECK_EQ(t.text(), "abc\ndef");
}

TEST(app_hex_menu_grays_out_text_entries) {
    AppT t("text");
    t.a().setHex(true);
    for (int a : {A_FORMAT, A_REPLACE, A_AUTOINDENT, A_EXPANDTAB, A_TABWIDTH, A_LINENUM, A_RELNUM, A_INVISIBLES,
                  A_SYNTAX, A_LANG, A_VIM})
        CHECK(!t.a().enabled(a));
    for (int a : {A_NEW, A_OPEN, A_SAVE, A_FIND, A_FINDNEXT, A_GOTO, A_SELALL, A_OVERWRITE, A_HEX, A_SPLIT})
        CHECK(t.a().enabled(a));
    CHECK(!t.a().enabled(A_UNDO));
    CHECK(!t.a().enabled(A_COPY));
    t.keys("41");
    CHECK(t.a().enabled(A_UNDO));
    // the Options menu shows them grayed out
    sendKeys(ESC);
    CHECK_EQ(t.a().menuLoop(3, true), int(A_NONE));
    int checkedRows = 0;
    for (int y = 2; y < 20; y++) {
        std::string r = t.row(y);
        size_t x = r.find("Line Numbers");  // also "Relative Line Numbers"
        if (x == std::string::npos) x = r.find("Vim Mode");
        if (x == std::string::npos) continue;
        int col = int(U(r.substr(0, x)).size());
        CHECK_EQ(int(t.cell(col, y).fg), int(Col::MDIS.fg));
        checkedRows++;
    }
    CHECK_EQ(checkedRows, 3);
    // their shortcuts do nothing either
    t.keys("\x1b[24~");  // F12: Vim mode
    CHECK(!S.vim);
    t.keys("\x1b[19~");  // F8: Reformat Code
    CHECK(t.a().hexMode());
    // Ins switches between overwrite and insert
    CHECK(t.a().checked(A_OVERWRITE));
    t.keys("\x1b[2~");
    CHECK(!t.a().checked(A_OVERWRITE));
    CHECK(t.a().doc().hex->insert);
    CHECK(!S.overwrite);
}

TEST(app_hex_find_goto_and_clipboard) {
    AppT t("Hello World");
    t.a().setHex(true);
    t.keys(ctrl('f') + "6F 20\r");  // hex bytes (the default)
    size_t a, b;
    CHECK(t.a().doc().hex->sel(a, b));
    CHECK_EQ(a, size_t(4));
    CHECK_EQ(b, size_t(6));
    t.keys("\x1b[13~");  // F3: the only match again, after wrapping around
    CHECK(t.a().doc().hex->sel(a, b));
    CHECK_EQ(a, size_t(4));
    t.keys(ctrl('f') + "WORLD\t \r");  // as text, ignoring case
    CHECK(t.a().doc().hex->sel(a, b));
    CHECK_EQ(a, size_t(6));
    t.keys(ctrl('g') + "0x2\r");
    CHECK_EQ(t.a().doc().hex->pos, size_t(2));
    CHECK(!t.a().doc().hex->anch);
    t.keys(ctrl('g') + "xyz\r" + "\r");  // not a hex number: a message
    CHECK_EQ(t.a().doc().hex->pos, size_t(2));
    t.keys("\x1b[1;2C\x1b[1;2C" + ctrl('c'));  // Shift+→ twice, copy as hex
    CHECK(t.a().clip == U"6C 6C");
    t.keys("\t" + ctrl('c'));  // in the character area: as text
    CHECK(t.a().clip == U"ll");
    t.a().clip = U"00 FF";
    t.keys("\t" + ctrl('v'));  // the hex area takes hex digits as bytes
    CHECK_EQ(hexData(t.a()), std::string("He\0\xFFo World", 11));
    t.keys("\t" + ctrl('v'));  // the character area takes them as text
    CHECK_EQ(hexData(t.a()).substr(4, 5), "00 FF");
    t.keys(ctrl('a') + ctrl('x'));
    CHECK_EQ(hexData(t.a()), "");
    t.keys(ctrl('z'));
    CHECK(hexData(t.a()).size() > 10);
    t.keys("\x1b[200~41\x1b[201~");  // bracketed paste
    CHECK(hexData(t.a()).find("41") != std::string::npos || hexData(t.a()).find('A') != std::string::npos);
}

TEST(app_hex_mouse) {
    std::string s;
    for (int i = 0; i < 1000; i++) s += char('a' + i % 26);
    AppT t(s);
    t.a().setHex(true);
    t.keys(std::string(LEFT));
    t.draw();
    HexLayout l = t.a().hexLayout();
    int x0 = t.a().tx(), y0 = t.a().ty();
    t.keys(click(x0 + l.hexCol(5), y0 + 1) + click(x0 + l.hexCol(5), y0 + 1, 0, true));
    HexBuf& h = *t.a().doc().hex;
    CHECK_EQ(h.pos, size_t(16 + 5));
    CHECK(!h.chars);
    t.keys(click(x0 + l.charCol(2), y0) + click(x0 + l.charCol(9), y0, 32) + click(x0 + l.charCol(9), y0, 0, true));
    CHECK(h.chars);
    size_t a, b;
    CHECK(h.sel(a, b));
    CHECK_EQ(a, size_t(2));
    CHECK_EQ(b, size_t(9));
    t.keys("\x1b[<65;10;10M");  // the wheel
    CHECK_EQ(h.top, size_t(3));
    t.keys("\x1b[<64;10;10M");
    CHECK_EQ(h.top, size_t(0));
    int sx = t.a().scr.w - 1;
    t.keys(click(sx, y0 + t.a().th() - 1));  // the down arrow of the scroll bar
    CHECK_EQ(h.top, size_t(1));
    t.keys(click(sx, y0 + 15) + click(sx, y0 + 15, 0, true));
    CHECK(h.top > 10);
    CHECK(h.pos / 16 >= h.top);  // the cursor stays in the view
}

TEST(app_hex_windows_and_save_all) {
    std::string f = tempDir() + "/hex_win.bin";
    writeFile(f, std::string("\0\0\0\0", 4));
    AppT t;
    t.a().openPath(f);
    t.keys("12");
    CHECK(t.a().splitWin(false));
    t.draw();
    // both windows show the bytes
    int rows = 0;
    for (int y = 2; y < 23; y++) rows += t.row(y).find("00000000  12 00 00 00") != std::string::npos;
    CHECK_EQ(rows, 2);
    t.keys(std::string(RIGHT) + RIGHT);
    CHECK_EQ(t.a().doc().hex->pos, size_t(3));
    t.keys("\x1b[17~");  // F6: the other window keeps its own cursor
    CHECK_EQ(t.a().doc().hex->pos, size_t(1));
    t.keys("\x1b[17~");
    CHECK_EQ(t.a().doc().hex->pos, size_t(3));
    // a text window in another tab saves every buffer with Vim's :wall
    t.a().newTab(nullptr);
    CHECK(!t.a().hexMode());
    u32 err;
    CHECK(t.a().vimHost.layout("wall", 0, u32(), false, err));
    CHECK_EQ(readFile(f), std::string("\x12\0\0\0", 4));
}

TEST(app_hex_with_vim_mode) {
    std::string f = tempDir() + "/hex_vim.bin";
    writeFile(f, std::string("\0AB", 3));
    AppT t("text");
    S.vim = true;
    t.a().vim.reset();
    t.keys(":e " + f + "\r");  // Vim's :e opens a binary file in hex mode as well
    CHECK(t.a().hexMode());
    t.keys("dd");  // no Vim commands: hex digits
    CHECK_EQ(hexData(t.a()), std::string("\xDD" "AB", 3));
    CHECK(t.row(24).find("Hex") != std::string::npos);
    t.keys(alt('o') + alt('x'));
    CHECK(!t.a().hexMode());
    CHECK(t.a().vim.mode == VM_NORMAL);
    CHECK(t.ed().latin1);
    CHECK_EQ(t.cur(), Pos({0, 1}));
    t.keys("x");  // Vim again
    CHECK_EQ(toUtf8(t.ed().lines[0]), "ÝB");
}

TEST(app_hex_german_menu_and_status) {
    g_uiLang = UiLang::German;
    buildMenus();
    AppT t("x");
    t.keys(alt('o') + alt('h'));  // Optionen > Hex-Modus
    CHECK(t.a().hexMode());
    CHECK(t.row(24).find("ÜB") != std::string::npos);
    sendKeys(ESC);
    t.a().doAct(A_GOTO);  // the dialog is drawn before the Esc is read
    bool found = false;
    for (int y = 0; y < 25; y++) found |= t.row(y).find("Gehe zu Offset") != std::string::npos;
    CHECK(found);
    g_uiLang = UiLang::English;
    buildMenus();
}
