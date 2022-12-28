#====================================================================
#
#             TinyRE - Tiny Regex Engine for Nim
#                  Copyright (c) 2022 Ward
#
#====================================================================

##[
  TinyRE is a Nim wrap for a tiny regex engine based on Rob Pike's VM
  implementation. Compare to other small regex engines, this engine
  supports unicode and most of common regex syntax, in less than 10K
  code size (LOC < 1K), and guarantees that input regex will scale O(n)
  with the size of the string.

  **NOTICE: This implementation always return entire pattern as first
  capture. This is different from std/re.**

  Syntax
  ######

  .. code-block::
    ^          Match beginning of a buffer
    $          Match end of a buffer
    (...)      Grouping and substring capturing
    (?:...)    Non-capture grouping
    \s         Match whitespace [ \t\n\r\f\v]
    \S         Match non-whitespace [^ \t\n\r\f\v]
    \w         Match alphanumeric [a-zA-Z0-9_]
    \W         Match non-alphanumeric [^a-zA-Z0-9_]
    \d         Match decimal digit [0-9]
    \D         Match non-decimal digit [^0-9]
    \n         Match new line character
    \r         Match line feed character
    \f         Match form feed character
    \v         Match vertical tab character
    \t         Match horizontal tab character
    \b         Match backspace character
    +          Match one or more times (greedy)
    +?         Match one or more times (non-greedy)
    *          Match zero or more times (greedy)
    *?         Match zero or more times (non-greedy)
    ?          Match zero or once (greedy)
    ??         Match zero or once (non-greedy)
    x|y        Match x or y (alternation operator)
    \meta      Match one of the meta character: ^$().[]{}*+?|\
    \x00       Match hex character code (exactly 2 digits)
    \u0000     Match hex character code (exactly 4 digits)
    \U00000000 Match hex character code (exactly 8 digits)
    \<, \>     Match start-of-word and end-of-word
    \B         Matches a nonword boundary
    [...]      Match any character from set. Ranges like [a-z] or [\x00-\u0000] are supported
    [^...]     Match any character but ones from set
    {n}        Matches exactly n times
    {n,}       Matches the preceding character at least n times (greedy)
    {n,m}      Matches the preceding character at least n and at most m times (greedy)
    {n,}?      Matches the preceding character at least n times (non-greedy)
    {n,m}?     Matches the preceding character at least n and at most m times (non-greedy)

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

  # reI for case insensitive matching
  doAssert match("abc123", reI"ABC") == @["abc"]

  # reU for utf8 matching
  doAssert match("中文", reU"..") == @["中文"]

import std/[unicode, strutils]

when defined(js):
  {.error: "This library needs to be compiled with a c-like backend".}

{.compile: "re.c".}

type
  ReRaw = ptr object
  Re* = object
    raw: ReRaw
    global: bool

  ReFlag* = enum
    reIgnoreCase ## Perform case-insensitive matching
    reGlobal     ## Perform global matching
    reUtf8       ## Perform utf8 matching

  ReGlobalKind = enum
    rgNone
    rgIncludeLastEmpty
    rgExcludeLastEmpty

proc re_compile(pattern: cstring, i: cint, u: cint): ReRaw {.importc, cdecl.}
proc re_free(re: ReRaw) {.importc, cdecl.}
proc re_dup(re: ReRaw): ReRaw {.importc, cdecl.}
proc re_match(re: ReRaw, text: cstring, L: cint): cstringArray {.importc, cdecl.}
proc re_max_matches(re: ReRaw): cint {.importc, cdecl.}
proc re_flags(re: ReRaw, i: ptr cint, u: ptr cint) {.importc, cdecl.}
proc re_uc_len(re: ReRaw, s: cstring): cint {.importc, cdecl.}

proc `=destroy`(re: var Re) =
  if not re.raw.isNil:
    re_free(re.raw)
    re.raw = ReRaw(nil)

