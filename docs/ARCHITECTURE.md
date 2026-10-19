# Architecture

A tour of the source code of Edith for contributors. For the rules and step-by-step guides see
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Overview

```
 terminal ──► terminal.cpp ──► Key ──► App::handleKey ──┬─► menus, dialogs (app_menu, app_dialogs, widgets)
 (bytes)      decodes escape         (app.cpp)          ├─► Vim engine (vim*.cpp)  ──┐
              sequences, mouse,                         └─► Editor commands ─────────┤
              paste                                                                  ▼
                                                                         Editor (editor.cpp)
                                                                         the text of the active buffer
 terminal ◄── Screen::flush ◄── App::drawAll ◄── highlighting (treesitter.cpp, lexer.cpp)
 (only the      (screen.cpp)     (app_draw.cpp)
  changes)
```

Edith has no dependencies besides the C++ standard library, POSIX and tree-sitter. There is no
ncurses: `terminal.cpp` puts the terminal into raw mode and decodes the input itself,
`screen.cpp` keeps a character buffer and writes only the cells that changed.

## Modules

| Module | Responsibility |
|--------|----------------|
| `main.cpp` | command line, UI language, terminal setup, starts `App::run()` |
| `app.h`, `app*.cpp` | the application `App`: main loop, drawing, menus, dialogs, mouse, windows and tabs, Reformat Code |
| `editor.*` | `Editor`: a text buffer with cursor, selection, undo/redo, search, loading and saving |
| `vim.h`, `vim*.cpp`, `vimregex.*` | the Vim engine and Vim's regular expressions |
| `screen.*`, `terminal.*`, `widgets.*` | output, input and the dialog toolkit |
| `menus.*`, `i18n*.cpp` | menu definitions, help text, translations |
| `languages.*`, `lexer.*`, `treesitter.*` | language detection and syntax highlighting |
| `format.*`, `app_format.cpp`, `process.*` | Reformat Code and running external programs |
| `clipboard.*` | the system clipboard (clipboard programs, OSC 52) |
| `search.*` | regular expressions in the Find and Change dialogs (with `vimregex.*`) |
| `utf8.*`, `paths.*`, `common.h` | helpers |

The layers depend only downwards: `Editor`, the Vim engine, the highlighting and the
formatters know nothing about `App`, `Screen` or the terminal. That is what makes them
testable in isolation (`tests/vimtest.h` drives the Vim engine on a bare `Editor`).

## The main loop

`App::run()` repeats: draw everything into the `Screen` buffer, `flush()` the differences to
the terminal, wait for a key (`getKey()`), handle it – and handle every key that is already
waiting before drawing again, so that fast typing and key repeat never lag behind.

`App::handleKey()` distributes a key: mouse events go to `app_mouse.cpp`, F10/Alt to the menu
bar, in Vim mode the key goes to `Vim::handleKey()` first, then the shortcuts (`App::shortcut()`
→ `App::doAct()`), and finally the editing commands of `Editor`. Menu entries, shortcuts and
Vim's Ex commands all end up in the same actions (`enum Act` in `menus.h`).

Dialogs are modal: `Dialog::run()` has its own small loop that draws the application behind it
(`Dialog::background`) and the dialog on top.

## Buffers, windows and tabs

```
App ── tabs: Tab ── root: Node (ROWS / COLS / LEAF) ── win: Win ── doc: Doc
                                                       View v      Editor store, highlighting, Vim marks
```

- A `Doc` is a buffer: the text of a file and its highlighting state.
- A `Win` shows a `Doc` with its own cursor and scroll position (`View`).
- A `Tab` has a layout tree of rows and columns (`Node`) with windows as leaves;
  `App::layoutAll()` computes the rectangles.

The buffer of the active window is **checked out** into `App::ed`: all editing code and the
Vim engine work on that one `Editor`. `App::activate()` saves the cursor of the old window,
swaps the texts and restores the new window's view. `App::textOf(doc)` returns the text of
any buffer; never keep a reference to `App::ed` across a window switch.

## Syntax highlighting

`detectLang()` picks the language from the file name, extension or `#!` line (`LANGS` in
`languages.cpp`). Languages with a tree-sitter grammar are highlighted by `TsDoc`
(`treesitter.cpp`); the others by the line-based fallback lexer (`lexer.cpp`).

- Every change marks the first changed line in `Editor::dirty`. `App::consumeDirty()` passes
  it on: the fallback lexer's cache of line start states (`Doc::hlStates`) is cut there and
  the tree-sitter document is marked dirty.
- Before drawing, `TsDoc::sync()` rebuilds the UTF-8 text, finds the edited range by comparing
  prefix and suffix and reparses incrementally.
- `TsDoc::highlight()` runs the highlight query (from nvim-treesitter, compiled into the
  binary by `tools/gen-registry.sh`) only over the **visible** byte range. Injections
  (`injections.scm`) find embedded languages, which are parsed with their own grammar and
  painted on top.
- Capture names are mapped to a few highlight classes (`tsCaptureClass()`, `H_*` in
  `languages.h`), and the classes to the 16 colors (`HLCOL`).

## The Vim engine

`Vim` (`vim.h`) works on an `Editor` and a `VimHost`. Keys are code points (`VK` namespace);
`Vim::feed()` appends to the input queue, `Vim::process()` resolves mappings and runs the
command parser of the current mode. Normal and Visual mode commands are parsed into a
`VimCmd` (count, register, operator, motion) and executed by `Vim::execute()`.

All buffer changes go through a few primitives (`setLine`, `insLines`, `delLines`,
`replaceLines`, `insText`, `delText`) so that marks, the jump list and `:g` follow the text,
and every command is enclosed in `beginChange()` / `endChange()` – one undo step, and nothing
recorded for `.` when nothing changed. Everything the engine needs from the application
(clipboard, dialogs, windows, quitting, formatting) is a virtual function of `VimHost`;
`AppVimHost` in `app.cpp` implements it.

## Reformat Code

`findFormatter()` (`format.cpp`) looks up the user's configuration and the table of standard
formatters and picks the first one that is installed. `App::reformatCode()`
(`app_format.cpp`) runs it with `runCommand()` (`process.cpp`: no terminal, a timeout) in the
file's directory and replaces the text as one undo step. Without a formatter JSON is formatted
by `formatJson()`, and other languages are re-indented by `TsDoc::indents()`, a port of
nvim-treesitter's `indent.lua`, or by braces (`braceIndents()`).

## Translations

All strings in the code are US English and wrapped in `tr()` / `trs()`. `tr()` looks the
string up in the table of the current language (`src/i18n_<code>.cpp`) and returns the English
text when there is no translation. The menus are built after the language is known
(`buildMenus()`). The tests extract every `tr()`/`trs()` string from the sources and check that
each language translates all of them with the same placeholders.

## Known limitations

- Undo keeps a full copy of the text per step (at most 300 steps). This is simple and robust,
  but needs a lot of memory for very large files; a list of changes would be more economical.
- There are no hidden buffers: a buffer exists as long as a window shows it.
- When the terminal is closed (or Edith gets SIGHUP or SIGTERM), Edith exits without saving;
  there are no swap or emergency save files.
- A regular expression search gives up after a fixed number of backtracking steps per line
  (`RX_MAX_STEPS` in `vimregex.h`), so a pathological pattern finds nothing instead of
  hanging; a search through many such lines can still take a while.
