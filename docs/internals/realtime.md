# Real-time rules

An audio thread has a deadline every period: miss it and the listener hears a click. Nothing on it may take a lock another thread can hold, allocate (allocators lock), make a system call that can block, or wait on another thread. This page says how the library keeps its own code to that rule, and how it keeps host code from breaking its state.

## What runs on an audio thread

- The **period loop**: the platform's callback (or the library's stream thread on WASAPI, ALSA and PulseAudio) moves the platform's buffer through the fixed-period adapter ([period.h](../../src/period.h)), which calls the host's callback once per period. On the web the "audio thread" of a stream is the page's main thread, rendering ahead into a queue the worklet plays (see [The web](web.md)).
- The **host's callback**, the one application function the library calls there, declared real-time in `maudStreamCallback`'s documentation.
- The **stamps and counters** the loop updates: the stream clock ([clock.h](../../src/clock.h)), xruns ([xrun.h](../../src/xrun.h)), a duplex stream's ring ([duplex.h](../../src/duplex.h)).
- On the host's own audio thread, the Spatial part's processing: every effect's `Process` call, a bed's encoding, rotation and decoding, and the spatializer's rendering side.

None of these allocates, locks or waits. A stream's memory (its period buffer, a duplex ring, an object stream's objects) is one block allocated when the stream is made ([stream_open.c](../../src/stream_open.c)); an effect's buffers are allocated when it is made, sized by its def's `maxFrames` or limits.

## How state crosses threads

The control thread (whichever thread uses the context) and an audio thread share a stream's state through atomics only:

- The **run state** the audio thread reads (running or idle) is one atomic the control thread sets; a suspended or stopped stream's loop renders silence and calls nothing.
- A **rate change** after a move is published by the control thread and applied by the loop at the next period boundary.
- The **stream clock** is written by the rendering thread at each callback and read by any thread under a sequence counter, without a lock: a reader that sees the counter move reads again.
- The spatializer's **results** pass through three buffers: the simulation side writes the back one and swaps it with the waiting one under an atomic exchange, flagged fresh; `maudLatchResults` swaps the waiting one into the front if it is fresh. A step is published whole, and the rendering side never waits for the simulation side, which may take a whole second over an estimate. The reflections' responses pass through four buffers, since a crossfade still reads the one just replaced ([reflections.h](../../src/reflections.h)).

Destroying a stream detaches its platform side first; the backend's `detachStream` returns only once no platform thread touches the stream, so `maudDestroyStream` returning means the callback has run for the last time.

## Keeping denormals away

A decaying filter state or delay line reaches the subnormal range within seconds of silence, where each multiply is many times slower; an effect that idles in silence would suddenly cost its whole budget. [flush.h](../../src/flush.h) sets such states to zero once a block where that is safe, and on every sample only where a block could carry a state across the floor and back. The realtime side never changes the thread's floating-point mode: the audio thread is the host's, and nothing it renders is promised the same bits across environments. The simulation side does: its computing calls and the tasks they hand the host's hooks set round to nearest with subnormals kept and restore the thread's mode before they return ([fpenv.h](../../src/fpenv.h), a family file), since bakes, estimates and paths are promised the same bits whatever the host's flush-to-zero or rounding mode. Host code they call runs in the library's mode only where its result feeds those bits: the ray queries. The allocator and the task hooks' `enqueueTask` and `finishTask` run in the host's own mode ([allocator.c](../../src/allocator.c), [host_tasks.h](../../src/host_tasks.h)). `tools/check_float_env.py` holds every public function to this: each either enters through a wrapper of one shape or is named, under its reason, in `tools/float-env-allowed.txt`.

## Host code that calls back in

The library calls host code in several places: the stream callback, every owner object's allocator, the spatializer's ray queries and task hooks. Such code must not call the object it serves, except the calls named for it, because the object is in the middle of changing its state. Each owner object refuses such a call with `maud_errorState` and counts one misuse:

- **The context** records the thread that renders each stream and the thread running its allocator ([context_core.h](../../src/context_core.h), [thread.h](../../src/thread.h)). A control call on either thread is refused. The stream callback may read its stream's format, position and clock, the host clock and the misuse count, and render or feed another offline stream; rendering its own stream again is refused too. The context's allocator runs on the calling thread while another thread may legitimately render, so the marker is a thread, not a flag.
- **The spatializer** holds a flag while it runs host code ([spatializer_state.h](../../src/spatializer_state.h)). Its hooks run on any of the host's task threads, so only an object-wide flag sees them; its simulation side is used by one thread at a time, so the flag refuses nothing legitimate. Every function that changes it checks the flag with one atomic load, so `maudSetSourcePose`, called thousands of times a frame, costs what it would without the check. The hooks may call the rendering side and the misuse count.
- **Every other object** (an HRTF set, an effect, a processor, a scene) calls its allocator only while it is made or destroyed, when the host holds no valid handle to it, so there is nothing to refuse.

## Testing it

`test_stall` runs a native output stream whose callback latches the spatializer every block while the host steps it, and stalls the host's ray query for 50 ms in the middle of a step: the stream must keep running without an underrun and every latched step must be whole. `test_call_back_in` and `test_spatializer_call_back_in` call every refused function from inside the callback, the allocator and each hook. The ThreadSanitizer cells run every stream test.
