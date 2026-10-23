# Private backends

Some platforms' audio APIs are under a non-disclosure agreement, a console's for instance, so their backends cannot live in this repository. Such a backend lives in the developer's own repository and joins the library when it is configured, without a fork.

## Building one in

Point `MAUL_AUDIO_PRIVATE_BACKEND` at a directory that holds a `backend.cmake`:

```sh
cmake -B build -DMAUL_AUDIO_PRIVATE_BACKEND=/path/to/console-backend
```

The library's build includes that file once its target exists, so the file adds the backend's sources to `maul-audio` and links what the platform needs:

```cmake
target_sources(maul-audio PRIVATE "${CMAKE_CURRENT_LIST_DIR}/console_backend.c")
target_include_directories(maul-audio PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(maul-audio PRIVATE ConsoleAudio::ConsoleAudio)
```

The sources define one function, `maudGetPrivateBackend`, which returns the backend's table (`maudBackend` in [src/backend.h](../../src/backend.h)) with its kind set to `maud_backendPrivate`.

## Choosing it

With a private backend built in, `maud_backendPrivate` asks for it by name and `maud_backendNative` tries it before the platform's own backends. Without one, asking for `maud_backendPrivate` returns `maud_errorUnsupported`, as for any backend a build lacks.

## The contract

`src/backend.h` is the contract, the same one the backends in this repository follow: each hook's comment says what it must do and which hooks may be NULL, and `src/device.h` says how a backend reports devices coming and going. It is not a stable interface. A private backend is built against one release of the library and moves with it.

The library's real-time rules bind a private backend as they bind every other: nothing on the audio thread allocates, locks or waits, and the backend starts no thread of its own except, where the platform runs no audio thread, the stream thread `src/worker.h` provides.

`test/private_backend/` is a minimal backend kept outside `src/` (one output device, rendered on the caller's thread); CI builds the library with it to prove the slot works from outside the tree.
