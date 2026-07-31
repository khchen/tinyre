<!--
#====================================================================
#
#             TinyRe - A Tiny Regex Engine for Nim
#                 Copyright (c) Chen Kai-Hung
#
#====================================================================
-->

Version 2.0.0
-------------
* Require Nim 2.0 or later.
* Fix NUL-byte, empty-pattern, anchor, and word-boundary handling.
* Fix zero-width matches in `split()`, `replace()`, and `multiReplace()`.
* Clarify byte-offset, capture, and UTF-8 matching semantics.
* Preserve preceding-byte context for `\b`, `\B`, `\<`, and `\>` with
  nonzero `start` offsets in matching and search APIs.
* Expand the regression suite.
* Refactor compiler and VM code.

Version 1.6.0
-------------
* Add a word-boundary assertion (`\b`); breaking change: use `\x08` for a
  literal backspace.
* Fix the `\B` assertion to match PCRE behavior.
* Fix zero-length matches.

Version 1.5.2
-------------
* Fix destructor compatibility.

Version 1.5.1
-------------
* Update for Nim Compiler 2.0.

Version 1.5.0
-------------
* Add a non-word-boundary assertion (`\B`).
* Add Unicode and hexadecimal support to character sets.
* Make `match()` and `bounds()` advance by one character after an empty match,
  similar to `nim-regex` (for example, `.*?`).
* Make `match()` and `bounds()` perform the final match at the end of input.
* Keep these behaviors out of `replace()` and `split()`; for example,
  `replacef("aaa", re"(a*)", "m($1)")` returns `m(aaa)`, not `m(aaa)m()`.
* Fix i386 compilation.
* Fix `split()`.

Version 1.4.0
-------------
* Add non-greedy repetition operators `{n,m}?` and `{n,}?`.
* Fix repetition operators.

Version 1.3.0
-------------
* Fix `multiReplace()`.
* Add the `inclSep` parameter to `split()`.

Version 1.2.0
-------------
* Add `multiReplace()`.

Version 1.1.0
-------------
* Fix flags in `reIU`, `reUG`, and `reIUG`.
* Fix UTF-8 empty-match handling in `split()`.

Version 1.0.0
-------------
* Initial release.
