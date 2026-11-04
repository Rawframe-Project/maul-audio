#!/bin/bash
# Prints clang-tidy's --header-filter for a build tree: this library's
# headers under src/ and include/, anchored at the source directory the
# tree was configured from. CMake writes the include paths through that
# directory as it was given, so in a checkout reached through a symbolic
# link neither pwd nor pwd -P need match them; the tree's
# CMAKE_HOME_DIRECTORY does. The directory is escaped, so that a dot or
# a plus in it matches only itself.
#
# usage: tidy_filter.sh <build tree>
set -euo pipefail

home=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$1/CMakeCache.txt")
if [ -z "$home" ]; then
    echo "no CMAKE_HOME_DIRECTORY in $1/CMakeCache.txt" >&2
    exit 1
fi
printf '^%s/(src|include)/\n' "$(printf '%s' "$home" | sed 's/[][\\.^$*+?(){}|]/\\&/g')"
