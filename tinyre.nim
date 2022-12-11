{.experimental: "views".}
{.compile: "re.c".}

type
  ReRaw = distinct pointer
  Re* = object
    raw: ReRaw

proc isNil(r: ReRaw): bool {.borrow.}
proc re_compile(pattern: cstring, insensitive: bool = false): ReRaw {.importc.}
proc re_free(re: ReRaw) {.importc.}
proc re_match(re: ReRaw, text: cstring, L: cint): cstringArray {.importc.}
proc re_max_matches(re: ReRaw): cint {.importc.}

proc `=destroy`(re: var Re) =
  if not re.raw.isNil:
    echo "=destroy re"
    re_free(re.raw)
    re.raw = nil

proc `=copy`(dest: var Re, source: Re) {.error.}

proc re*(s: string): Re =
  return Re(raw: re_compile(s, false))

proc rei*(s: string): Re =
  return Re(raw: re_compile(s, true))

# type
#   StringView = openArray[char]

# var arr = newSeq[StringView]()
# var s = "abc123"
# # s[2] = 'A'
# var sv = toOpenArray(s, 0, s.len-1)
# s[2] = 'B'
# echo s
# echo sv

proc `$`(s: openArray[char]): string =
  result = newStringOfCap(s.len)
  for c in s:
    result.add c

iterator matchRaw(s: cstring, L: int, re: Re): openArray[char] =
  var matches = re_match(re.raw, s, cint L)
  if matches != nil:
    var i = 0
    while i < re_max_matches(re.raw):
      let L = cast[int](matches[i + 1]) - cast[int](matches[i])
      yield toOpenArray(matches[i], 0, L - 1)
      i.inc(2)

iterator matchRaw(s: string, re: Re): openArray[char] =
  for i in matchRaw(s.cstring, s.len, re):
    yield i

# match
# find
# matchLen
# matchBound

proc match*(s: string, re: Re, matches: var openArray[string], start = 0): bool =
  let cs = cast[cstring](cast[uint](s.cstring) + start.uint)
  var index = 0
  for o in matchRaw(cs, s.len - start, re):
    if index < matches.len:
      matches[index] = $o
      index.inc

  return index != 0

proc matchBound*(s: string, re: Re, matches: var openArray[HSlice[int, int]], start = 0): bool =
  let cs = cast[cstring](cast[uint](s.cstring) + start.uint)
  var index = 0
  for o in matchRaw(cs, s.len - start, re):
    if index < matches.len:
      matches[index] = $o
      index.inc

  return index != 0



block:
  var r = re"\d+"
  var matches = newSeq[string](20)
  echo match("zzz123abc\0", r, matches)
  echo matches

block:
  var r = re"\d+"
  var matches: array[20, openArray[char]]
  echo match("zzz123abc\0", r, matches)



# var r = re"(\d)(\d+)"
# for o in "123abc".matchRaw(r):
#   echo o

# var r = re".*"
# var s = "123abc\0"
# for o in s.matchRaw(r):
#   echo repr o

  # echo cast[cstring](addr(o[0]))



# var re = re_compile(r"\d+")
# var matches = re.re_match("123abc")

# re_free(re)


# static void _re_match(PKVM* vm, RE* re, const char *input, uint32_t len,
#     bool global, bool range) {

#   pkNewList(vm, 0);
#   const char *ptr = input;
#   do {
#     const char** matches = re_match(re, ptr);
#     if (!matches) break;

#     for (int i = 0; i < re_max_matches(re); i += 2) {
#       if (matches[i] && matches[i + 1]) {
#         if (range) pkNewRange(vm, 1, matches[i] - input, matches[i + 1] - input);
#         else  pkSetSlotStringLength(vm, 1, matches[i], matches[i + 1] - matches[i]);
#         pkListInsert(vm, 0, -1, 1);
#       } else {
#         if (range) pkSetSlotNull(vm, 1);
#         else pkSetSlotStringLength(vm, 1, ptr, 0);
#         pkListInsert(vm, 0, -1, 1);
#       }
#     }
#     if (ptr == matches[1]) break; // cannot advance

#     ptr = matches[1]; // point to last matched char
#   } while (global && ptr < input + len);
# }

# proc match(re: RE, input: cstring, len: int32, global = false) =
#   var p = cast[ptr char](input)

#   let matches = re_match(re, input)
#   if matches.isNil:
#     var i = 0
#     while i < re_max_matches(re):
#       if (not matches[i].isNil) and (not matches[i + 1].isNil):

#         # matches[i], cast[uint](matches[i + 1]) - cast[uint](matches[i])

#       i.inc(2)

#     const char** matches = re_match(re, ptr);
#     if (!matches) break;

#     for (int i = 0; i < re_max_matches(re); i += 2) {
#       if (matches[i] && matches[i + 1]) {
#         if (range) pkNewRange(vm, 1, matches[i] - input, matches[i + 1] - input);
#         else  pkSetSlotStringLength(vm, 1, matches[i], matches[i + 1] - matches[i]);
#         pkListInsert(vm, 0, -1, 1);
#       } else {
#         if (range) pkSetSlotNull(vm, 1);
#         else pkSetSlotStringLength(vm, 1, ptr, 0);
#         pkListInsert(vm, 0, -1, 1);
#       }
#     }
#     if (ptr == matches[1]) break; // cannot advance

#     ptr = matches[1]; // point to last matched char




