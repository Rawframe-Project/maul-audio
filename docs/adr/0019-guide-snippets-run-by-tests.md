# 0019. The guide's snippets are built and run by tests

Status: Accepted

## Context

A guide's C snippets are compiled nowhere, so they drift from the API:
one library's snippets were found ignoring the results of calls marked
nodiscard, which its own build rejects. Rust's doctests and Python's
doctest check every example unless it is marked otherwise.

## Decision

Every C block of `docs/guide.md` appears unchanged in one of
`test/test_guide*.c`, every non-blank line at one indentation, and the
tests build and run it, giving it what it uses and checking what it
keeps. A whole program that waits for its user is built with the tests
and not run. A fragment no test can build everywhere is marked
`<!-- guide: not run: REASON -->` right above its fence.
`tools/check_guide.py` checks this in CI and prints every reason.

## Consequences

A snippet that no longer builds fails the tests instead of misleading
a reader. The guide keeps its prose and its code side by side, at the
cost of a copy of each snippet in a test, which the check keeps equal.
