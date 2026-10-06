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
// new response swaps in without a jump; misuse is refused.

#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

enum
{
    FRAMES = 28800
};

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

int main(void)
{
    TestFloor();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
