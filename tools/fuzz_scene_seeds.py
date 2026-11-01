#!/usr/bin/env python3
# Writes fuzz_scene's seeds, built here rather than kept as files: a
# closed room with the host's probes in it, the room with a wall between
# them, at two ranges, the room's probes generated over it, and eight
# small triangles near x = +3e38 and -3e38. The layout is the one
# test/fuzz_scene.c reads: a mode byte (the range in its low two bits,
# generation in its third), the vertex and triangle counts, the vertices
# as floats, a byte per index, six bytes that place a generation's box,
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


def box(low, high):
    corners = [tuple(high[k] if i >> k & 1 else low[k] for k in range(3)) for i in range(8)]
    return corners, list(FACES)


def seed(mode, vertices, indices, points, place=(0, 0, 0, 0, 0, 0)):
    out = bytes([mode, (len(vertices) - 1) % 64, len(indices) // 3])
    out += b"".join(struct.pack("<3f", *v) for v in vertices)
    out += bytes(indices) + bytes(place)
    return out + b"".join(struct.pack("<3f", *p) for p in points)


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
    place = (4, 7, 4, 8, 2, 2)
    return [
        seed(RANGE_5, room, room_faces, probes),
        seed(RANGE_5, walled, walled_faces, probes),
        seed(RANGE_5 | GENERATE, walled, walled_faces, [], place),
        seed(RANGE_1_5, walled, walled_faces, probes),
        seed(RANGE_5, far, far_faces, [(0.0, 1.0, 0.0), (1.0, 1.0, 1.0)]),
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
