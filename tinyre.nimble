#====================================================================
#
#             TinyRE - Tiny Regex Engine for Nim
#                  Copyright (c) 2022 Ward
#
#====================================================================

# Package
version       = "1.1.0"
author        = "Ward"
description   = "TinyRE - Tiny Regex Engine for Nim"
license       = "MIT"
skipDirs      = @["docs"]
installFiles  = @["re.c"]

# Dependencies
requires "nim >= 1.6.0"
