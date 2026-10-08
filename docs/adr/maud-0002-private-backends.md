# maud-0002. Private backends join at configure time

Status: Accepted

## Context

A platform whose audio API is under a non-disclosure agreement, a
console's, cannot have its backend in a public repository, and the
requirements ask that such a backend be possible without forking the
library. Libraries answer this three ways: a public backend interface
(miniaudio's custom backend vtable), a fork per platform (SDL3's
consoles, cubeb), or a slot the build fills (SDL3's reserved private
audio driver, though only its forks fill it).

## Decision

- A CMake cache variable, `MAUL_AUDIO_PRIVATE_BACKEND`, names a
  directory outside the repository holding a `backend.cmake`. The
  library's build includes it; its sources, added to the library,
  define `maudGetPrivateBackend` against `src/backend.h`, as the
  backends in the repository do.
- A reserved kind, `maud_backendPrivate`, asks for it. Built, it comes
  first in the native order; unbuilt, asking for it is unsupported.
- `src/backend.h` is the contract and is not stable API: a private
  backend tracks the library's version.
- A test backend kept outside `src/` (`test/private_backend/`) is built
  by the CI, so the slot is proven from outside the tree.
  `docs/private-backends.md` states the contract.

## Consequences

- No fork, and one enum value of public API.
- The core stays free to change how it talks to its backends
  (publishing a stream's state, following devices, the drain, the
  period loop); a public backend interface would have frozen all of it
  for backends the library cannot see.
- A private backend is rebuilt with each library release it moves to;
  console builds compile from source and pin a version anyway.
