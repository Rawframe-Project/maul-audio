// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Geometric reflections through the spatializer: over a lossless,
// fully scattering floor 2 m below, an impulse sent renders W with the
// floor's energy, 1 / (8 pi h^2), within 5 %, and arriving from below
// (Z over W, energy-weighted, -0.8 within 0.05; X and Y nothing); a
// listener rolled upside down hears it from above; the mean arrival is
// on time within 1 ms (the convolution's block taken back); the bed is
// added to; nothing arrives
// before the first path does; before any response the bed is silent; a
// new response swaps in without a jump; misuse is refused. In an office
// (1 s): the reverb at the published level and delay starts where the
// reflections' response ends and has the energy of a response rendered
// on past the join (0.35 to 0.6 s, within 15 %; measured 7 %), and
// without reflections the published level gives the reverb that
// response's energy at 100 ms (within 25 %).

#include "test_harness.h"

#include "maul-audio/reverb.h"
#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

enum
{
    FRAMES = 28800
};

// A wall 2 m to the listener's right (x = 2) facing away from it.
static maudAcousticScene* Wall(void)
{
    static const maudVector3 v[4] = {
        {2, -1000, -1000}, {2, 1000, -1000}, {2, -1000, 1000}, {2, 1000, 1000}};
    static const uint32_t faces[6] = {0, 2, 1, 1, 2, 3};
    static const uint32_t materials[2] = {0, 0};
    maudMesh mesh = {v, 4, faces, materials, 2};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a wall");
    return scene;
}

static maudAcousticScene* Floor(void)
{
    static const maudVector3 v[4] = {
        {-1000, 0, -1000}, {1000, 0, -1000}, {-1000, 0, 1000}, {1000, 0, 1000}};
    static const uint32_t faces[6] = {0, 1, 2, 1, 3, 2};
    static const uint32_t materials[2] = {0, 0};
    maudMesh mesh = {v, 4, faces, materials, 2};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a floor");
    return scene;
}

static maudSpatializer* Create(maudAcousticScene* scene)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.reverbRays = 4096;
    def.reflectionOrder = 1;
    def.reflectionDuration = 0.5f;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        def.airAbsorption[b] = 0.0f;
    }
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial lossless = {{0.0f, 0.0f, 0.0f}, 1.0f, {0, 0, 0}};
    CHECK(maudSetMaterials(s, &lossless, 1) == maud_success, "materials");
    return s;
}

static float s_send[FRAMES];
static float s_bed[4][FRAMES];

// Renders an impulse at frame 0 in blocks of 480 into a bed holding
// base.
static void RenderOver(maudSpatializer* s, maudQuaternion orientation, float base)
{
    memset(s_send, 0, sizeof(s_send));
    for (int c = 0; c < 4; ++c)
    {
        for (int i = 0; i < FRAMES; ++i)
        {
            s_bed[c][i] = base;
        }
    }
    s_send[0] = 1.0f;
    for (int at = 0; at < FRAMES; at += 480)
    {
        float* bed[4] = {s_bed[0] + at, s_bed[1] + at, s_bed[2] + at, s_bed[3] + at};
        CHECK(maudRenderReflections(s, &orientation, s_send + at, bed, 480) == maud_success,
              "render");
    }
}

// Channel c's energy-weighted correlation with W: sum W c / sum W W.
static double Along(int c)
{
    double wc = 0.0;
    double ww = 0.0;
    for (int i = 0; i < FRAMES; ++i)
    {
        wc += (double)s_bed[0][i] * (double)s_bed[c][i];
        ww += (double)s_bed[0][i] * (double)s_bed[0][i];
    }
    return wc / ww;
}

static void Render(maudSpatializer* s, maudQuaternion orientation)
{
    RenderOver(s, orientation, 0.0f);
}

