Version 1.5.0
-------------
* Add nonword boundary assertion (`\B`).
* Character set supports unicode and hex now.
* Add some features that similar to nim-regex:
  * match()/bounds() will advance one character for empty match (instead of stop).
    For example: `.*?`.
  * match()/bounds() will do last match at the end of input.
* The new features won't apply to replace() or split(), so that
  `replacef("aaa", re"(a*)", "m($1)")` gets `m(aaa)` instead of `m(aaa)m()`.
  I think this is more intuitively.
* Fix bug to compile in i386 mode.
* Fix bug in split().

Version 1.4.0
-------------
* Add non-greedy repetition operators {n,m}? and {n,}?.
* Fix bug in repetition operators.

Version 1.3.0
-------------
* Fix bug in multiReplace().
* Add inclSep parameter for split().

Version 1.2.0
-------------
* Add proc multiReplace().

Version 1.1.0
-------------
* Fix wrong flags in reIU, reUG, and reIUG.
* Fix split to handle utf8 string for empty match.

Version 1.0.0
-------------
* Initial release.
