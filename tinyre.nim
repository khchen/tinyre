#====================================================================
#
#             TinyRe - A Tiny Regex Engine for Nim
#                 Copyright (c) Chen Kai-Hung
#
#====================================================================

##[
  TinyRe is a Nim wrapper for a small regex engine based on Rob Pike's VM
  implementation. It supports byte-oriented matching and an optional UTF-8
  code-point mode, together with most common regex syntax. UTF-8 mode does not
  provide complete Unicode property handling or complete Unicode case folding.
  For a fixed compiled pattern, the Pike VM matching pass is designed to scale
  linearly with the input length.

  **NOTICE: This implementation always returns the entire pattern as the first
  capture. This differs from std/re.**

  Syntax
  ######

  .. code-block::
    ^          Match beginning of a buffer
    $          Match end of a buffer
    (...)      Grouping and substring capturing
    (?:...)    Non-capturing grouping
    \s         Match whitespace [ \t\n\r\f\v]
    \S         Match non-whitespace [^ \t\n\r\f\v]
    \w         Match alphanumeric [a-zA-Z0-9_]
    \W         Match non-alphanumeric [^a-zA-Z0-9_]
    \d         Match decimal digit [0-9]
    \D         Match non-decimal digit [^0-9]
    \n         Match a newline character
    \r         Match carriage return character
    \f         Match a form-feed character
    \v         Match a vertical-tab character
    \t         Match horizontal tab character
    +          Match one or more times (greedy)
    +?         Match one or more times (non-greedy)
    *          Match zero or more times (greedy)
    *?         Match zero or more times (non-greedy)
    ?          Match zero or one time (greedy)
    ??         Match zero or one time (non-greedy)
    x|y        Match x or y (alternation operator)
    \meta      Match one of the metacharacters: ^$().[]{}*+?|\
    \x00       Match hex character code (exactly 2 digits)
    \u0000     Match hex character code (exactly 4 digits)
    \U00000000 Match hex character code (exactly 8 digits)
    \<, \>     Match start-of-word and end-of-word
    \b         Match a word boundary; use \x08 for a backspace byte
    \B         Match a non-word boundary
    [...]      Match any character from the set. Ranges such as [a-z] or [\x00-\u0000] are supported.
    [^...]     Match any character not in the set.
    {n}        Match exactly n times.
    {n,}       Match the preceding atom at least n times (greedy).
    {n,m}      Match the preceding atom at least n and at most m times (greedy).
    {n,}?      Match the preceding atom at least n times (non-greedy).
    {n,m}?     Match the preceding atom at least n and at most m times (non-greedy).

]##

runnableExamples:
  doAssert match("abc123", re"\d+") == @["123"]
  doAssert bounds("abc123", re"\d+") == @[3..5]
  doAssert contains("abc123", re"\d+") == true
  doAssert startsWith("abc123", re"[a-z]+") == true
  doAssert endsWith("abc123", re"\d+") == true
  doAssert split("abc123", re"\d+") == @["abc", ""]
  doAssert replacef("abc123", re"([a-z]+)(\d+)", "$2$1") == "123abc"

  # reG for global matching
  doAssert match("abc123", reG".") == @["a", "b", "c", "1", "2", "3"]

  # reI for case-insensitive matching
  doAssert match("abc123", reI"ABC") == @["abc"]

  # reU for UTF-8 code-point matching (not full Unicode semantics)
  doAssert match("中文", reU"..") == @["中文"]

import std/strutils

when defined(js):
  {.error: "This library needs to be compiled with a c-like backend".}

{.compile: "re.c".}

type
  ReRaw = ptr object
  Re* = object
    ## Owns one compiled regular expression.
    ##
    ## Copies make an independent C-side copy, so the source value may be
    ## destroyed without invalidating the destination. `global` is a Nim-side
    ## iteration option and is preserved by copying.
    raw: ReRaw
    global: bool

  ReFlag* = enum
    reIgnoreCase ## Perform case-insensitive matching.
    reGlobal     ## Perform global matching.
    reUtf8       ## Match UTF-8 code points instead of individual bytes.

  ReGlobalKind = enum
    rgNone              ## Stop after the first match.
    rgIncludeLastEmpty  ## Include a zero-width match at the input end.
    rgExcludeLastEmpty  ## Stop before a zero-width match at the input end.

