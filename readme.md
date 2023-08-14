[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://paypal.me/khchen0915?country.x=TW&locale.x=zh_TW)

# TinyRE
TinyRE is a Nim wrap for a tiny regex engine based on Rob Pike's VM
implementation. Compare to other small regex engines, this engine supports
unicode and most of common regex syntax, in less than 10K code size (LOC < 1K),
and guarantees that input regex will scale O(n) with the size of the string.

## Features
* Support unicode.
* Support case-insensitive matching.
* Support global matching.
* Support most common regex syntax, including:
  * Greedy and non-greedy expressions: `*`, `+`, `?`, `*?`, `+?`, `??`.
  * Characters sets: `[xyz]`, `[a-z]`, `[^xyz]`, `[^m-z]`.
  * Meta characters: `\s`, `\S` ,`\w`, `\W`, `\d`, `\D`, `\n`, `\r`, `\t` etc.
  * Ascii or unicode characters: `\x00`, `\u0000`, `\U00000000`
  * Alternation operator: `|`.
  * Beginning and end assertions: `^`, `$`.
  * Repetition operators: `{n}`, `{n,m}`, `{n,}`.
  * Non-greedy repetition operators: `{n,m}?`, `{n,}?`.
  * Group and non-capture group: `(...)`, `(?:...)`.
  * Start-of-word, end-of-word, and nonword boundary assertions: `\<`, `\>`, `\B`.

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

# reU for utf8 matching
doAssert match("中文", reU"..") == @["中文"]
```

## Code Size

Comparing the code size of different regex library.

```nim
# gcc: version 11.1.0 MinGW-W64
# compile options: -d:release -d:danger --opt:size -d:lto -d:strip

# test codes
echo contains("abc123def", "123") # for strutils
echo contains("abc123def", re"\d+") # for regex library

# Results
strutils:       65,536 bytes (no dependence)
std/re:         68,608 bytes (dependence: pcre64.dll: 526,336 bytes)
tinyre:         75,264 bytes (no dependence)
nim regex:     296,448 bytes (no dependence)
```

## Performance

In summary, faster than `std/re` in small string, but slower than `std/re`
in large string. Here is the benchmark result on my computer. The test file
and pattern is from https://github.com/mariomka/regex-benchmark.

```nim
# small string: "abc123def".contains("\d+")
# large string: 6.71 MB text file
#   email: [\w\.+-]+@[\w\.-]+\.[\w\.-]+
#   uri: [\w]+://[^/\s?#]+[^\s?#]+(?:\?[^\s#]*)?(?:#[^\s]*)?
#   ipv4: (?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9])\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9])
# compile options: -d:release -d:danger --opt:speed -d:lto

name ............................... min time      avg time    std dv   runs
tinyre (small string) .............. 0.366 ms      0.379 ms    ±0.018  x1000
std/re (small string) .............. 5.862 ms      6.218 ms    ±0.171   x797
nim-regex (small string) .......... 16.132 ms     17.067 ms    ±0.580   x288
tinyre (large string, email) ..... 140.684 ms    151.663 ms    ±8.625    x33
std/re (large string, email) ...... 44.793 ms     48.884 ms    ±2.716   x102
nim-regex (large string, email) .... 3.680 ms      3.921 ms    ±0.132  x1000
tinyre (large string, uri) ....... 127.465 ms    131.721 ms    ±2.110    x38
std/re (large string, uri) ........ 40.380 ms     42.812 ms    ±1.175   x117
nim-regex (large string, uri) ..... 21.400 ms     22.205 ms    ±0.344   x225
tinyre (large string, ipv4) ...... 182.995 ms    186.441 ms    ±1.057    x27
std/re (large string, ipv4) ........ 4.854 ms      5.965 ms    ±0.903   x838
nim-regex (large string, ipv4) ..... 7.569 ms      7.849 ms    ±0.159   x635
```

## Docs
* https://khchen.github.io/tinyre

## Reference
* [pikevm](https://github.com/kyx0r/pikevm "pikevm") by Kyryl Melekhin
* [re1.5](https://github.com/pfalcon/re1.5 "re1.5") by Paul Sokolovsky
* [re1.0](https://code.google.com/archive/p/re1/ "re1.0") by Russel Cox
* [Regular Expression Matching: the Virtual Machine Approach](https://swtch.com/~rsc/regexp/regexp2.html "Regular Expression Matching: the Virtual Machine Approach") by Russel Cox

## License
Copyright (c) Chen Kai-Hung, Ward. All rights reserved.

## Donate
If this project help you reduce time to develop, you can give me a cup of coffee :)

[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://paypal.me/khchen0915?country.x=TW&locale.x=zh_TW)
