# Security

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately through
GitHub's "Report a vulnerability" button on the Security tab of the repository, or by mail to
the maintainer (the address is in `packaging/arch/PKGBUILD`). Describe the problem and, if
possible, how to reproduce it (a file, a key sequence, the terminal). You will get an answer
within a week; fixes are released as a new patch version and listed in `CHANGELOG.md`.

Only the latest release is supported.

## What Edith does and does not do

These are the guarantees the code is written to keep; a case where Edith breaks one of them is
a security bug.

- **Opening a file never runs code.** Edith has no modelines, no `.exrc`/`.vimrc`, no plugins
  and no per-project configuration. tree-sitter grammars and queries are compiled into the
  binary.
- **File contents never reach the terminal as control sequences.** C0 and C1 control characters
  and DEL are shown as `^X`, `␀`… or `?` (`isControl()` in `src/utf8.h`, `Screen::put()`).
- **External programs run only on request:** Reformat Code (the formatter of the language, or
  the command from `~/.config/edith/formatters`) and Vim's `:!`, `:r !`, `:w !` and filters.
  They run with `/bin/sh`, without access to the terminal (`src/process.cpp`); file names are
  quoted for the shell. Formatters read the project's configuration, and some of those
  configuration files are code (for example `prettier.config.js`).
- **Saving keeps the file intact on errors:** an existing file is written to a temporary file
  next to it and renamed over it, keeping its permissions; files with several hard links or of
  another owner are written in place, so their identity and owner do not change.
- **The system clipboard is only written** (OSC 52, at most 100 000 bytes), never read.
