// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Path refinement and diffraction (whitebox): a path through a probe
// off an L corridor's corner lands on the corner's edge with the exact
// length; paths through a door in a wall 0.2 m thick land within 0.1 %
// of the shortest (one point on each face, found by searches) near a
// jamb, within 1 % over the lintel; a vertex whose neighbours see each
// other goes;
// no query leaves a path as it is; the diffraction table gives 0 dB at
// no turn and the half-plane's loss at 30, 90 and 180 degrees within
// 0.1 dB; a path's corners multiply.

#include "path_refine.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>

static maudVector3 s_vertices[32];
static uint32_t s_indices[144];
static uint32_t s_materials[48];
static uint32_t s_boxes;

static void Box(maudVector3 low, maudVector3 high)
{
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    uint32_t base = s_boxes * 8;
    for (uint32_t i = 0; i < 8; ++i)
    {
        s_vertices[base + i] = (maudVector3){(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                                             (i & 4) ? high.z : low.z};
    }
    for (uint32_t k = 0; k < 36; ++k)
    {
        s_indices[s_boxes * 36 + k] = base + faces[k];
    }
    s_boxes += 1;
}

static maudAcousticScene* Scene(void)
{
    maudMesh mesh = {s_vertices, s_boxes * 8, s_indices, s_materials, s_boxes * 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a scene");
    s_boxes = 0;
    return scene;
}

static double Length(maudVector3 a, maudVector3 b)
{
    double x = (double)b.x - (double)a.x;
    double y = (double)b.y - (double)a.y;
    double z = (double)b.z - (double)a.z;
    return sqrt(x * x + y * y + z * z);
}

static double PathLength(const maudVector3* v, uint32_t count)
{
    double total = 0.0;
    for (uint32_t k = 0; k + 1 < count; ++k)
    {
        total += Length(v[k], v[k + 1]);
    }
    return total;
}

static void TestCorner(void)
{
    // The L's inside corner is a block over x < 17, z > 3; its edge runs
    // up through (17, y, 3).
    Box((maudVector3){0.0f, 0.0f, 3.0f}, (maudVector3){17.0f, 3.0f, 20.0f});
    maudAcousticScene* scene = Scene();
    const maudVector3 listener = {18.5f, 1.5f, 12.0f};
    const maudVector3 source = {6.0f, 1.5f, 1.5f};
    maudVector3 v[4] = {listener, {19.5f, 1.5f, 2.5f}, {16.5f, 1.5f, 0.5f}, source};
    uint32_t n = maudRefinePath(maudSceneAnyHit, scene, v, 4);
    const maudVector3 corner = {17.0f, 1.5f, 3.0f};
    double exact = Length(listener, corner) + Length(corner, source);
    printf("corner: %u vertices, %.5f m against %.5f, the vertex %.4f m off the edge\n", n,
           PathLength(v, n), exact, n == 3 ? Length(v[1], corner) : -1.0);
    CHECK(n == 3, "one vertex left");
    CHECK(fabs(PathLength(v, n) / exact - 1.0) < 1e-4, "the exact length");
    CHECK(n == 3 && Length(v[1], corner) < 0.01, "on the corner's edge");
    maudVector3 same[3] = {listener, {19.5f, 1.5f, 2.5f}, source};
    CHECK(maudRefinePath(nullptr, nullptr, same, 3) == 3 && same[1].x == 19.5f,
          "no query, no change");
    maudVector3 clear[3] = {listener, {18.0f, 1.5f, 8.0f}, {18.5f, 1.5f, 4.0f}};
    CHECK(maudRefinePath(maudSceneAnyHit, scene, clear, 3) == 2, "a clear line, no vertex");
    maudDestroyAcousticScene(scene);
}

// The point on the plane at x within the doorway (y0 to y1, z0 to z1)
// that makes a to p to b shortest, by searches of finer grids.
static maudVector3 Through(maudVector3 a, maudVector3 b, float x, float y0, float y1, float z0,
                           float z1)
{
    double best = HUGE_VAL;
    double cy = 0.5 * ((double)y0 + (double)y1);
    double cz = 0.5 * ((double)z0 + (double)z1);
    double span = fmax((double)y1 - (double)y0, (double)z1 - (double)z0);
    for (double step = span / 20.0; step > 1e-5; step /= 4.0)
    {
        double by = cy;
        double bz = cz;
        for (int i = -40; i <= 40; ++i)
        {
            for (int k = -40; k <= 40; ++k)
            {
                double y = fmin(fmax(cy + i * step, (double)y0), (double)y1);
                double z = fmin(fmax(cz + k * step, (double)z0), (double)z1);
                maudVector3 p = {x, (float)y, (float)z};
                double d = Length(a, p) + Length(p, b);
                if (d < best)
                {
                    best = d;
                    by = y;
                    bz = z;
                }
            }
        }
        cy = by;
        cz = bz;
    }
    return (maudVector3){x, (float)cy, (float)cz};
}

// The shortest path through the door's opening in a wall 0.2 m thick:
// a point on each face, each found with the other held, in turn.
static double ThroughThickDoor(maudVector3 a, maudVector3 b)
{
    maudVector3 near = {5.1f, 1.0f, 2.5f};
    maudVector3 far = {4.9f, 1.0f, 2.5f};
    for (int round = 0; round < 30; ++round)
    {
        near = Through(a, far, 5.1f, 0.0f, 2.1f, 2.0f, 3.0f);
        far = Through(near, b, 4.9f, 0.0f, 2.1f, 2.0f, 3.0f);
    }
    return Length(a, near) + Length(near, far) + Length(far, b);
}

// A wall across x = 5, 0.2 m thick, with a door 1 m wide (z 2 to 3)
// and 2.1 m high. Paths crossing near a jamb come within 0.1 % of the
// shortest; ones crossing the lintel at a height, where the shortest
// path bends at both faces, within 1 % (slides alone are 2.6 % long on
// the one tested).
static void TestDoor(void)
{
    Box((maudVector3){4.9f, 0.0f, 0.0f}, (maudVector3){5.1f, 3.0f, 2.0f});
    Box((maudVector3){4.9f, 0.0f, 3.0f}, (maudVector3){5.1f, 3.0f, 6.0f});
    Box((maudVector3){4.9f, 2.1f, 2.0f}, (maudVector3){5.1f, 3.0f, 3.0f});
    maudAcousticScene* scene = Scene();
    const maudVector3 ends[2][2] = {{{8.0f, 2.6f, 5.0f}, {1.0f, 0.4f, 0.5f}},
                                    {{7.0f, 2.8f, 0.5f}, {1.0f, 2.8f, 0.5f}}};
    const double within[2] = {1e-3, 1e-2};
    for (int c = 0; c < 2; ++c)
    {
        maudVector3 v[4] = {ends[c][0], {6.0f, 1.5f, 2.5f}, {4.0f, 1.5f, 2.5f}, ends[c][1]};
        uint32_t n = maudRefinePath(maudSceneAnyHit, scene, v, 4);
        double exact = ThroughThickDoor(ends[c][0], ends[c][1]);
        double got = PathLength(v, n);
        printf("door %d: %u vertices, %.5f m, the shortest %.5f (+%.3f %%)\n", c, n, got, exact,
               100.0 * (got / exact - 1.0));
        CHECK(got >= exact - 1e-4 && got < exact * (1.0 + within[c]), "through the door");
    }
    maudDestroyAcousticScene(scene);
}

static void TestDiffraction(void)
{
    static maudDiffraction d;
    maudSetupDiffraction(&d);
    const float want[3][3] = {
        {-1.6f, -4.9f, -8.4f}, {-4.1f, -11.3f, -16.4f}, {-5.6f, -14.1f, -19.4f}};
    const uint32_t degrees[3] = {30, 90, 180};
    double worst = 0.0;
    for (int a = 0; a < 3; ++a)
    {
        for (int b = 0; b < 3; ++b)
        {
            double db = 20.0 * log10((double)d.gain[b][degrees[a]]);
            worst = fmax(worst, fabs(db - (double)want[a][b]));
        }
    }
    printf("diffraction: worst %.3f dB off\n", worst);
    CHECK(worst < 0.1, "the half-plane's loss");
    CHECK(d.gain[0][0] == 1.0f && d.gain[1][0] == 1.0f && d.gain[2][0] == 1.0f, "0 dB unturned");
    // Two right angles: each corner's loss, multiplied.
    const maudVector3 v[4] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    float gains[3];
    maudPathDiffraction(&d, v, 4, gains);
    CHECK(fabsf(gains[1] - d.gain[1][90] * d.gain[1][90]) < 1e-6f, "corners multiply");
    maudPathDiffraction(&d, v, 2, gains);
    CHECK(gains[0] == 1.0f && gains[2] == 1.0f, "a straight path loses nothing");
}

int main(void)
{
    TestCorner();
    TestDoor();
    TestDiffraction();
    return s_failures == 0 ? 0 : 1;
}