proc `=copy`(dest: var Re, source: Re) =
  if dest.raw == source.raw: return
  `=destroy`(dest)
  wasMoved(dest)
  dest.raw = re_dup(source.raw)
  if dest.raw.isNil: raise newException(OutOfMemDefect, "out of memory")

iterator matchRaw(s: cstring, L0: int, re: ReRaw,
    global: ReGlobalKind, sub: bool): Slice[int] {.closure.} =

  template `===`(a, b: cstring): bool =
    # must cast to ptr to compare cstring
    cast[pointer](a) == cast[pointer](b)

  assert not re.isNil
  var insensitive, utf8: cint
  re_flags(re, addr insensitive, addr utf8)

  var
    L = L0
    p = s
    lastMatch1: cstring

  while true:
    var matches = re_match(re, p, cint L)
    if matches.isNil: break

    var i = 0
    while i < re_max_matches(re):
      var slice = cast[int](matches[i]) .. cast[int](matches[i + 1])
      if slice.a == 0 or slice.b == 0: # (?, ?)
        slice = -1 .. -1

      else:
        slice.a = slice.a -% cast[int](s)
        slice.b = slice.b -% cast[int](s) -% 1

      if i == 0 and lastMatch1 === matches[1] and p === lastMatch1:
        # match same anchor again, avoid to yield the same slice twice.
        # for example, match(" a", re"\<")
        # first time match "| |a", second time match " ||a"
        # but yield the same slice because the pattern has no length.
        discard
      else:
        yield slice

      if not sub: break
      i.inc(2)

    if p === matches[1]:
      # zero length captures, advance one character instead of break
      let uclen = int re_uc_len(re, p)
      L -= uclen
      p = cast[cstring](cast[int](p) +% uclen)
    else:
      L -= cast[int](matches[1]) -% cast[int](p)
      p = matches[1]

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

proc re*(s: string, flags: set[ReFlag] = {}): Re {.inline.} =
  ## Constructor of regular expressions.
  result = Re(
    raw: re_compile(s, cint(reIgnoreCase in flags), cint(reUtf8 in flags)),
    global: reGlobal in flags
  )
  if result.raw.isNil: raise newException(ValueError, "cannot compile pattern")

proc reI*(s: string): Re {.inline.} =
  ## Constructor of regular expressions with reIgnoreCase flag.
  return re(s, {reIgnoreCase})

proc reG*(s: string): Re {.inline.} =
  ## Constructor of regular expressions with reGlobal flag.
  return re(s, {reGlobal})

proc reU*(s: string): Re {.inline.} =
  ## Constructor of regular expressions with reUtf8 flag.
  return re(s, {reUtf8})

proc reIG*(s: string): Re {.inline.} =
  ## Constructor of regular expressions with reIgnoreCase and reGlobal flags.
  return re(s, {reIgnoreCase, reGlobal})

proc reIU*(s: string): Re {.inline.} =
  ## Constructor of regular expressions with reIgnoreCase and reUtf8 flags.
  return re(s, {reIgnoreCase, reUtf8})

proc reUG*(s: string): Re {.inline.} =
  ## Constructor of regular expressions with reUtf8 and reGlobal flags.
  return re(s, {reUtf8, reGlobal})

proc reIUG*(s: string): Re {.inline.} =
  ## Constructor of regular expressions with reIgnoreCase, reUtf8 and
  ## reGlobal flags.
  return re(s, {reIgnoreCase, reGlobal, reUtf8})