// The floor's energy-weighted mean arrival time, each 10 ms bin's energy
// at its centre: arrivals at cosine c (of the angle from straight down)
// travel 2 h / c and carry c^3 (for h above 1 m).
static double MeanArrival(double h)
{
    double sum = 0.0;
    double weight = 0.0;
    for (int k = 0; k < 100000; ++k)
    {
        double c = (k + 0.5) / 100000.0;
        double t = 2.0 * h / c / 343.0;
        double w = c * c * c;
        if (t < 0.5)
        {
            sum += w * (floor(t / 0.01) + 0.5) * 0.01;
            weight += w;
        }
    }
    return sum / weight;
}

static void TestFloor(void)
{
    maudAcousticScene* scene = Floor();
    maudSpatializer* s = Create(scene);
    const maudQuaternion upright = {0.0f, 0.0f, 0.0f, 1.0f};
    Render(s, upright);
    bool silent = true;
    for (int i = 0; i < FRAMES; ++i)
    {
        silent = silent && s_bed[0][i] == 0.0f;
    }
    CHECK(silent, "nothing before a response");
    maudPose listener = {{0.3f, 2.0f, -0.2f}, upright};
    CHECK(maudSimulateReverb(s, &listener) == maud_success, "an estimate");
    Render(s, upright);
    double energy = 0.0;
    double early = 0.0;
    for (int i = 0; i < FRAMES; ++i)
    {
        double e = (double)s_bed[0][i] * (double)s_bed[0][i];
        energy += e;
        // The first path, 4 m, takes 11.7 ms; bins start at 10 ms and
        // their frames half a bin earlier.
        early += i < 240 ? e : 0.0;
    }
    double want = 1.0 / (8.0 * PI * 4.0);
    printf("floor: W energy %.5f (want %.5f), early %.2e; Y %.3f Z %.3f X %.3f\n", energy, want,
           early, Along(1), Along(2), Along(3));
    CHECK(fabs(energy / want - 1.0) < 0.05, "the floor's energy");
    CHECK(early < 1e-6 * energy, "nothing before the first path");
    CHECK(fabs(Along(2) + 0.8) < 0.05, "from below");
    CHECK(fabs(Along(1)) < 0.05 && fabs(Along(3)) < 0.05, "nothing from the sides");
    // On time: the convolution's block is taken back.
    double mean = 0.0;
    for (int i = 0; i < FRAMES; ++i)
    {
        mean += (double)i / 48000.0 * (double)s_bed[0][i] * (double)s_bed[0][i] / energy;
    }
    printf("mean arrival %.2f ms (want %.2f)\n", 1e3 * mean, 1e3 * MeanArrival(2.0));
    CHECK(fabs(mean - MeanArrival(2.0)) < 0.001, "on time");
    // Added into the bed, not over it.
    RenderOver(s, upright, 1.0f);
    double added = 0.0;
    for (int i = 0; i < FRAMES; ++i)
    {
        added += ((double)s_bed[0][i] - 1.0) * ((double)s_bed[0][i] - 1.0);
    }
    CHECK(fabs(added / energy - 1.0) < 1e-3, "added to the bed");
    // Rolled upside down (half a turn about the forward axis, -z): the
    // floor is above.
    const maudQuaternion rolled = {0.0f, 0.0f, 1.0f, 0.0f};
    Render(s, rolled);
    printf("rolled: Z %.3f\n", Along(2));
    CHECK(fabs(Along(2) - 0.8) < 0.05, "rolled over, from above");
    // A new response from higher up swaps in without a jump.
    listener.position.y = 3.0f;
    CHECK(maudSimulateReverb(s, &listener) == maud_success, "another estimate");
    Render(s, upright);
    bool bounded = true;
    for (int i = 0; i < FRAMES; ++i)
    {
        bounded = bounded && isfinite(s_bed[0][i]) && fabs((double)s_bed[0][i]) < 1.0;
    }
    CHECK(bounded, "a swap stays bounded");
    maudDestroySpatializer(s);
    maudDestroyAcousticScene(scene);
}

