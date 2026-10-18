#pragma once
// Searching with regular expressions in the Find and Change dialogs. The patterns use Vim's
// "very magic" syntax (see vimregex.h), which is close to extended regular expressions:
// ( ) | + ? {n,m} . [ ] ^ $ \d \w \s ... ; < and > match the start and end of a word.
// = & ~ @ % are plain characters as in other regular expressions.

#include "common.h"
#include "editor.h"
#include "vimregex.h"

// Compiles the pattern of the Find dialog; false with the message in err.
// cs = match upper/lowercase, whole = only whole words.
bool compileSearch(VimRegex& rx, const u32& pattern, bool cs, bool whole, u32& err);

// The next match from "from" on (an empty match at "from" itself is skipped), wrapping
// around to the start of the file.
bool findRx(const Editor& ed, const VimRegex& rx, Pos from, RxMatch& m);

// The replacement text for a match: \0 is the whole match, \1 ... \9 the groups, \n a line
// break, \t a tab, \\ a backslash; everything else is taken literally.
u32 expandReplacement(const Editor& ed, const RxMatch& m, const u32& r);

// Replaces every match (one undo step); returns their number.
int replaceAllRx(Editor& ed, const VimRegex& rx, const u32& r);