template reGI*(s: string): Re = reGI(s) ## Same as `reIG(s)`
template reUI*(s: string): Re = reIU(s) ## Same as `reIU(s)`
template reGU*(s: string): Re = reUG(s) ## Same as `reUG(s)`
template reIGU*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reUIG*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reUGI*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reGIU*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`
template reGUI*(s: string): Re = reIUG(s) ## Same as `reIUG(s)`

proc groupsCount*(re: Re): int =
  ## Returns the number of capturing groups.
  assert not re.raw.isNil
  return re_max_matches(re.raw) div 2

iterator match*(s: string, pattern: Re, start = 0): string =
  ## Yields all matching substrings of `s[start..]` that match `pattern`.
  let start0 = start # avoid to be modified during iteration
  let cs = cast[cstring](cast[int](s.cstring) +% start0)
  let rg = if pattern.global: rgIncludeLastEmpty else: rgNone
  for i in matchRaw(cs, s.len - start0, pattern.raw, rg, true):
    var slice = (i.a +% start0) .. (i.b +% start0)
    yield if slice.a == -1 or slice.b == -1: "" else: s[slice]

proc match*(s: string, pattern: Re, start = 0): seq[string] =
  ## Returns all matching substrings of `s[start..]` that match `pattern`.
  ## If it does not match, returns empty seq.
  for m in match(s, pattern, start):
    result.add m

proc match*(s: string, pattern: Re, matches: var openArray[string], start = 0): int =
  ## Returns substrings in the array `matches` and then length of the matches
  ## if `s[start..]` matches the `pattern`. If it does not match, nothing is
  ## written into `matches` and 0 is returned.
  for m in match(s, pattern, start):
    if result >= matches.len: break
    matches[result] = m
    result.inc

iterator bounds*(s: string, pattern: Re, start = 0): Slice[int] =
  ## Yields all the starting position and end position of `pattern` and
  ## substrings in `s[start..]`.
  let start0 = start # avoid to be modified during iteration
  let cs = cast[cstring](cast[int](s.cstring) +% start0)
  let rg = if pattern.global: rgIncludeLastEmpty else: rgNone
  for i in matchRaw(cs, s.len - start0, pattern.raw, rg, true):
    var slice = (i.a +% start0) .. (i.b +% start0)
    yield slice

proc bounds*(s: string, pattern: Re, start = 0): seq[Slice[int]] {.inline.} =
  ## Returns all the starting position and end position of `pattern` and
  ## substrings in `s[start..]`. If it does not match, returns empty seq.
  for slice in bounds(s, pattern, start):
    result.add slice

proc find*(s: string, pattern: Re, start = 0): int =
  ## Returns the starting position of `pattern` in `s`.
  ## If it does not match, `-1` is returned.
  let cs = cast[cstring](cast[int](s.cstring) +% start)
  for i in matchRaw(cs, s.len - start, pattern.raw, rgNone, false):
    return i.a +% start
  return -1

proc contains*(s: string, pattern: Re, start = 0): bool {.inline.} =
  ## Same as `find(s, pattern, start) >= 0`.
  return find(s, pattern, start) >= 0

proc startsWith*(s: string, prefix: Re, start = 0): bool =
  ## Returns true if `s[start..]` starts with the pattern `prefix`.
  ## Add prefix `^` (assert start of string) to patten will speed up.
  let cs = cast[cstring](cast[int](s.cstring) +% start)
  for slice in matchRaw(cs, s.len - start, prefix.raw, rgNone, false):
    return slice.a == 0
  return false

proc endsWith*(s: string, suffix: Re): bool =
  ## Returns true if `s` ends with the pattern `suffix`.
  var insensitive, utf8: cint
  re_flags(suffix.raw, addr insensitive, addr utf8)

  if utf8.bool:
    for i in countdown(s.runeLen - 1, 0):
      let start = s.runeOffset(i)
      let cs = cast[cstring](cast[int](s.cstring) +% start)
      for slice in matchRaw(cs, s.len - start, suffix.raw, rgNone, false):
        if slice.b >= slice.a and start + slice.b == s.len - 1:
          return true
        break
  else:
    for start in countdown(s.len - 1, 0):
      let cs = cast[cstring](cast[int](s.cstring) +% start)
      for slice in matchRaw(cs, s.len - start, suffix.raw, rgNone, false):
        if slice.b >= slice.a and start + slice.b == s.len - 1:
          return true
        break

  return false

proc split*(s: string, pattern: Re, maxsplit = -1, inclSep = false): seq[string] =
  ## Splits the string `s` into a seq of substrings. If `maxsplit` is
  ## specified and is positive, no more than `maxsplit` splits is made.
  ## If `inclSep` is true, the separator will be included in the result.
  if maxsplit == 0: # do nothing
    result.add s
    return

  let cs = s.cstring
  var
    pos = 0
    count = 0

  for slice in matchRaw(cs, s.len, pattern.raw, rgExcludeLastEmpty, false):
    if slice.b >= slice.a: # not empty match
      result.add s[pos..slice.a - 1]
      pos = slice.b + 1
      count.inc
      if maxsplit >= 0 and count >= maxsplit: break

      if inclSep:
        result.add s[slice]
        count.inc
        if maxsplit >= 0 and count >= maxsplit: break

    else: # empty match, add one character as result
      let uclen = int re_uc_len(pattern.raw, cast[cstring](cast[int](cs) +% pos))
      result.add s[pos..pos + uclen-1]
      pos.inc(uclen)
      count.inc
      if maxsplit >= 0 and count >= maxsplit: break

      # avoid last empty string after adding one character
      if pos >= s.len: return

  result.add s[pos..^1]

proc replace*(s: string, sub: Re, by: string = "", limit = 0): string =
  ## Replaces `sub` in `s` by the string `by`. Captures cannot be
  ## accessed in `by`.
  let cs = s.cstring
  var
    pos = 0
    count = 0

  for slice in matchRaw(cs, s.len, sub.raw, rgExcludeLastEmpty, false):
    if slice.b >= slice.a: # not empty match
      result.add s[pos..slice.a - 1]
      result.add by
      pos = slice.b + 1
      count.inc
      if limit > 0 and count >= limit: break

  result.add s[pos..^1]

proc replacef*(s: string, sub: Re, by: string = "", limit = 0): string =
  ## Replaces `sub` in `s` by the string `by`. Captures can be accessed in `by`
  ## with the notation `$i` and `$#` (see strutils.\`%\`).

  # carefully deal with matches, so that $1 = matches[1] (by default is matches[0])
  let cs = s.cstring
  var
    matches = newSeq[string](sub.groupsCount() - 1)
    index = 0
    count = 0
    pos = 0
    slice0: Slice[int]

  for slice in matchRaw(cs, s.len, sub.raw, rgExcludeLastEmpty, true):
    if index == 0:
      slice0 = slice

    else:
      matches[index - 1] = s[slice]

    index.inc

    if index > matches.len:
      result.add s[pos..slice0.a - 1]
      result.addf(by, matches)
      pos = slice0.b + 1

      index = 0
      count.inc
      if limit > 0 and count >= limit: break

  result.add s[pos..^1]

