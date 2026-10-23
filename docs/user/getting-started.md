# Getting started

Maul Audio has two parts that build and work apart. The Device part moves interleaved float frames between a program and the platform's audio devices. The Spatial part renders sources in space for headphones or speakers. A program links the part it uses.

## Requirements

- CMake 3.25, and GCC 14 or Clang 19 or newer. On Windows, `clang-cl` (Visual Studio's "C++ Clang tools for Windows" component). The library is C23; its public headers are C17 and C++17, so any C or C++ program can include them.
- On Linux, the development headers of PipeWire (`libpipewire-0.3`), PulseAudio (`libpulse`) and ALSA (`alsa`). Only the headers are needed to build: the library opens each one at run time, so a program runs where any of them is missing.
- Nothing else. The library takes no dependency beyond the platform's own audio APIs.

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix /usr/local
```

A program finds the installed package with CMake:

```cmake
find_package(maul-audio REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-audio::maul-audio)
```

or with pkg-config as `maul-audio` (add `--static` for the static library). `samples/minimal` is such a program, a project that knows only the installed package.

## Build options

| Option | Default | What it does |
|---|---|---|
| `MAUL_AUDIO_DEVICE` | `ON` | The Device part: contexts, devices, streams, voice processing. |
| `MAUL_AUDIO_SPATIAL` | `ON` | The Spatial part: HRTF sets, effects, the ambisonic bed, the spatializer. |
| `MAUL_AUDIO_BUILD_SHARED` | `OFF` | A shared library instead of a static one. |
| `MAUL_AUDIO_PIPEWIRE`, `MAUL_AUDIO_PULSE`, `MAUL_AUDIO_ALSA` | `ON` on Linux | Each Linux backend. |
| `MAUL_AUDIO_WASAPI`, `MAUL_AUDIO_COREAUDIO`, `MAUL_AUDIO_IOS`, `MAUL_AUDIO_AAUDIO` | on their platform | The other native backends. |
| `MAUL_AUDIO_PRIVATE_BACKEND` | empty | A directory holding a backend kept outside the library; see [Private backends](private-backends.md). |
| `MAUL_AUDIO_BUILD_TESTS`, `MAUL_AUDIO_BUILD_BENCH`, `MAUL_AUDIO_BUILD_SAMPLES`, `MAUL_AUDIO_INSTALL` | `ON` at the top level | Tests, benchmarks, samples and install rules; off when the library is added to another project. |

`-DMAUL_AUDIO_SPATIAL=OFF` builds the Device part alone, `-DMAUL_AUDIO_DEVICE=OFF` the Spatial part alone. Neither part's code calls the other's.

## The model

- **Results.** Every call that can fail returns a `maudResult`: zero for success, a positive value for an outcome that is not an error (`maud_empty`), a negative one for an error. The compiler warns when one is dropped. `maudResultName` names any of them. The library prints and logs nothing.
- **Defs.** Every object is made from a def that `maudDefault...Def` returns. Change the fields you need and pass it on. A def carries a cookie, so a def built by hand without it is refused with `maud_errorInvalid`.
- **Versions.** The defaults of the objects made without a parent (a context, an HRTF set, an effect, a spatializer, a voice processor) are inline in the headers and stamp the version the program was built with. A library of another version refuses them with `maud_errorVersion`, so a program never runs against a library whose structs are laid out differently.
- **Handles.** A context, a spatializer and the standalone effects are pointers. Devices, streams, sources and probe sets are ids: a 1-based slot and a generation. An id whose object is gone is refused with `maud_errorStale`, never mistaken for another object.
- **Threads.** A context is used by one thread at a time. A stream's callback runs on an audio thread; it must not allocate, lock or wait, and the library holds itself to the same rule there. Each function's documentation says which threads may call it.
- **Memory.** Every owner object takes an allocator in its def (a zeroed one means the C library's) and allocates when it is made, sized by named limits. Nothing allocates on the audio thread.
- **Misuse.** An invalid argument to a context or a spatializer, or a call back into one from code it is running, counts one misuse; `maudGetContextMisuse` and `maudGetSpatializerMisuse` read the count, so a program can catch its own bugs in a release build.

## Opening a context

```c
maudContextDef def = maudDefaultContextDef();   // the platform's backend
maudContext* context = NULL;
maudResult made = maudCreateContext(&def, &context);
if (made != maud_success)
{
    // maud_errorUnsupported: no backend runs here, such as a Linux
    // machine without an audio service.
}
```

From there, [Devices and streams](devices-and-streams.md) continues with the Device part and [Spatial audio](spatial-audio.md) with the Spatial part.

## Samples

- `sample_devices [seconds] [capture]` lists the devices, plays a quiet tone on the default output and prints every notification for 60 seconds or the number given; with `capture` it also captures the default input and prints its peak each second. On the web it builds as a page that starts at the first click.
- `sample_binaural <set.maudhrtf> <out.wav>` renders a source circling the listener to a WAV file, without a device: `sample_binaural data/hrtf/sadie2-ku100-48k.maudhrtf circle.wav`.
