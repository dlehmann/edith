# Contributing to Edith

Thank you for helping! This document explains how to build and test Edith and how to extend
it in the most common ways. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) gives an overview of
the source code; [AGENTS.md](AGENTS.md) contains the same rules in compact form for AI coding
agents.

## Getting started

```bash
tools/fetch-grammars.sh       # once: downloads tree-sitter and the grammars into third_party/
make -j$(nproc)               # builds ./edith
make test                     # unit and integration tests, then the end-to-end tests
build/edith-tests vim_ex       # only the tests whose name or file contains "vim_ex"
./edith --ts-check             # loads every grammar and query, reports dropped patterns
```

The first build compiles about 50 grammars and takes a few minutes; later builds only compile
what changed.

## Ground rules

- **Self-contained.** Grammars and queries are compiled into the binary; Edith needs no data
  files at run time. Libraries are fine – prefer ones the common distributions package and
  that can be linked statically, because the release builds use `make STATIC=1` so that they
  run on every Linux distribution (see `packaging/`).
- **External programs are optional.** Only Reformat Code calls external formatters, and
  everything has to work without them.
- **English** for code, comments, identifiers, commit messages and documentation. Every
  user-visible string is written in US English and translated (see below).
- **Tests with every change.** Every feature and every bug fix comes with tests in the same
  change, and `make test` must pass.
- **No warnings.** The build uses `-Wall -Wextra` and must stay free of warnings in `src/`.
- **Responsiveness.** Highlighting only queries the visible range and parsing is incremental;
  check the typing latency on a large file after changes to the drawing or highlighting path.
- `third_party/` and `build/ts_registry.inc` are generated – never edit them by hand.

## Code style

C++17, 4 spaces, at most 120 columns, braces on the same line. Short statements may stay on
one line (`if (x) return;`, `case A: f(); break;`). Text is UTF-32 (`u32`) inside the editor
and converted at the I/O boundaries. Match the style of the surrounding code; there is no
automatic formatter for the project (`.editorconfig` sets the basics).

Each source file starts with a comment that says what it contains; each header documents its
public functions. Comments explain *why* something is done, not what the next line does.

## Tests

| File | Covers |
|------|--------|
| `tests/test.h` | the framework: `TEST(name)`, `CHECK(c)`, `CHECK_EQ(a, b)` |
| `tests/test_core.cpp` | UTF-8, paths, i18n (every string translated, hotkeys unique), menus, language detection, the fallback lexer, tree-sitter |
| `tests/test_editor.cpp` | the text buffer, loading and saving |
| `tests/test_process.cpp` | running external programs |
| `tests/test_vim_*.cpp` | the Vim engine, with `VT` from `tests/vimtest.h` |
| `tests/test_ui.cpp` | terminal input decoding, the screen buffer, dialog widgets |
| `tests/test_app.cpp` | the whole application with `AppT` from `tests/apptest.h` |
| `tests/test_windows*.cpp` | windows and tabs, including a random command fuzzer |
| `tests/test_format.cpp` | Reformat Code |
| `tests/pty_test.py` | the real binary in a pseudo terminal, in every UI language |

Examples:

```cpp
TEST(vim_dw_deletes_a_word) {
    VT t("one tw‸o three");     // "‸" marks the cursor
    t.k("dw");                  // keys in Vim notation: <Esc>, <CR>, <C-v> ...
    CHECK_EQ(t.state(), "one tw‸three");
}
```

`AppT` sends raw bytes through the real input decoder; use `"\x1b[27u"` for Escape inside a
byte sequence (a lone `"\x1b"` followed by more bytes is read as Alt+key).

After larger changes to the Vim engine run the fuzzer longer, ideally with sanitizers:

```bash
EDITH_FUZZ_RUNS=100000 EDITH_FUZZ_SEED=12345 build/edith-tests vim_fuzz
make clean && make build/edith-tests CXXFLAGS="-g -O1 -fsanitize=address,undefined"
```

## How to ...

### ... add or change a user-visible string

1. Write the string in US English and wrap it in `tr(U"...")` (UTF-32) or `trs("...")` (UTF-8,
   for `printf`-style formatting).
2. Add the translation to the table `T` of **every** language file `src/i18n_<code>.cpp`. Keep
   `printf` placeholders (`%d`, `%s`) and the `&` hotkey markers; a hotkey must be unique in its
   menu and in the menu bar in every language.
3. `make test` fails for missing or unused translations and for menus or dialogs whose text
   does not fit.

Help text lines (`HELP_EN` in `src/menus.cpp`, `HELP` in the language files) are at most 70
columns wide.

### ... add a user interface language

1. Add the language to `enum class UiLang` in `src/i18n.h`.
2. Copy an existing language file to `src/i18n_<code>.cpp` (ISO 639 code), rename its table
   to `LANG_<CODE>` and translate `T` and `HELP`.
