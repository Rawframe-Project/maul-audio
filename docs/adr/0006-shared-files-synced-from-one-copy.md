# 0006. Shared files synced from one canonical copy

Status: Accepted

## Context

Record 0004 kept shared files as copies checked against the sibling
repository. With eight libraries, comparing every repository with every
other does not scale, and a copy changed in one place has no home to
return to.

## Decision

Shared files stay copies in every library that uses them; no library
depends on another for them, through a shared base library or a git
submodule. Their canonical copies live in the family's docs
repository, under `family/files/` (identical everywhere) and
`family/renamed/` (identical once the library's prefixes are
substituted). A sync script writes them into a library or reports every
difference. It runs where the libraries are checked out, not in CI, and
it must report no difference before a library is released.

## Consequences

Each library still builds and ships alone. A change to a shared file
is made once and synced. Record 0004 is superseded by this one.
