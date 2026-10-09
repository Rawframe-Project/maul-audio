# Private backends

A platform whose audio API is under a non-disclosure agreement, a
console's for instance, cannot have its backend in this repository.
Such a backend lives in the studio's own repository and joins the
library when it is configured; the library is not forked (record
[maud-0002](adr/maud-0002-private-backends.md)).

## Building one in

Point `MAUL_AUDIO_PRIVATE_BACKEND` at a directory holding a
`backend.cmake`:

```sh
cmake -B build -DMAUL_AUDIO_PRIVATE_BACKEND=/path/to/console-backend
```

The library's build includes that file after making its target, so the
file adds the backend's sources to `maul-audio` and may link the
platform's libraries:

```cmake
target_sources(maul-audio PRIVATE "${CMAKE_CURRENT_LIST_DIR}/console_backend.c")
target_include_directories(maul-audio PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(maul-audio PRIVATE ConsoleAudio::ConsoleAudio)
```

The sources define one function, `maudGetPrivateBackend`, which returns
the backend's table (`maudBackend` in `src/backend.h`), its `kind`
`maud_backendPrivate`.

## Choosing it

With a private backend built, `maud_backendPrivate` asks for it, and
`maud_backendNative` tries it before the platform's own backends (a
console has no other). Without one, asking for `maud_backendPrivate` is
`maud_errorUnsupported`, as for any backend the build lacks.

## The contract

`src/backend.h` is the contract, as it is for the backends in this
repository: each hook's comment says what it must do and what may be
NULL, and `src/device.h` adds and removes devices. The header is not
stable API. A private backend tracks the library's version, as a
console build pinned to one release does, and moves with it.

The real-time rules bind it as they bind every backend: nothing on the
audio callback allocates, locks, blocks or waits; the library starts no
thread but an audio one (`src/worker.h` gives one where the platform
runs none).

`test/private_backend/` is such a backend, kept outside `src/`: one
output device rendered on the caller's thread. The gate and the CI's
device-only cell build the library with it and run its test.
