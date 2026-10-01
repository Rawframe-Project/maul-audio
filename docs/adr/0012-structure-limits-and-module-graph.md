# 0012. Structure limits and a module graph

Status: Accepted

## Context

Line limits keep functions short but not simple, and nothing kept a
library's files from depending on each other in any direction.

## Decision

A source file stays under 1000 lines and a function under 80, with a
shrinking exceptions list. A function's cognitive complexity, as
clang-tidy measures it, stays at or under 25; a function kept above it
carries a `NOLINT` for that check with its reason, and a script lists
those exceptions. Each library declares its internal module graph in
`tools/modules.txt`, and a check fails on an include the graph does not
allow and on any cycle. A struct holds one concept; a report of field
counts keeps growth visible.

## Consequences

Code stays small, flat and layered, and every exception is visible.