# These declarations form the small C ABI used by the Nim wrapper. Match
# results are returned as pointers into the caller-owned input buffer; the
# wrapper converts them to Nim byte offsets before exposing them publicly.
proc re_compile(pattern: cstring, i: cint, u: cint): ReRaw {.importc, cdecl.}
proc re_free(re: ReRaw) {.importc, cdecl.}
proc re_dup(re: ReRaw): ReRaw {.importc, cdecl.}
proc re_match_from(re: ReRaw, text: cstring, L: cint, cont: cstring,
    bolStart: cstring): cstringArray {.importc, cdecl.}
proc re_max_matches(re: ReRaw): cint {.importc, cdecl.}
proc re_flags(re: ReRaw, i: ptr cint, u: ptr cint) {.importc, cdecl.}
proc re_uc_len_bounded(re: ReRaw, s: cstring, length: cint): cint {.importc, cdecl.}
proc re_is_empty(re: ReRaw): cint {.importc, cdecl.}

const arcLike = defined(gcArc) or defined(gcAtomicArc) or defined(gcOrc)
when defined(nimAllowNonVarDestructor) and arcLike:
  proc `=destroy`(re: Re) =
    if not re.raw.isNil:
      re_free(re.raw)

else:
  proc `=destroy`(re: var Re) =
    if not re.raw.isNil:
      re_free(re.raw)
      re.raw = ReRaw(nil)

proc `=copy`(dest: var Re, source: Re) =
  # Re is an owning value: assignment duplicates the opaque C object instead
  # of copying its pointer, while preserving the Nim-side global flag.
  if dest.raw == source.raw: return
  `=destroy`(dest)
  wasMoved(dest)
  dest.raw = re_dup(source.raw)
  if dest.raw.isNil: raise newException(OutOfMemDefect, "out of memory")
  dest.global = source.global

iterator matchRaw(s: cstring, L0: int, re: ReRaw,
    global: ReGlobalKind, sub: bool, bolStart: cstring = nil,
    continuationStart: cstring = nil): Slice[int] {.closure.} =
  ## Internal adapter from the C pointer-based API to inclusive Nim slices.
  ## `sub` controls whether capture groups are yielded after the whole match;
  ## the global mode controls whether matching continues after that result.

  template `===`(a, b: cstring): bool =
    # cstring equality compares contents; this iterator needs pointer identity.
    cast[pointer](a) == cast[pointer](b)

  assert not re.isNil
  var insensitive, utf8: cint
  re_flags(re, addr insensitive, addr utf8)

  var
    L = L0
    p = s
    lastMatch1: cstring
    cont: cstring = continuationStart
    bol = if bolStart.isNil: s else: bolStart

  while true:
    var matches = re_match_from(re, p, cint L, cont, bol)
    if matches.isNil: break

    var i = 0
    while i < re_max_matches(re):
      var slice = cast[int](matches[i]) .. cast[int](matches[i + 1])
      if slice.a == 0 or slice.b == 0: # A missing C capture is represented by nil.
        slice = -1 .. -1

      else:
        slice.a = slice.a -% cast[int](s)
        slice.b = slice.b -% cast[int](s) -% 1

      if i == 0 and lastMatch1 === matches[1] and p === lastMatch1:
        # Avoid yielding the same zero-width anchor twice when continuation
        # state makes the C VM revisit the same input position.
        discard
      else:
        yield slice

      if not sub: break
      i.inc(2)

    if p === matches[1]:
      # A zero-width match must still make progress. In UTF-8 mode advance one
      # code point; in byte mode the C helper returns one byte.
      cont = p
      let uclen = int re_uc_len_bounded(re, p, cint L)
      L -= uclen
      p = cast[cstring](cast[int](p) +% uclen)
    else:
      L -= cast[int](matches[1]) -% cast[int](p)
      p = matches[1]
      cont = cast[cstring](cast[int](p) -% 1)

    lastMatch1 = matches[1]
    case global
    of rgNone:
      break
    of rgIncludeLastEmpty:
      if cast[int](p) >% cast[int](s) +% L0:
        break
    of rgExcludeLastEmpty:
      if cast[int](p) >=% cast[int](s) +% L0:
        break

proc continuationAt(s: string, start: int): cstring =
  ## Return the original subject byte immediately before `start`, if any.
  if start > 0:
    return cast[cstring](cast[int](s.cstring) +% start -% 1)

