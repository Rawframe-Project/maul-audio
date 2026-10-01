# 0011. Code style checked by tools

Status: Accepted

## Context

Rules that tools check hold; rules that only reviewers check drift. In
Maul3D, formatting, warnings and lengths held, while the documentation
contract did not: most public functions lacked their thread-safety
line.

## Decision

Formatting stays Maul3D's (Allman braces, 4 spaces, 100 columns) and
clang-format also inserts braces, orders includes and places `const`.
Every rule in the rulebook names the tool that checks it, or is marked
as a review rule. A family source checker rejects what formatters do
not see: banned calls, assignments in conditions, `TODO`, history
markers, missing SPDX lines, mutable file-scope state. Public API
documentation is `///` with Doxygen commands, and a script checks that
every public function has a summary, its `@param` and `@return`
entries and a thread-safety paragraph.

## Consequences

The rules hold because a tool fails the build when they do not.
