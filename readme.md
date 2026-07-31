<!--
#====================================================================
#
#             TinyRe - A Tiny Regex Engine for Nim
#                 Copyright (c) Chen Kai-Hung
#
#====================================================================
-->

[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://paypal.me/khchen0915?country.x=TW&locale.x=zh_TW)

# TinyRe
TinyRe is a Nim wrapper for a small regex engine based on Rob Pike's VM
implementation. It supports byte-oriented matching and an optional UTF-8
code-point mode, together with most common regex syntax. UTF-8 mode does not
provide complete Unicode property handling or complete Unicode case folding.
For a fixed compiled pattern, the Pike VM matching pass is designed to scale
linearly with the input length.

## Features
* Supports UTF-8 matching.
* Supports case-insensitive matching.
* Supports global matching.
* Supports most common regex syntax, including:
  * Greedy and non-greedy expressions: `*`, `+`, `?`, `*?`, `+?`, `??`.
  * Character sets: `[xyz]`, `[a-z]`, `[^xyz]`, `[^m-z]`.
  * Metacharacters: `\s`, `\S`, `\w`, `\W`, `\d`, `\D`, `\n`, `\r`, `\t`, etc.
  * Fixed-width hexadecimal escapes: `\x00`, `\u0000`, `\U00000000`.
  * Alternation operator: `|`.
  * Start- and end-of-input assertions: `^`, `$`.
  * Repetition operators: `{n}`, `{n,m}`, `{n,}`.
  * Non-greedy repetition operators: `{n,m}?`, `{n,}?`.
  * Capturing and non-capturing groups: `(...)`, `(?:...)`.
  * Start-of-word, end-of-word, and word/non-word boundary assertions: `\<`, `\>`, `\b`, `\B`.

## Matching semantics

* For `match()` and `bounds()`, slot 0 is always the whole-pattern match;
  explicit capture groups follow it. This differs from Nim's `std/re`.
* `bounds()` returns inclusive byte offsets into a Nim string. A zero-width match
  is represented by a reversed slice such as `3..2`, while a nonparticipating
  capture is `-1..-1`.
* `reG` continues matching from left to right. Empty matches still make
  progress by one byte, or by one UTF-8 code point when `reU` is enabled.
* `\b` is a word-boundary assertion. Use `\x08` when a literal backspace byte
  is required. Embedded NUL bytes in Nim pattern strings are normalized for
  the C compiler; `\x00` is the explicit pattern spelling.
* For APIs with a `start` offset, word-boundary assertions retain the byte
  immediately before that offset as context. Thus `\b`, `\B`, `\<`, and `\>`
  do not treat a nonzero `start` as the beginning of a new word.

## Examples
```nim
import tinyre

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

# reU for UTF-8 code-point matching (not full Unicode semantics)
doAssert match("中文", reU"..") == @["中文"]
```

## Code Size

The following is a historical code-size snapshot for different regex
libraries. Results depend on the compiler, target, and build options; they are
not a current reproducible baseline.

```nim
import std/strutils
import tinyre

# GCC 11.1.0 on MinGW-w64
# Compilation options: -d:release -d:danger --opt:size -d:lto -d:strip

# The two calls below are the small workload used for the comparison.
echo contains("abc123def", "123") # for strutils
echo contains("abc123def", re"\d+") # for the regex library
```

```text
# Results
strutils:       65,536 bytes (no external dependencies)
std/re:         68,608 bytes (depends on pcre64.dll: 526,336 bytes)
TinyRe:         75,264 bytes (no external dependencies)
nim-regex:     296,448 bytes (no external dependencies)
```

## Performance

The historical measurements below were faster than `std/re` for the small
workload but slower for the large workload on the author's machine. They are
included for context only; the test data and patterns came from
[regex-benchmark](https://github.com/mariomka/regex-benchmark).

```text
# Historical workload descriptions; these are not a benchmark command.
# small string: "abc123def".contains("\d+")
# large string: 6.71 MB text file
#   email: [\w\.+-]+@[\w\.-]+\.[\w\.-]+
#   uri: [\w]+://[^/\s?#]+[^\s?#]+(?:\?[^\s#]*)?(?:#[^\s]*)?
#   ipv4: (?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9])\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9])
# Compilation options: -d:release -d:danger --opt:speed -d:lto

name ............................... min time      avg time    std. dev.  runs
TinyRe (small string) .............. 0.366 ms      0.379 ms    ±0.018  x1000
std/re (small string) .............. 5.862 ms      6.218 ms    ±0.171   x797
nim-regex (small string) .......... 16.132 ms     17.067 ms    ±0.580   x288
TinyRe (large string, email) ..... 140.684 ms    151.663 ms    ±8.625    x33
std/re (large string, email) ...... 44.793 ms     48.884 ms    ±2.716   x102
nim-regex (large string, email) .... 3.680 ms      3.921 ms    ±0.132  x1000
TinyRe (large string, URI) ....... 127.465 ms    131.721 ms    ±2.110    x38
std/re (large string, uri) ........ 40.380 ms     42.812 ms    ±1.175   x117
nim-regex (large string, uri) ..... 21.400 ms     22.205 ms    ±0.344   x225
TinyRe (large string, IPv4) ...... 182.995 ms    186.441 ms    ±1.057    x27
std/re (large string, ipv4) ........ 4.854 ms      5.965 ms    ±0.903   x838
nim-regex (large string, ipv4) ..... 7.569 ms      7.849 ms    ±0.159   x635
```

## Documentation

* https://khchen.github.io/tinyre

## Reference
* [pikevm](https://github.com/kyx0r/pikevm "pikevm") by Kyryl Melekhin
* [re1.5](https://github.com/pfalcon/re1.5 "re1.5") by Paul Sokolovsky
* [re1.0](https://code.google.com/archive/p/re1/ "re1.0") by Russ Cox
* [Regular Expression Matching: the Virtual Machine Approach](https://swtch.com/~rsc/regexp/regexp2.html "Regular Expression Matching: the Virtual Machine Approach") by Russ Cox

## License
Copyright (c) Chen Kai-Hung.

## Donate
If this project helps you reduce your development time, you can buy me a cup of coffee. :)

[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://paypal.me/khchen0915?country.x=TW&locale.x=zh_TW)
