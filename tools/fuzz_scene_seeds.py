#!/usr/bin/env python3
# Writes fuzz_scene's seeds, built here rather than kept as files: a
# closed room with the host's probes in it, the room with a wall between
# them, at two ranges, the room's probes generated over it, eight small
# triangles near x = +3e38 and -3e38, and generations over a box too
# wide for its columns, a box whose top is near 3e38 and a column at
# x = 3e38, the walled room's instances made, moved and destroyed, and
# a cube turned with its diagonal along x, placed where the padding's sum
# just overflows and just below where it would.
# The layout is the one test/fuzz_scene.c reads: a mode byte (the range
# in its low two bits, generation in its third, instances in its
# fourth), the vertex and triangle counts, the vertices as floats, a
# byte per index, a generation's box corners, spacing and height as
# floats, with instances their capacity, the call count and the calls,
# then the points.
#
# Usage: python3 tools/fuzz_scene_seeds.py <directory>

import hashlib
import os
import struct
import sys

# The box's twelve triangles over its eight corners (corner i takes the
# high x for bit 0, y for bit 1, z for bit 2), each wound outward.
FACES = [0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
         2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5]
RANGE_1_5 = 1
RANGE_5 = 2
GENERATE = 4
INSTANCES = 8
CREATE, MOVE, DESTROY, COMMIT = range(4)
IDENTITY = (0.0, 0.0, 0.0, 1.0)


def box(low, high):
    corners = [tuple(high[k] if i >> k & 1 else low[k] for k in range(3)) for i in range(8)]
    return corners, list(FACES)


def seed(mode, vertices, indices, points, place=((0, 0, 0), (0, 0, 0), 0, 0), capacity=0,
         calls=()):
    out = bytes([mode, (len(vertices) - 1) % 64, len(indices) // 3])
    out += b"".join(struct.pack("<3f", *v) for v in vertices)
    low, high, spacing, height = place
    out += bytes(indices) + struct.pack("<8f", *low, *high, spacing, height)
    if mode & INSTANCES:
        out += bytes([capacity, len(calls)])
        for kind, which, position, orientation, scale in calls:
            out += bytes([kind, which]) + struct.pack("<8f", *position, *orientation, scale)
    return out + b"".join(struct.pack("<3f", *p) for p in points)


def call(kind, which=0, position=(0.0, 0.0, 0.0), orientation=IDENTITY, scale=1.0):
    return (kind, which, position, orientation, scale)


def seeds():
    room, room_faces = box((-4.0, 0.0, -4.0), (4.0, 3.0, 4.0))
    wall, wall_faces = box((-0.2, 0.0, -4.0), (0.2, 3.0, 2.0))
    walled = room + wall
    walled_faces = room_faces + [i + 8 for i in wall_faces]
    probes = [(x, 1.5, z) for x in (-3.0, -1.0, 1.0, 3.0) for z in (-3.0, 0.0, 3.0)]
    far, far_faces = [], []
    for t in range(8):
        x = 3.0e38 if t < 4 else -3.0e38
        far += [(x, float(t), 0.0), (x, t + 1.0, 0.0), (x, float(t), 1.0)]
        far_faces += [3 * t, 3 * t + 1, 3 * t + 2]
    # The generation box from (-4, -1, -4), 9 m on a side, 1 m apart,
    # probes 1.1 m up.
    place = ((-4.0, -1.0, -4.0), (5.0, 8.0, 5.0), 1.0, 1.1)
    wide = ((-1.0e30, -1.0, -1.0e30), (1.0e30, 8.0, 1.0e30), 1.0, 1.1)
    tall = ((-4.0, -1.0, -4.0), (4.0, 3.0e38, 4.0), 2.0, 1.1)
    column = ((3.0e38, -1.0, 0.0), (3.0e38, 9.0, 0.5), 1.0, 1.1)
    # Two instances (a third finds the scene full, one of mesh 1 is
    # refused), moved far and turned, one with no orientation refused;
    # one destroyed twice and moved after, ids 0 and past the capacity
    # refused, and a small one made in its place.
    turned = (0.0, 0.38268343, 0.0, 0.92387953)
    calls = [
        call(CREATE), call(CREATE, position=(20.0, 0.0, 0.0), orientation=turned, scale=2.0),
        call(CREATE), call(CREATE, which=1), call(COMMIT),
        call(MOVE, 0, position=(1.0e30, 0.0, 0.0), scale=1000.0),
        call(MOVE, 1, orientation=(0.0, 0.0, 0.0, 0.0)), call(COMMIT),
        call(DESTROY, 1), call(DESTROY, 1), call(MOVE, 1), call(DESTROY, 200), call(DESTROY, 201),
        call(CREATE, position=(0.0, 1.0, 0.0), scale=0.001), call(COMMIT),
    ]
    across = [(-30.0, 1.5, 0.1), (30.0, 1.5, 0.1), (20.0, 10.0, 0.0), (20.0, -10.0, 0.5)]
    # A cube 5e37 from the origin on each axis, its diagonal turned onto
    # x: at scale 1 its corner reaches 8.66e37, where the four of it the
    # padding sums pass FLT_MAX and the instance is refused; at 0.9,
    # 7.79e37, the sum fits and it is taken.
    huge, huge_faces = box((-5.0e37, -5.0e37, -5.0e37), (5.0e37, 5.0e37, 5.0e37))
    diagonal = (0.0, 0.32505758, -0.32505758, 0.88807383)
    huge_calls = [
        call(CREATE, orientation=diagonal), call(CREATE, orientation=diagonal, scale=0.9),
        call(COMMIT), call(MOVE, 0, orientation=diagonal), call(COMMIT),
    ]
    return [
        seed(RANGE_5, room, room_faces, probes),
        seed(RANGE_5, walled, walled_faces, probes),
        seed(RANGE_5 | GENERATE, walled, walled_faces, [], place),
        seed(RANGE_1_5, walled, walled_faces, probes),
        seed(RANGE_5, far, far_faces, [(0.0, 1.0, 0.0), (1.0, 1.0, 1.0)]),
        seed(RANGE_5 | GENERATE, walled, walled_faces, [], wide),
        seed(RANGE_5 | GENERATE, walled, walled_faces, [], tall),
        seed(RANGE_5 | GENERATE, far, far_faces, [], column),
        seed(RANGE_5 | INSTANCES, walled, walled_faces, across, capacity=2, calls=calls),
        seed(RANGE_5 | INSTANCES, huge, huge_faces, [(0.0, 0.0, -1.0e38), (0.0, 0.0, 1.0e38)],
             capacity=2, calls=huge_calls),
    ]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: fuzz_scene_seeds.py <directory>")
    os.makedirs(sys.argv[1], exist_ok=True)
    for data in seeds():
        name = "seed-" + hashlib.sha1(data).hexdigest()[:16]
        with open(os.path.join(sys.argv[1], name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