proc replace*(s: string, sub: Re,
    by: proc (n: int, matches: openArray[string]): string,
    limit = 0): string =
  ## Replaces `sub` in `s` by the resulting strings from the callback.
  ## The callback proc receives the index of the current match (starting with 0),
  ## and an open array with the captures of each match.
  let cs = s.cstring
  var
    matches = newSeq[string](sub.groupsCount())
    index = 0
    count = 0
    pos = 0
    slice0: Slice[int]

  for slice in matchRaw(cs, s.len, sub.raw, rgExcludeLastEmpty, true):
    if index == 0:
      slice0 = slice

    matches[index] = s[slice]
    index.inc

    if index == matches.len:
      result.add s[pos..slice0.a - 1]
      result.add by(count, matches)
      pos = slice0.b + 1

      index = 0
      count.inc
      if limit > 0 and count >= limit: break

  result.add s[pos..^1]

proc multiReplace*(s: string, subs: openArray[tuple[re: Re, by: string]]): string =
  ## Returns a modified copy of `s` with the substitutions in `subs`
  ## applied in parallel.
  var pos = 0
  while pos < s.len:
    block searchSubs:
      for i in 0..<subs.len:
        if s.startsWith(subs[i].re, pos): # optimize it?
          var matches = s.match(subs[i].re, pos)
          if matches[0].len != 0:
            addf(result, subs[i].by, matches[1..^1])
            pos.inc(matches[0].len)
            break searchSubs

      result.add s[pos]
      pos.inc

  result.add s[pos..^1]