proc normalizePattern(s: string): string =
  # The C compiler accepts a cstring, so rewrite embedded NUL bytes before the
  # call. This keeps a Nim string pattern with "\0" from being truncated.
  for c in s:
    if c == '\0':
      result = newStringOfCap(s.len + 4)
      for c in s:
        if c == '\0':
          result.add "\\x00"
        else:
          result.add c
      return
  result = s

proc re*(s: string, flags: set[ReFlag] = {}): Re {.inline.} =
  ## Compiles `s` into a regular expression with `flags`.
  ##
  ## Embedded NUL bytes are normalized to `\x00` for the C compiler. Invalid
  ## patterns raise `ValueError`; a successful result owns its compiled state.
  let pattern = normalizePattern(s)
  result = Re(
    raw: re_compile(pattern.cstring, cint(reIgnoreCase in flags), cint(reUtf8 in flags)),
    global: reGlobal in flags
  )
  if result.raw.isNil: raise newException(ValueError, "cannot compile pattern")

proc reI*(s: string): Re {.inline.} =
  ## Compiles a case-insensitive regular expression.
  return re(s, {reIgnoreCase})

proc reG*(s: string): Re {.inline.} =
  ## Compiles a regular expression whose iterators return all matches.
  return re(s, {reGlobal})

proc reU*(s: string): Re {.inline.} =
  ## Compiles a regular expression that advances by UTF-8 code points.
  return re(s, {reUtf8})

proc reIG*(s: string): Re {.inline.} =
  ## Constructs a regular expression with reIgnoreCase and reGlobal flags.
  return re(s, {reIgnoreCase, reGlobal})

proc reIU*(s: string): Re {.inline.} =
  ## Constructs a regular expression with reIgnoreCase and reUtf8 flags.
  return re(s, {reIgnoreCase, reUtf8})

proc reUG*(s: string): Re {.inline.} =
  ## Constructs a regular expression with reUtf8 and reGlobal flags.
  return re(s, {reUtf8, reGlobal})

proc reIUG*(s: string): Re {.inline.} =
  ## Constructs a regular expression with reIgnoreCase, reUtf8 and reGlobal
  ## flags.
  return re(s, {reIgnoreCase, reGlobal, reUtf8})

