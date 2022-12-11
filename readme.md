[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://paypal.me/khchen0915?country.x=TW&locale.x=zh_TW)

# TinyRE
TinyRE is a Nim wrap for a tiny regex engine less than 10K binary size (loc < 1K), and guarantees that input regex will scale O(n) with the size of the string.

## Features
* Support unicode.
* Support case-insensitive matching.
* Support global matching.
* Support most common regex syntax, including:
  * Greedy and non-greedy expressions: `*`, `+`, `?`, `*?`, `+?`, `??`
  * Characters sets: `[xyz]`, `[^xyz]`
  * Meta characters: `\s`, `\S` ,`\w`, `\W`, `\d`, `\D`, `\n`, `\r`, `\t` etc.
  * Ascii or unicode characters: `\x00`, `\u0000`, `\U00000000`
  * Beginning and end assertions: `^`, `$`.
  * Repetition operators: `{n}`, `{n,m}`, `{n,}`.
  * Group and non-capture group: `(...)`, `(?:...)`.
  * Start-of-word and end-of-word assertions: `\<`, `\>`.

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

## Docs
* https://khchen.github.io/tinyre

## Reference
* [pikevm](https://github.com/kyx0r/pikevm "pikevm") by Kyryl Melekhin
* [re1.5](https://github.com/pfalcon/re1.5 "re1.5") by Paul Sokolovsky
* [re1.0](https://code.google.com/archive/p/re1/ "re1.0") by Russel Cox
* [Regular Expression Matching: the Virtual Machine Approach](https://swtch.com/~rsc/regexp/regexp2.html "Regular Expression Matching: the Virtual Machine Approach") by Russel Cox

## License
Copyright (c) 2022 Kai-Hung Chen, Ward. All rights reserved.

## Donate
If this project help you reduce time to develop, you can give me a cup of coffee :)

[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://paypal.me/khchen0915?country.x=TW&locale.x=zh_TW)