// A turn about the vertical: the wall on the right (Y, left, at -0.8)
// is behind once the listener turns a quarter to the left (X, ahead, at
// -0.8); turned the other way it would be ahead.
static void TestTurn(void)
{
    maudAcousticScene* scene = Wall();
    maudSpatializer* s = Create(scene);
    const maudQuaternion upright = {0.0f, 0.0f, 0.0f, 1.0f};
    maudPose listener = {{0.0f, 0.3f, 0.2f}, upright};
    CHECK(maudSimulateReverb(s, &listener) == maud_success, "an estimate");
    Render(s, upright);
    printf("wall: Y %.3f X %.3f\n", Along(1), Along(3));
    CHECK(fabs(Along(1) + 0.8) < 0.05 && fabs(Along(3)) < 0.05, "the wall on the right");
    // A quarter turn to the left about +y: ahead (-z) becomes -x.
    const maudQuaternion left = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
    Render(s, left);
    Render(s, left);
    printf("turned: Y %.3f X %.3f\n", Along(1), Along(3));
    CHECK(fabs(Along(3) + 0.8) < 0.05 && fabs(Along(1)) < 0.05, "turned left, the wall behind");
    maudDestroySpatializer(s);
    maudDestroyAcousticScene(scene);
}

static void TestMisuse(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    CHECK(def.reflectionOrder == 0 && def.reflectionDuration == 1.0f &&
              def.reflectionRate == 48000.0f,
          "the default: none, 1 s at 48 kHz when asked for");
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "no reflections");
    const maudQuaternion upright = {0.0f, 0.0f, 0.0f, 1.0f};
    float* bed[1] = {s_bed[0]};
    CHECK(maudRenderReflections(s, &upright, s_send, bed, 16) == maud_errorState, "none asked for");
    maudDestroySpatializer(s);
    // Short, to fit the web build's 16 MB heap: four responses of nine
    // channels at 1 s would take 14 MB.
    def.reflectionOrder = 2;
    def.reflectionDuration = 0.1f;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "order 2");
    float* nine[9] = {s_bed[0], s_bed[1], s_bed[2], s_bed[3], s_bed[0],
                      s_bed[1], s_bed[2], s_bed[3], nullptr};
    CHECK(maudRenderReflections(s, &upright, s_send, nine, 16) == maud_errorInvalid,
          "a channel missing");
    const maudQuaternion zero = {0.0f, 0.0f, 0.0f, 0.0f};
    nine[8] = s_bed[0];
    CHECK(maudRenderReflections(s, &zero, s_send, nine, 16) == maud_errorInvalid,
          "a zero orientation");
    CHECK(maudRenderReflections(s, &upright, nullptr, nullptr, 0) == maud_success, "no frames");
    CHECK(maudRenderReflections(nullptr, &upright, s_send, nine, 16) == maud_errorInvalid, "NULL");
    maudDestroySpatializer(s);
    maudSpatializer* none = nullptr;
    def.reflectionOrder = 4;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid && none == nullptr, "order 4");
    def.reflectionOrder = 1;
    def.reflectionDuration = 0.01f;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "too short");
    def.reflectionDuration = 1.0f;
    def.reflectionRate = 8000.0f;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "8 kHz");
    def.reflectionRate = 48000.0f;
    def.reverbRays = 0;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "reflections without rays");
}

static maudAcousticScene* Office(void)
{
    static maudVector3 v[8];
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0};
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? 5.0f : 0.0f, (i & 2) ? 3.0f : 0.0f, (i & 4) ? 4.0f : 0.0f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "an office");
    return scene;
}

// A spatializer in the office (reflections of duration seconds, or
// none), estimated once at its centre.
static maudSpatializer* InOffice(maudAcousticScene* scene, float duration, maudReverbResult* result)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.reverbRays = 2048;
    def.reflectionOrder = duration > 0.0f ? 1 : 0;
    def.reflectionDuration = duration > 0.0f ? duration : 1.0f;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        def.airAbsorption[b] = 0.0f;
    }
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.5f, {0, 0, 0}};
    maudPose centre = {{2.3f, 1.4f, 1.9f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSetMaterials(s, &wall, 1) == maud_success &&
              maudSimulateReverb(s, &centre) == maud_success &&
              maudSimulateDirect(s, &centre) == maud_success && maudLatchResults(s) == 1 &&
              maudGetReverbResult(s, result) == maud_success,
          "an estimate");
    return s;
}

