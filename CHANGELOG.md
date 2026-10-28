# Changelog

All notable changes to Edith are documented in this file. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and Edith uses [Semantic Versioning](https://semver.org/) (the version is set in `src/version.h`).

Write each entry on one line: the section of a version becomes the release notes on GitHub, which keep line breaks.

## [Unreleased]

## [1.3.0] - 2026-10-28

### Added

- Hex mode (Options > Hex Mode, `edith --hex`): shows and edits the bytes of a file in hexadecimal and as characters, with overwrite and insert, undo, the clipboard, Find for text or hex bytes and Go to Offset; binary files are opened in hex mode automatically, and the menu entries that only apply to text are grayed out.

## [1.2.0] - 2026-10-20

### Added

- Every tab has a × on the right of its title, also when only one tab is open; a mouse click on it closes the tab (asking for modified buffers), on the last tab it exits like F4.

### Changed

- Close Tab moved from the Window menu to the File menu.

## [1.1.0] - 2026-10-19

### Added

- Cut, Copy and Paste use the system clipboard everywhere (also Paste in dialogs and the Vim registers `+` and `*`): through `pbcopy`/`pbpaste` on macOS and `wl-clipboard`, `xclip` or `xsel` on Linux if installed, otherwise and over SSH by asking the terminal (OSC 52).
- Mac keys on macOS: Option+←/→ move by word (also in Terminal.app), Cmd+←/→ and Cmd+↑/↓ go to the start/end of the line and file, Option+Backspace and Cmd+Backspace delete; the help has a section for macOS and Terminal.app.
- Cmd shortcuts in terminals that pass the Command key on (kitty, Ghostty, WezTerm, iTerm2): Cmd works like Ctrl, plus Cmd+Shift+Z (redo), Cmd+G (find next), Cmd+L (go to line), Cmd+Option+F (change), Cmd+Shift+] / [ (tabs).
- Ctrl+Backspace and Alt+Backspace delete the word before the cursor.

### Fixed

- Option+→ in Terminal.app no longer opens the File menu.
- Cmd+letter no longer types the letter in terminals with the kitty keyboard protocol.

## [1.0.0] - 2026-10-18

First public release.

### Added

- Menus (Alt+letter or F10; h j k l move in menus and dialogs like the arrow keys), dialogs, mouse support, split windows and tabs, line numbers.
- Syntax highlighting with tree-sitter for more than 50 languages, including embedded languages.
- Find and Change, also with regular expressions (Vim's very magic syntax with = & ~ @ % as plain characters); the replacement can use the groups \1 to \9.
- Options > Show Invisible Characters (in Vim mode `:set list`) shows tabs as →, spaces as ·, line ends as ¶ (↵ in CRLF files), no-break spaces as °, control characters as ␀ to ␟ and marks zero-width characters with a colored cell.
- The status line shows the language, the encoding (UTF-8, UTF-8 BOM or Latin-1), the cursor position and, while text is selected, the start and end of the selection and its length.
- Reformat Code with the standard formatters, the built-in JSON formatter and re-indenting by the syntax tree.
- Vim mode (F12 or `--vim`) with motions, operators, text objects, registers, macros, Vim patterns, Ex commands and mappings; a selection becomes Visual mode when it is turned on and back; a few of Vim's easter eggs (try `:help!`).
- Runs on Linux and macOS.
- User interface in English, German, French, Spanish, Italian, Hungarian and Polish.
- Packages for Arch Linux (`.pkg.tar.zst` and `PKGBUILD`) and Debian/Ubuntu (`.deb`), an AppImage, a static Linux binary and a binary for macOS on Apple Silicon, built by the release workflow; a manual page (`man edith`) and a desktop entry; the binary packages contain the license texts of all third-party code (`THIRD_PARTY_LICENSES`).
- `--version`, `--` to end the options; unknown options are reported.
- `make` passes `CPPFLAGS` to the compiler, so distribution package builds get their hardening flags; `SECURITY.md` describes how to report vulnerabilities.
- Terminals: the key sequences of rxvt/urxvt, `ESC O` with modifiers and xterm's `modifyOtherKeys`; `ESCDELAY` for slow connections; `COLUMNS`/`LINES` when the terminal reports no size; bold instead of bright colors on the Linux console; a clear message for `TERM=dumb`; control characters (also C1 control characters in binary files) are never written to the terminal; the terminal is given back and set up again when the shell stops and continues Edith.
