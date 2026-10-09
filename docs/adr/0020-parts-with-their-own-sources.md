# 0020. A part with its own sources lives beside src/

Status: Accepted

## Context

A library may ship a part built as a library of its own from its own
sources, such as a renderer on another Maul library. The family's
checks read `src/` alone, so such sources escaped the rules every
library's sources keep, or each library edited the shared checks, which
must stay identical everywhere (record 0006).

## Decision

Such a part lives in a directory beside `src/`, its sources in
`DIR/src` and its public headers in `DIR/include`, and the library lists
the directory in `tools/source-dirs.txt`. The family's checks read the
list: the source rules for `src/` hold in `DIR/src`, the length rules
measure its files (named by their path), its headers count toward the
structure limits, and its sources include their own headers and public
ones alone, never the library's internals. The API reference
(`tools/gen_api.py`) lists each part's headers, `DIR/include/NAME`, in
a section of its own after the library's. A library without the file
is checked as before.

## Consequences

Every source a library ships keeps the family's rules, through the same
shared checks, with one line per part, and its reference lists the
part's functions. A part cannot reach into its library's internals, so
it stays as separable as its own build target.
