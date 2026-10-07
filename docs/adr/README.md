# Design records

Each record explains one decision: the problem, the choice and what it
costs.

Family records apply to every Maul library and are identical in every
repository; they are numbered `NNNN` and listed below. A library's own
records carry its prefix (`P-NNNN`) and are listed in `docs/adr/P.md`
(record 0015).

Records are numbered in order and never renumbered. A decision that is
reversed gets a new record that supersedes the old one; the old one
stays, marked superseded.

## Family records

| Number | Title | Status |
|---|---|---|
| [0001](0001-one-family-one-set-of-rules.md) | One family, one set of rules | Accepted |
| [0002](0002-refusals-record-a-reason.md) | Refusals record a reason and never assert | Superseded by 0009 |
| [0003](0003-restart-at-0-0-1.md) | Restart the version history at 0.0.1 | Accepted |
| [0004](0004-shared-code-by-copy.md) | Shared code by copy, checked for drift | Superseded by 0006 |
| [0005](0005-family-core-and-library-profiles.md) | Family core and library profiles | Accepted |
| [0006](0006-shared-files-synced-from-one-copy.md) | Shared files synced from one canonical copy | Accepted |
| [0007](0007-naming-prefixes.md) | Naming prefixes | Accepted |
| [0008](0008-c23-with-portable-headers.md) | C23 implementations, portable headers | Accepted |
| [0009](0009-fallible-calls-return-their-status.md) | A fallible call returns its status | Accepted |
| [0010](0010-allocators-per-object.md) | Allocators are per object | Accepted |
| [0011](0011-code-style-checked-by-tools.md) | Code style checked by tools | Accepted |
| [0012](0012-structure-limits-and-module-graph.md) | Structure limits and a module graph | Accepted |
| [0013](0013-performance-discipline.md) | Performance discipline | Accepted |
| [0014](0014-out-parameters-strings-assertions.md) | Out-parameters, strings and assertions | Accepted |
| [0015](0015-record-naming.md) | Family and library records | Accepted |
| [0016](0016-handles.md) | Roots are pointers, what they own are generation-checked ids | Accepted |
| [0017](0017-threads.md) | No threads of the library's own, platform threads by contract, typed thread safety | Accepted |
| [0018](0018-asynchronous-requests.md) | Asynchronous requests: an id now, exactly one completion later | Accepted |
| [0019](0019-guide-snippets-run-by-tests.md) | The guide's snippets are built and run by tests | Accepted |
| [0020](0020-parts-with-their-own-sources.md) | A part with its own sources lives beside src/ | Accepted |

## Template

```md
# NNNN. Title in sentence case

Status: Proposed | Accepted | Superseded by NNNN

## Context

What problem forced a decision, with the facts that matter.

## Decision

What we do, stated so that code review can check it.

## Consequences

What this costs, what it enables, and what we give up.
```