template reGI*(s: string): Re = reIG(s) ## Same as `reIG(s)`
template reUI*(s: string): Re = reIU(s) ## Same as `reIU(s)`
template reGU*(s: string): Re = reUG(s) ## Same as `reUG(s)`
template reIGU*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reUIG*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reUGI*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reGIU*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reGUI*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`

proc groupsCount*(re: Re): int =
  ## Returns the number of match slots, including the whole-pattern slot.
  ## Thus a pattern with `n` explicit capturing groups returns `n + 1`.
  assert not re.raw.isNil
  return re_max_matches(re.raw) div 2

iterator match*(s: string, pattern: Re, start = 0): string =
  ## Yields matching substrings of `s[start..]`.
  ##
  ## The first yielded value is the whole pattern. With `reG`, matching
  ## continues from left to right and zero-width matches advance by one byte
  ## or one UTF-8 code point. Word-boundary assertions retain `s[start - 1]`
  ## as context when `start > 0`. An invalid `start` yields nothing.
  if start >= 0 and start <= s.len:
    let start0 = start # avoid to be modified during iteration
    let cs = cast[cstring](cast[int](s.cstring) +% start0)
    let continuation = continuationAt(s, start0)
    let rg = if pattern.global: rgIncludeLastEmpty else: rgNone
    for i in matchRaw(cs, s.len - start0, pattern.raw, rg, true,
                      continuationStart = continuation):
      var slice = (i.a +% start0) .. (i.b +% start0)
      yield if slice.b >= slice.a and slice.a >= 0: s[slice] else: ""

proc match*(s: string, pattern: Re, start = 0): seq[string] =
  ## Returns the strings yielded by `match(s, pattern, start)`.
  ## The whole pattern occupies the first slot of each match result; returns
  ## an empty sequence when `start` is invalid or there is no match.
  for m in match(s, pattern, start):
    result.add m

proc match*(s: string, pattern: Re, matches: var openArray[string], start = 0): int =
  ## Writes matching whole-pattern/capture strings into `matches` and returns
  ## the number written. The whole pattern is at index 0. If there are more
  ## results than slots, the excess results are ignored; an invalid `start` or
  ## no match leaves the array unchanged and returns 0.
  for m in match(s, pattern, start):
    if result >= matches.len: break
    matches[result] = m
    result.inc

iterator bounds*(s: string, pattern: Re, start = 0): Slice[int] =
  ## Yields inclusive byte-offset slices for the whole pattern and captures.
  ## A zero-width match is represented by a reversed slice such as `3 .. 2`;
  ## a nonparticipating capture is `-1 .. -1`. Word-boundary assertions retain
  ## `s[start - 1]` as context when `start > 0`. An invalid `start` yields
  ## nothing.
  if start >= 0 and start <= s.len:
    let start0 = start # avoid to be modified during iteration
    let cs = cast[cstring](cast[int](s.cstring) +% start0)
    let continuation = continuationAt(s, start0)
    let rg = if pattern.global: rgIncludeLastEmpty else: rgNone
    for i in matchRaw(cs, s.len - start0, pattern.raw, rg, true,
                      continuationStart = continuation):
      var slice = (i.a +% start0) .. (i.b +% start0)
      yield if i.a == -1 and i.b == -1: -1 .. -1 else: slice

proc bounds*(s: string, pattern: Re, start = 0): seq[Slice[int]] {.inline.} =
  ## Returns the inclusive byte-offset slices produced by `bounds()`.
  ## Returns an empty sequence when the pattern does not match.
  for slice in bounds(s, pattern, start):
    result.add slice

proc find*(s: string, pattern: Re, start = 0): int =
  ## Returns the first matching byte offset at or after `start`, or `-1`.
  ## `start` must be within `0..s.len`; word-boundary assertions retain
  ## `s[start - 1]` as context when `start > 0`.
  if start < 0 or start > s.len:
    return -1
  let cs = cast[cstring](cast[int](s.cstring) +% start)
  let continuation = continuationAt(s, start)
  for i in matchRaw(cs, s.len - start, pattern.raw, rgNone, false,
                    continuationStart = continuation):
    if i.a >= 0: return i.a +% start
  return -1

proc contains*(s: string, pattern: Re, start = 0): bool {.inline.} =
  ## Same as `find(s, pattern, start) >= 0`.
  return find(s, pattern, start) >= 0

proc startsWith*(s: string, prefix: Re, start = 0): bool =
  ## Returns true if `s[start..]` starts with `prefix`.
  ## Adding `^` to the pattern can avoid scanning for the first match.
  ## Word-boundary assertions retain `s[start - 1]` as context when `start > 0`.
  ## If `start` is outside `0..s.len`, returns false.
  if start < 0 or start > s.len:
    return false
  let cs = cast[cstring](cast[int](s.cstring) +% start)
  let continuation = continuationAt(s, start)
  for slice in matchRaw(cs, s.len - start, prefix.raw, rgNone, false,
                        continuationStart = continuation):
    return slice.a == 0
  return false

proc endsWith*(s: string, suffix: Re): bool =
  ## Returns true if the final match of `suffix` ends at the end of `s`.
  for slice in matchRaw(s.cstring, s.len, suffix.raw,
                        rgIncludeLastEmpty, false):
    if slice.a >= 0 and slice.b == s.len - 1:
      return true
  return false

proc split*(s: string, pattern: Re, maxsplit = -1, inclSep = false): seq[string] =
  ## Splits `s` around matches of `pattern`.
  ##
  ## A positive `maxsplit` limits the number of separators; a negative value
  ## allows unlimited splits and zero returns `s` unchanged. If `inclSep` is
  ## true, each matched separator is included as its own result item. An empty
  ## pattern splits into bytes, or UTF-8 code points when `reUtf8` is set.
  if maxsplit == 0: # do nothing
    result.add s
    return
  if s.len == 0 and find(s, pattern) == 0:
    return

  let cs = s.cstring
  var
    pos = 0
    count = 0
  let emptyPattern = re_is_empty(pattern.raw) != 0

  for slice in matchRaw(cs, s.len, pattern.raw, rgExcludeLastEmpty, false):
    if slice.b >= slice.a and slice.a >= 0: # not empty match
      result.add s[pos..slice.a - 1]
      pos = slice.b + 1
      count.inc
      if inclSep:
        result.add s[slice]
      if maxsplit >= 0 and count >= maxsplit: break

    else: # empty match
      if emptyPattern:
        # Keep the established behavior of an empty regex: split into
        # individual bytes or UTF-8 characters.
        let uclen = int re_uc_len_bounded(
          pattern.raw, cast[cstring](cast[int](cs) +% pos), cint(s.len - pos))
        result.add s[pos..pos + uclen-1]
        pos.inc(uclen)
      else:
        # Assertions are separators at their actual match position and must
        # not consume an unrelated character.
        if slice.a > pos:
          result.add s[pos..slice.a - 1]
        else:
          result.add ""
        pos = slice.a
      count.inc
      if maxsplit >= 0 and count >= maxsplit: break

      # Avoid the last empty string when splitting an empty regex.
      if emptyPattern and pos >= s.len: return

  result.add s[pos..^1]

proc replace*(s: string, sub: Re, by: string = "", limit = 0): string =
  ## Replaces matches of `sub` in `s` with `by`.
  ## Captures are not expanded in `by`; use `replacef` or the callback overload
  ## when replacement text depends on captures. A positive `limit` restricts
  ## the number of replacements; zero or a negative value means unlimited.
  let cs = s.cstring
  var
    pos = 0
    count = 0

  for slice in matchRaw(cs, s.len, sub.raw, rgExcludeLastEmpty, false):
    if slice.a >= 0:
      result.add s[pos..slice.a - 1]
      result.add by
      pos = slice.b + 1
      count.inc
      if limit > 0 and count >= limit: break

  result.add s[pos..^1]

proc replacef*(s: string, sub: Re, by: string = "", limit = 0): string =
  ## Replaces matches of `sub` with formatted text.
  ## Captures can be referenced with `$i` and `$#` (see `strutils.%`); `$1`
  ## refers to the first explicit capture because the whole pattern is slot 0.
  ## A positive `limit` restricts replacements; zero or a negative value means
  ## unlimited.

  # `matchRaw` emits the whole match before its captures; format replacements
  # intentionally expose only the explicit captures, so `$1` is matches[1].
  let
    cs = s.cstring
    groupsCount = sub.groupsCount()

  var
    matches = newSeq[string](groupsCount - 1)
    index = 0
    count = 0
    pos = 0
    slice0 = -1 .. -1

  for slice in matchRaw(cs, s.len, sub.raw, rgExcludeLastEmpty, true):
    if slice0.a < 0: slice0 = slice
    if index >= 1 and slice.b >= slice.a and slice.a >= 0: # not empty match
      matches[index - 1] = s[slice]

    index.inc
    if index > matches.len:
      if slice0.a >= 0:
        result.add s[pos..slice0.a - 1]
        result.addf(by, matches)
        pos = slice0.b + 1

      index = 0
      slice0 = -1 .. -1
      matches = newSeq[string](groupsCount - 1)
      count.inc
      if limit > 0 and count >= limit: break

  result.add s[pos..^1]

