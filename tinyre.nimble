#====================================================================
#
#             TinyRE - A Tiny Regex Engine for Nim
#              Copyright (c) Chen Kai-Hung, Ward
#
#====================================================================

# Package
version       = "1.5.2"
author        = "Ward"
description   = "TinyRE - A Tiny Regex Engine for Nim"
license       = "MIT"
skipDirs      = @["docs"]
installFiles  = @["re.c"]

# Dependencies
requires "nim >= 1.6.0"
