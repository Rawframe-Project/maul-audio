# 0015. Family and library records

Status: Accepted

## Context

Records are numbered in one sequence. With eight libraries, a record
belonging to one library would take a number that means something else
in another.

## Decision

Family records are `NNNN-title.md`, identical in every library. A
library's own records are `P-NNNN-title.md`, with the library's
prefix, in the same directory, and are indexed in `docs/adr/P.md`.

## Consequences

Family records sync unchanged, and a library's records never collide
with another's.
