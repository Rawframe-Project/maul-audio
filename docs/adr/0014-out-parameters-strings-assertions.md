# 0014. Out-parameters, strings and assertions

Status: Accepted

## Context

Three conventions every API needs one answer to.

## Decision

Out-parameters end in `Out` (`windowIdOut`). Strings cross every API
as UTF-8 with an explicit byte length, never as NUL-terminated text
alone. The library's assert macro checks internal invariants in debug
and test builds and compiles to nothing in release builds.

## Consequences

APIs read the same in every library, strings from hostile sources are
never measured by searching for a terminator, and release builds pay
nothing for invariant checks.
