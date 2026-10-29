# Environments: the spatializer

The effects in [Spatial audio](spatial-audio.md) take parameters: how far a source is, how much a wall hides it, how long the room rings. A program can set them by hand, or let the spatializer (`maul-audio/spatializer.h`) work them out from its world: occlusion and transmission through geometry, reverberation times from the room's shape, geometric reflections, and paths around corners. It is the Spatial part's root object.

## Contents

- [Two sides](#two-sides)
- [The program's geometry, or the library's](#the-programs-geometry-or-the-librarys)
- [Sources and the direct step](#sources-and-the-direct-step)
- [The room: estimates and reflections](#the-room-estimates-and-reflections)
- [Probe sets: paths and baking](#probe-sets-paths-and-baking)
- [Limits](#limits)

## Two sides

A spatializer has two sides that run on different threads.

- **The simulation side** runs on the program's threads, at a rate the program picks (a game might run a direct step every frame and a reverberation estimate every second). It makes and destroys sources, sets their poses, and runs steps that trace rays through the scene. Steps publish their results whole.
- **The rendering side** runs on the audio thread. `maudSpatializer_LatchResults` takes the newest published step, and `maudSource_GetDirectResult`, `maudSpatializer_GetReverbResult` and `maudSpatializer_RenderReflections` read from it, never torn, without allocating, locking or waiting. The program then passes the results to the direct effect, the binaural effect or a panner, and the reverb.

A source made after the latched step, or destroyed before it, reads `maud_errorStale` until the next latch catches up.

Positions on the simulation side are in metres in the program's world, right-handed with +y up; an orientation turns the frame of the listener or a source (+x right, +y up, -z ahead) into the world's. Results come back in the listener's frame, ready for the effects.

## The program's geometry, or the library's

The spatializer does not own the level's geometry. It asks two questions through hooks in its def: `anyHit` (does each of these rays hit anything within its distances?) and `closestHit` (where is each ray's nearest hit, its normal and its material?). A program with its own physics or ray tracer answers them there, so level geometry is never copied. An answer must depend only on the ray and the geometry.

A program without a ray tracer, or one that needs results that match on every platform, uses an acoustic scene (`maul-audio/scene.h`): the library's own geometry and tracer. Static meshes are fixed when the scene is made; instances of its instancing meshes are made, moved and destroyed at any time and take effect at `maudScene_Commit`. `maudFindAnyHit` and `maudFindClosestHit` have the hooks' signatures, with the scene as their context. Between commits any number of threads may query a scene; changes and commits must not overlap queries.

Each triangle names a material in the spatializer's table (`maudSpatializer_SetMaterials`): what it absorbs and lets through per band, and how much it scatters.

With the task hooks (`enqueueTask` and `finishTask`) set, a step spreads its rays over the program's task system. Results never depend on how the tasks split the work: rays go out in a fixed order in batches of 64, and every sum is taken in the same order. Nor do they depend on the floating-point mode of the threads that run them: each task, like each simulation call, computes in round to nearest with subnormals kept and leaves the thread's mode as it found it. An engine that runs its threads with flush-to-zero or another rounding mode gets the same results. Its ray queries run in the library's mode as well, not in the thread's own, so its tracer gives the same hits on every thread. Its allocator and its `enqueueTask` and `finishTask` run in the thread's own mode.

## Sources and the direct step

`maudCreateSource` makes a source with a directivity pattern and an occlusion method: a single ray from the listener, or a sphere of sample points (volumetric), which lets a source fade as it moves behind an edge instead of switching. `maudSource_SetPose` places it.

`maudSpatializer_SimulateDirect` runs a direct step for a listener. For each source it finds the distance, the direction in the listener's frame, the directivity, the occlusion and, for occluded sources that ask, the transmission through each surface crossed (up to `maxSurfaces`). A source with `enablePathing` set that is occluded looks for a path around the obstacle over the probe set in use; when one is found, the result's distance is the path's length and its direction blends the path's with the straight line's.

The result's fields map onto `maudDirectParams` and `maudBinauralParams`; the program applies its own distance curve to the distance.

## The room: estimates and reflections

- **Reverberation estimates.** `maudSpatializer_SimulateReverb` traces `reverbRays` rays from the listener, gathers the energy that comes back per band in 10 ms bins, and fits the decay. The next direct step publishes the times, with the send level and delay that give the reverb the traced energy; `maudSpatializer_GetReverbResult` reads them for `maudReverbParams`. Where a band decays in two slopes (a room fed by a livelier one through a door), the result also gives the slower slope for a reverb made with a tail.
- **Geometric reflections.** A def with `reflectionOrder` 1 to 3 also turns the same rays' energy, by direction of arrival, into a response, which `maudSpatializer_RenderReflections` convolves with the program's send into an ambisonic bed in the listener's frame. A new response takes over smoothly, within about 128 ms for a one-second response at 48 kHz. The early reflections then come from the response and the reverb takes over where it ends.

Estimates cost rays: about 1,000 for a diffuse room, about 8,000 for a smooth room with one absorbent surface. They need the closest-hit hook.

## Probe sets: paths and baking

A probe set is a graph of points in the walkable space, two probes linked when one ray connects them within the set's range. The program gives the points, or the spatializer generates them on a grid over a box, at a height above every floor it finds.

- **Paths.** `maudSpatializer_SetPathingProbeSet` names the set occluded sources search for paths over. The search attaches each end to the nearest probes it sees, then refines the path with rays until its corners lie on the edges of the obstacles, and turns each corner into diffraction per band.
- **Baking.** `maudProbeSet_Bake` runs a reverberation estimate at every probe, with the energy field when the spatializer renders reflections. `maudSpatializer_SetReverbProbeSet` then makes estimates blend the four nearest baked probes the listener sees, so a running program traces nothing for its reverb. A bake gives the same bits on every platform, whatever the task hooks do and whatever the threads' floating-point modes.
- **Files.** `maudProbeSet_Save` writes a set to `.maudbake` bytes, the same on every platform, for a program to ship with its level. A refused save zeroes the caller's buffer, so it never holds an earlier file, and a buffer too short still learns the size it needs. `maudLoadProbeSet` reads them back, treating them as hostile. A file with a bake loads only into a spatializer whose reflections match it. A file of another format version, such as a version-2 bake from before bakes were made of tagged fields, is refused with `maud_errorVersion`, as is a file holding a field only a later version writes: bake the scene again and save it. See [File formats](../internals/file-formats.md).

## Limits

Every count is a limit in a def: sources, occlusion points, surfaces per walk, materials, rays, probes, probe pairs, paths, triangles, instances. The spatializer allocates for them when it is made and takes nothing more; going past one is `maud_errorCapacity`, never unbounded work.

The ray hooks, the task hooks and the allocator run while the spatializer works. They may call its rendering side and `maudSpatializer_GetMisuse`, nothing else: any other call that changes it is refused with `maud_errorBusy` and counted as misuse. The simulation side is used by one thread at a time; the rendering side by one thread at a time too, which may be another.