template `=~`*(s: string, pattern: Re, start = 0): untyped =
  ## This calls `match` with an implicit declared `matches` seq that
  ## can be used in the scope of the `=~` call.
  var matches {.inject.}: seq[string]
  matches = match(s, pattern, start)
  matches.len != 0

iterator bounds*(cs: cstring, pattern: Re, length = -1): Slice[int] =
  ## Yields all the starting position and end position of `pattern` and
  ## substrings in cstring `cs`.
  ## `cs` is not necessary null-terminated if length >= 0.
  ## Otherwise, `cs` will be assumed null-terminated and then length
  ## will be counted at runtime.
  let L = if length < 0: cs.len else: length
  let rg = if pattern.global: rgIncludeLastEmpty else: rgNone
  for slice in matchRaw(cs, L, pattern.raw, rg, true):
    yield slice

proc bounds*(cs: cstring, pattern: Re, length = -1): seq[Slice[int]] {.inline.} =
  ## Returns all the starting position and end position of `pattern` and
  ## substrings in cstring `cs`. If it does not match, returns empty seq.
  ## `cs` is not necessary null-terminated if length >= 0.
  ## Otherwise, `cs` will be assumed null-terminated and then length
  ## will be counted at runtime.
  for slice in bounds(cs, pattern, length):
    result.add slice

proc find*(cs: cstring, pattern: Re, length = -1): int =
  ## Returns the starting position of `pattern` in cstring `cs`.
  ## If it does not match, `-1` is returned.
  ## `cs` is not necessary null-terminated if length >= 0.
  ## Otherwise, `cs` will be assumed null-terminated and then length
  ## will be counted at runtime.
  let L = if length < 0: cs.len else: length
  for i in matchRaw(cs, L, pattern.raw, rgNone, false):
    return i.a
  return -1

proc contains*(cs: cstring, pattern: Re, length = -1): bool {.inline.} =
  ## Same as `find(cs, pattern, start) >= 0`.
  return find(cs, pattern, length) >= 0

proc escapeRe*(s: string): string {.raises: [].} =
  ## Escapes `s` so that it can be matched verbatim.
  for c in s:
    case c
    of '\n': result.add "\\n"
    of '\r': result.add "\\r"
    of '\t': result.add "\\t"
    of '\b': result.add "\\b"
    of '\f': result.add "\\f"
    of '\v': result.add "\\v"
    of '^', '$', '(', ')', '.', '[', ']', '{', '}', '*', '+', '?', '|', '\\':
      result.add '\\'
      result.add c

    else:
      result.add c

when isMainModule:
  doAssert match("abc123", re"\d+") == @["123"]
  doAssert bounds("abc123", re"\d+") == @[3..5]
  doAssert contains("abc123", re"\d+") == true
  doAssert startsWith("abc123", re"[a-z]+") == true
  doAssert endsWith("abc123", re"\d+") == true
  doAssert split("abc123", re"\d+") == @["abc", ""]
  doAssert replacef("abc123", re"([a-z]+)(\d+)", "$2$1") == "123abc"

  # reG for global matching
  doAssert match("abc123", reG".") == @["a", "b", "c", "1", "2", "3"]

  # reI for case insensitive matching
  doAssert match("abc123", reI"ABC") == @["abc"]

  # reU for utf8 matching
  doAssert match("中文", reU"..") == @["中文"]