3. Add one line to the list in `uiLanguages()` in `src/i18n.cpp` (and the `extern` above it).
4. Mention the language in `README.md` and `AGENTS.md`.

The Makefile picks up the new file automatically; the tests check the translation in the
same way as for the other languages.

### ... add a programming language

1. Add an entry to `LANGS` in `src/languages.cpp`: name, file extensions and names, `#!`
   interpreters and the keywords for the fallback lexer.
2. If nvim-treesitter has a grammar for it: add it to `LANGS` in `tools/fetch-grammars.sh`,
   run `tools/fetch-grammars.sh <name>` and map the editor language to the grammar in
   `TS_FOR_LANG` (`src/treesitter.cpp`).
3. Add its standard formatter to the table in `src/format.cpp`, and the language to
   `indentSignificant()` if its indentation has a meaning.
4. Add tests for the detection (and for highlighting or formatting, if they need special
   handling) and check `./edith --ts-check`.

### ... add a menu command

1. Add an `A_...` value to `enum Act` in `src/menus.h` and the item to `buildMenus()`
   (`src/menus.cpp`) with a hotkey, a shortcut label and a status line help text.
2. Implement it in `App::doAct()` (`src/app.cpp`); `App::enabled()` / `App::checked()` in
   `src/app_menu.cpp` grey it out or show a check mark, `App::shortcut()` binds a key.
3. Translate the new strings, update the help text if there is a shortcut, add a test.

### ... add a Vim command

Read Vim's `:help` for the command first: the engine follows Vim's behavior, including its
edge cases.

- Normal mode commands, motions, operators and text objects live in `src/vim_normal.cpp`.
- Ex commands: add the name and its shortest abbreviation to `EXCMDS` in `src/vim_ex.cpp` and
  the implementation to `Vim::exCommand()`.
- Change the buffer only through the primitives in `src/vim.cpp` (`setLine`, `insLines`,
  `delLines`, `replaceLines`, `insText`, `delText`) and wrap each change in `beginChange()` /
  `endChange()`, so that marks, undo and `.` work.
- The engine must not know the application: anything it needs from outside (clipboard,
  dialogs, windows) goes through `VimHost`.
- Messages are UI strings: translate them and keep Vim's `Exxx:` codes.

## Screenshots and website

The screenshots in `docs/screenshots/` are made by a script, so that they can be renewed for
every version: `python3 tools/screenshots.py ./edith` runs Edith in a pseudo terminal, types
the keys of each scene (see `SCENES` in the script) and renders the screen as PNG. It needs
Pillow and the DejaVu Sans Mono font.

The website is `site/index.html`; `.github/workflows/pages.yml` publishes it on GitHub Pages
together with the screenshots whenever one of them changes on `master`.

## Releasing

1. Set the new version in `src/version.h` (Semantic Versioning), `docs/edith.1` (header line)
   and `pkgver` in `packaging/arch/PKGBUILD`; move the entries under "Unreleased" in
   `CHANGELOG.md` to a section for the version. A test checks that these agree.
2. Commit, then tag and push: `git tag v1.2.3 && git push origin master v1.2.3`.
3. The release workflow (`.github/workflows/release.yml`) builds and tests a static binary on
   Debian bookworm, the Arch package with makepkg on Arch Linux, the macOS binary on Apple
   Silicon (`.github/workflows/macos.yml`), and creates a **draft** release with the source
   tarball, the static binary, the `.deb`, the Arch package, the macOS archive, the AppImage,
   the `PKGBUILD` and `SHA256SUMS`. Check it and publish it. The `.deb` and the Arch
   package are also attached without the version in the name (`edith_amd64.deb`,
   `edith-editor-x86_64.pkg.tar.zst`, `edith-macos-arm64.tar.gz`), so that the `releases/latest/download/` links in the
   README always get the newest release.

`packaging/build-release.sh` does the same locally (see the comments in the scripts under
`packaging/`). The command and the Debian package are called `edith`; the Arch package is
`edith-editor`, because the AUR already has a package `edith`.

## Submitting changes

Before you open a pull request:

- `make test` passes and `make clean && make` builds without warnings,
- you looked at the change in the UI with at least `--lang=en` and `--lang=de`,
- `./edith --ts-check` reports no dropped patterns (after grammar or query updates),
- new strings are translated in every language,
- `CHANGELOG.md` has an entry under "Unreleased" for user-visible changes (one line per entry,
  without line breaks: it becomes the release notes).

Keep commits focused and describe *why* a change is made. When you report a bug, include the
Edith version (`edith --version`), the terminal, the steps to reproduce it and, if possible, a
small file that shows it.