proc replace*(s: string, sub: Re,
    by: proc (n: int, matches: openArray[string]): string,
    limit = 0): string =
  ## Replaces matches using the result of `by`.
  ## The callback receives the zero-based match index and an array whose index
  ## 0 is the whole match, followed by the explicit captures. A positive
  ## `limit` restricts replacements; zero or a negative value means unlimited.
  let
    cs = s.cstring
    groupsCount = sub.groupsCount()

  var
    matches = newSeq[string](groupsCount)
    index = 0
    count = 0
    pos = 0
    slice0 = -1 .. -1

  for slice in matchRaw(cs, s.len, sub.raw, rgExcludeLastEmpty, true):
    if slice.a >= 0:
      if index == 0:
        slice0 = slice
      if slice.b >= slice.a:
        matches[index] = s[slice]

    index.inc
    if index == matches.len:
      if slice0.a >= 0:
        result.add s[pos..slice0.a - 1]
        result.add by(count, matches)
        pos = slice0.b + 1

      index = 0
      slice0 = -1 .. -1
      matches = newSeq[string](groupsCount)
      count.inc
      if limit > 0 and count >= limit: break

  result.add s[pos..^1]

proc matchAt(s: string, pattern: Re, start: int): seq[string] =
  # Probe exactly at `start`, retaining the preceding byte for boundary
  # assertions while preventing the search from moving to a later position.
  if start < 0 or start > s.len:
    return

  let
    cs = cast[cstring](cast[int](s.cstring) +% start)
    continuation = continuationAt(s, start)
  var first = true
  for slice in matchRaw(cs, s.len - start, pattern.raw, rgNone, true,
                        s.cstring, continuation):
    if first:
      first = false
      if slice.a != 0:
        return
    if slice.a >= 0 and slice.b >= slice.a:
      result.add s[(slice.a + start)..(slice.b + start)]
    else:
      result.add ""

