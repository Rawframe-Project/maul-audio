# Propagation

How the spatializer finds what the world does to sound (occlusion, transmission, reverberation, reflections, paths) and how it keeps its answers the same however the host runs it.

## Contents

- [Determinism](#determinism)
- [Rays and the host's geometry](#rays-and-the-hosts-geometry)
- [Scenes](#scenes)
- [The direct step](#the-direct-step)
- [Reverberation estimates](#reverberation-estimates)
- [Reflections](#reflections)
- [Probe sets and pathing](#probe-sets-and-pathing)
- [Bakes](#bakes)

## Determinism

The spatializer runs on the host's tasks, in whatever split the host's scheduler picks, so every result that leaves it is defined to not depend on the split:

- Rays are generated in a fixed order and sent in batches of 64. A batch's results land in the batch's own slots, and every sum over batches (a histogram, a probe's links) is taken in batch order afterwards.
- Randomness is a hash of the ray's index and the bounce, never a shared generator.
- Ties break by index: a path search prefers the lower probe, a scene's static hierarchy is asked before its instances.
- Code whose output is stored (a bake) or compared across platforms uses the library's own `exp`, `log`, `sin` and `cos` ([portable_math.h](../../src/portable_math.h)), built from correctly rounded operations with fixed polynomials, about twice glibc's time. The C library's versions differ by an ulp between platforms, which changes a bake's bytes.

A bake is therefore bit-exact on every platform, compiler and architecture, and a simulation step gives the same results however the tasks run. `test_spatializer_bake` bakes the office scene again and compares its bytes with `data/bake/office.maudbake`.

## Rays and the host's geometry

The spatializer never holds the level. It asks the host's any-hit and closest-hit hooks about batches of rays, so a game reuses its physics engine's acceleration structure and the level is never copied. Rays are built in double from world positions and handed over as unit directions with lengths ([occlusion.c](../../src/occlusion.c)); a ray of no length (a listener at the source) points ahead with length 0, so no hit falls within it.

## Scenes

An acoustic scene ([scene.c](../../src/scene.c), [scene_instances.c](../../src/scene_instances.c)) is the library's own geometry for hosts without a tracer and for results that must match everywhere. Static meshes are copied at creation into one bounding volume hierarchy, built by binned SAH ([bvh.h](../../src/bvh.h)), the same on every platform. Instancing meshes get a hierarchy each; a commit places every live instance and builds a top level over their bounds. A ray enters an instance's frame through its inverse transform without renormalizing, so its distances stay the world's.

Triangles are hit by the watertight test of Woop, Benthin and Wald ([watertight.h](../../src/watertight.h)): a ray through a shared edge or vertex hits exactly one of the triangles there, so a closed room never leaks a ray through a seam between two triangles.

## The direct step

For each source, the step computes distance, direction in the listener's frame and directivity from the poses ([direct_step.h](../../src/direct_step.h)), then occlusion ([occlusion.h](../../src/occlusion.h)):

- **One ray**: from the listener to the source, occluded or not.
- **Volumetric**: sample points in a sphere around the source; for each, does the source see it, and does the listener? The occlusion is the share of the points the source sees that the listener does not, so a large source fades out behind an edge instead of switching off.

An occluded source that asks for transmission is then walked ([transmission.h](../../src/transmission.h)): from the listener towards the source, one closest hit at a time, each surface crossed multiplying the transmission by its material's, up to `maxSurfaces`. Every query starts at the listener with its minimum distance just past the last surface, rather than at the last hit, so no offset accumulates over the walk.

## Reverberation estimates

An estimate ([reverb_estimate.c](../../src/reverb_estimate.c)) traces rays from the listener in directions on a Fibonacci lattice. At each hit, a shadow ray back to the listener adds the energy the surface reflects toward it (a Lambert share for scattering and a specular lobe for the rest, over the distance's spread and the air's absorption) into 10 ms bins per band; the ray continues mirrored or, with the scattering's probability, diffusely, until each band has lost 40 dB, the bins end or the bounces run out. Each band's histogram is integrated backwards (Schroeder's method) and the decay fitted from -5 to -25 dB.

Where a band's decay bends (a room fed through a door by a livelier one), the fit finds two slopes and reports the slower as the tail. The estimate also gives the send level and delay that make the reverb's energy match the traced energy: at 100 ms without reflections, or where the reflections' response ends with them.

## Reflections

With `reflectionOrder` above 0, the same rays' energy is also gathered on spherical harmonics of its direction of arrival: an energy field per band and 10 ms bin. A response is made from it ([reflection_response.h](../../src/reflection_response.h)): per bin, a frame of noise from a fixed sequence, shaped in frequency by the bin's band energies and overlap-added, so each sample's expected power is the bin's energy. The channels share the noise, each scaled per band by its ratio to the omnidirectional channel. The result is the room's early sound with the right energy, direction and decay, without tracing an image source per reflection.

The response is published to the rendering side through four buffers and convolved with the host's send ([reflections.h](../../src/reflections.h)) in two stages after Gardner: 128-frame blocks for its start and longer blocks for the rest, so a one-second response costs a fraction of a single-stage convolution and adds no latency beyond the first block ([staged.h](../../src/staged.h), [partitioned.h](../../src/partitioned.h)). A new response takes over at the next long-block boundary, crossfaded.

## Probe sets and pathing

A probe set ([probe_graph.h](../../src/probe_graph.h)) is points in walkable space and the links between pairs within range that one any-hit ray connects. Generated probes go down columns of a grid over a box, one at a set height above every horizontal surface with that height free above it; generation walks each column twice, once to count its floors and once to place them, so its memory is a byte and a word a column. Pairs within range come from a sweep along x, sorted by index, so the links fill compressed rows in a fixed order.

Pathing ([pathing.h](../../src/pathing.h)) gives an occluded source a way around what hides it:

1. **Search** ([path_search.h](../../src/path_search.h)). Each end attaches to the nearest probes it sees within the set's range, and Dijkstra's search runs over the links. Links the step finds blocked (a door closed since the set was made) are left out and the search runs again, three times at most.
2. **Refine** ([path_refine.h](../../src/path_refine.h)). Vertices whose neighbours see each other are dropped; each remaining vertex slides along its legs while the other leg stays visible, then a pattern search moves it while that shortens the path and both legs stay visible. Around corners and through doors this lands vertices on the edges a probe graph only comes near.
3. **Diffract.** Each corner attenuates per band as a half-plane does in the uniform theory of diffraction, and a path's corners multiply.

The result's distance becomes the path's length; its direction and directivity blend the path's with the straight line's by the energy each brings; its transmission per band is the energy sum of what diffracts around and what passes through, at most 1.

## Bakes

`maudBakeProbeSet` runs an estimate at every probe ([probe_bake.h](../../src/probe_bake.h)), with the energy field when the spatializer renders reflections. At run time a listener blends the four nearest probes it sees within range, weighted by inverse distance less the fifth nearest's (Shepard's weights cut off so that a probe's weight reaches zero before it leaves the four, and moving is smooth); times blend in log, levels in dB, fields linearly. A point on a probe takes that probe's values exactly. With no probe in sight, the estimate traces as without a bake.