static float s_tail[4][FRAMES];

// The reverb's W for an impulse at a result's times, levels and delay.
static void Tail(const maudReverbResult* result)
{
    maudReverbDef def = maudDefaultReverbDef();
    def.maxDelay = 0.5f;
    maudReverb* r = nullptr;
    CHECK(maudCreateReverb(&def, &r) == maud_success, "a reverb");
    maudReverbParams p = {{result->reverbTime[0], result->reverbTime[1], result->reverbTime[2]},
                          {result->level[0], result->level[1], result->level[2]},
                          result->delay,
                          {0.0f, 0.0f, 0.0f},
                          {0.0f, 0.0f, 0.0f}};
    memset(s_send, 0, sizeof(s_send));
    memset(s_tail, 0, sizeof(s_tail));
    s_send[0] = 1.0f;
    for (int at = 0; at < FRAMES; at += 480)
    {
        float* bed[4] = {s_tail[0] + at, s_tail[1] + at, s_tail[2] + at, s_tail[3] + at};
        CHECK(maudProcessReverb(r, &p, s_send + at, bed, 480) == maud_success, "process");
    }
    maudDestroyReverb(r);
}

static double Energy(const float* w, double from, double to)
{
    double e = 0.0;
    for (int i = (int)(from * 48000.0); i < (int)(to * 48000.0); ++i)
    {
        e += (double)w[i] * (double)w[i];
    }
    return e;
}

static void TestHybrid(void)
{
    maudAcousticScene* scene = Office();
    const maudQuaternion upright = {0.0f, 0.0f, 0.0f, 1.0f};
    // The reference: reflections rendered to 0.8 s.
    maudReverbResult reference;
    maudSpatializer* s = InOffice(scene, 0.8f, &reference);
    Render(s, upright);
    maudDestroySpatializer(s);
    static float truth[FRAMES];
    memcpy(truth, s_bed[0], sizeof(truth));
    // Hybrid: reflections to 0.3 s, the reverb after.
    maudReverbResult result;
    s = InOffice(scene, 0.3f, &result);
    printf("hybrid: times %.2f s, levels %.1f / %.1f / %.1f dB, delay %.3f s\n",
           (double)result.reverbTime[1], (double)result.level[0], (double)result.level[1],
           (double)result.level[2], (double)result.delay);
    CHECK(fabs((double)result.delay - 0.277) < 1e-6, "the duration less 23 ms");
    Render(s, upright);
    maudDestroySpatializer(s);
    Tail(&result);
    CHECK(Energy(s_tail[0], 0.0, 0.29) < 1e-6 * Energy(s_tail[0], 0.3, 0.6),
          "the tail waits for the join");
    double tail = Energy(s_tail[0], 0.35, 0.6);
    double room = Energy(truth, 0.35, 0.6);
    printf("tail 0.35 to 0.6 s: %.4g, the reference %.4g\n", tail, room);
    CHECK(fabs(tail / room - 1.0) < 0.15, "the tail at the room's level");
    // Alone, without reflections: the reverb at 100 ms has the
    // reference's energy there.
    maudReverbResult alone;
    s = InOffice(scene, 0.0f, &alone);
    maudDestroySpatializer(s);
    CHECK(alone.delay == 0.0f, "no delay alone");
    Tail(&alone);
    double reverb = Energy(s_tail[0], 0.075, 0.125);
    double response = Energy(truth, 0.075, 0.125);
    printf("alone: the reverb's energy at 100 ms %.3g, the reference's %.3g\n", reverb, response);
    CHECK(fabs(reverb / response - 1.0) < 0.25, "the room's level");
    maudDestroyAcousticScene(scene);
}

int main(void)
{
    TestFloor();
    TestTurn();
    TestHybrid();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