proc multiReplace*(s: string, subs: openArray[tuple[re: Re, by: string]]): string =
  ## Returns a modified copy of `s` using the substitutions in `subs`.
  ##
  ## At each input position, entries are checked in order and the first
  ## matching entry wins. Replacement strings may use capture placeholders;
  ## zero-width matches still advance so the operation cannot loop forever.
  var pos = 0
  while pos <= s.len:
    var replaced = false
    block searchSubs:
      for i in 0..<subs.len:
        let matches = matchAt(s, subs[i].re, pos)
        if matches.len == 0:
          continue

        # Keep the established replace() behavior of not adding a trailing
        # replacement for the completely empty pattern, while still allowing
        # anchors such as `$` to match at the end of the input.
        if pos == s.len and matches[0].len == 0 and
            re_is_empty(subs[i].re.raw) != 0:
          continue

        addf(result, subs[i].by, matches[1..^1])
        replaced = true
        if matches[0].len == 0:
          if pos == s.len:
            return
          # A zero-length match must still make progress. Keep the current
          # character after the replacement, just like replace().
          result.add s[pos]
          pos.inc
        else:
          pos.inc(matches[0].len)
        break searchSubs

    if not replaced:
      if pos == s.len:
        break
      result.add s[pos]
      pos.inc

template `=~`*(s: string, pattern: Re, start = 0): untyped =
  ## Calls `match` and injects its result as `matches` in the surrounding scope.
  ## `matches[0]` is the whole pattern when the expression returns true.
  var matches {.inject.}: seq[string]
  matches = match(s, pattern, start)
  matches.len != 0

iterator bounds*(cs: cstring, pattern: Re, length = -1): Slice[int] =
  ## Yields inclusive byte-offset slices for `cs`, the whole pattern, and its
  ## captures. With `length >= 0`, `cs` need not be NUL-terminated and exactly
  ## `length` bytes are read. With a negative `length`, `cs` must be
  ## NUL-terminated and its length is determined at runtime.
  let L = if length < 0: cs.len else: length
  let rg = if pattern.global: rgIncludeLastEmpty else: rgNone
  for slice in matchRaw(cs, L, pattern.raw, rg, true):
    yield slice

proc bounds*(cs: cstring, pattern: Re, length = -1): seq[Slice[int]] {.inline.} =
  ## Returns the inclusive byte-offset slices produced by `bounds` for `cs`.
  ## With `length >= 0`, `cs` need not be NUL-terminated; with a negative
  ## `length`, it must be NUL-terminated. Returns an empty sequence on no match.
  for slice in bounds(cs, pattern, length):
    result.add slice

proc find*(cs: cstring, pattern: Re, length = -1): int =
  ## Returns the first matching byte offset in `cs`, or `-1`.
  ## With `length >= 0`, exactly that many bytes are read and `cs` need not be
  ## NUL-terminated. With a negative `length`, `cs` must be NUL-terminated.
  let L = if length < 0: cs.len else: length
  for i in matchRaw(cs, L, pattern.raw, rgNone, false):
    return i.a
  return -1

proc contains*(cs: cstring, pattern: Re, length = -1): bool {.inline.} =
  ## Same as `find(cs, pattern, length) >= 0`.
  return find(cs, pattern, length) >= 0

proc escapeRe*(s: string): string {.raises: [].} =
  ## Escapes regex metacharacters and control bytes in `s` for literal matching.
  ## In particular, embedded NUL and backspace are emitted as `\x00` and
  ## `\x08`, so the result remains unambiguous to the C compiler.
  for c in s:
    case c
    of '\n': result.add "\\n"
    of '\r': result.add "\\r"
    of '\t': result.add "\\t"
    of '\0': result.add "\\x00"
    of '\b': result.add "\\x08"
    of '\f': result.add "\\f"
    of '\v': result.add "\\v"
    of '^', '$', '(', ')', '.', '[', ']', '{', '}', '*', '+', '?', '|', '\\':
      result.add '\\'
      result.add c

    else:
      result.add c

when isMainModule:
  # Keep `nim r tinyre.nim` as the command-line entry point for the complete
  # test suite defined in tests.nim.
  include tests
