// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Golden renders through the offline backend (requirements, section 5):
// a noise source sweeping a helix about the listener through the
// binaural effect; a third-order bed of a noise source turned a full
// turn and decoded for both ears; a source walking behind a wall, the
// spatializer's direct result applied each block; a burst into the
// reverb, its tail decoded for both ears. Each is rendered twice and
// must be bit-identical, then compared with its 16-bit WAV reference in
// data/golden: per 10 ms block and channel the error's RMS is at most
// the reference block's RMS times 10^(-50/20), or 3e-5 when that is
// less. MAUD_WRITE_GOLDEN set rewrites the references.

// fopen and getenv read and rewrite the references; the C runtime's
// warning that they are unsafe is about the Annex K alternatives, which
// the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "test_harness.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/binaural.h"
#include "maul-audio/direct.h"
#include "maul-audio/reverb.h"
#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/stream.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE        48000u
#define BLOCK       480u
#define MAX_SECONDS 1u
#define MAX_FRAMES  (RATE * MAX_SECONDS)
#define PI          3.14159265358979f
// The tolerance: 50 dB under the reference block, 3e-5 at the least.
#define RELATIVE 0.0031622777
#define FLOOR    3e-5

static unsigned char s_hrtfBytes[900000];
static maudHrtf* s_hrtf;

// White noise from a fixed generator, within +-0.25.
typedef struct Noise
{
    uint32_t state;
} Noise;

static float NextNoise(Noise* n)
{
    n->state = n->state * 1664525u + 1013904223u;
    return ((float)(n->state >> 8) / 16777216.0f - 0.5f) * 0.5f;
}

// A render: its state, and what it does to make each block, given the
// block's index; out holds BLOCK frames for each ear.
typedef struct Render Render;
typedef void Make(Render* r, uint32_t block, float* left, float* right);

struct Render
{
    Make* make;
    uint32_t blocks;
    Noise noise;
    float mono[BLOCK];
    float bed[16][BLOCK];
    maudBinaural* binaural;
    maudBinauralDecoder* decoder;
    maudDirectEffect* direct;
    maudReverb* reverb;
    maudAcousticScene* scene;
    maudSpatializer* spatializer;
    maudSourceId source;
    maudDirectParams params;
    maudBinauralParams pan;
};

static void Mono(Render* r)
{
    for (uint32_t i = 0; i < BLOCK; ++i)
    {
        r->mono[i] = NextNoise(&r->noise);
    }
}

static void ClearBed(Render* r)
{
    memset(r->bed, 0, sizeof(r->bed));
}

static maudBinaural* Binaural(void)
{
    maudBinauralDef def = maudDefaultBinauralDef();
    def.hrtf = s_hrtf;
    def.maxFrames = BLOCK;
    maudBinaural* effect = nullptr;
    CHECK(maudCreateBinaural(&def, &effect) == maud_success, "a binaural effect");
    return effect;
}

static maudBinauralDecoder* Decoder(uint32_t order)
{
    maudBinauralDecoderDef def = maudDefaultBinauralDecoderDef();
    def.hrtf = s_hrtf;
    def.order = order;
    def.maxFrames = BLOCK;
    maudBinauralDecoder* decoder = nullptr;
    CHECK(maudCreateBinauralDecoder(&def, &decoder) == maud_success, "a decoder");
    return decoder;
}

// The sweep: one turn about the listener in the second, rising from 45
// degrees below to 45 above, 2 m away.
static void MakeSweep(Render* r, uint32_t block, float* left, float* right)
{
    Mono(r);
    float t = ((float)block + 1.0f) / (float)r->blocks;
    float azimuth = 2.0f * PI * t;
    float elevation = (t - 0.5f) * 0.5f * PI;
    maudBinauralParams params = {{2.0f * sinf(azimuth) * cosf(elevation), 2.0f * sinf(elevation),
                                  -2.0f * cosf(azimuth) * cosf(elevation)},
                                 1.0f};
    float* out[2] = {left, right};
    CHECK(maudProcessBinaural(r->binaural, &params, r->mono, out, BLOCK) == maud_success,
          "the sweep");
}

static maudQuaternion Yaw(float angle)
{
    return (maudQuaternion){0.0f, sinf(0.5f * angle), 0.0f, cosf(0.5f * angle)};
}

// The rotation: a source ahead, left and up encoded at third order, the
// bed turned a full turn about the vertical in the half second.
static void MakeRotation(Render* r, uint32_t block, float* left, float* right)
{
    Mono(r);
    ClearBed(r);
    float* bed[16];
    for (int c = 0; c < 16; ++c)
    {
        bed[c] = r->bed[c];
    }
    const maudPanSource source = {{-0.6f, 0.3f, -0.74f}, 1.0f};
    CHECK(maudEncodeAmbisonic(3, &source, &source, r->mono, bed, BLOCK) == maud_success, "encoded");
    maudQuaternion from = Yaw(2.0f * PI * (float)block / (float)r->blocks);
    maudQuaternion to = Yaw(2.0f * PI * ((float)block + 1.0f) / (float)r->blocks);
    CHECK(maudRotateAmbisonic(3, &from, &to, bed, BLOCK) == maud_success, "turned");
    float* out[2] = {left, right};
    CHECK(maudDecodeBinaural(r->decoder, (const float* const*)bed, out, BLOCK) == maud_success,
          "decoded");
}

// The walk: the listener at the origin facing -z; a wall 4 m wide and
// 3 m tall from x = 0 to 4 at z = -2; the source walks from x = -3 to 3
// along z = -4, from sight to behind the wall. Each block steps the
// spatializer first, outside the stream's callback.
static void StepWalk(Render* r, uint32_t block)
{
    float x = -3.0f + 6.0f * ((float)block + 1.0f) / (float)r->blocks;
    const maudPose source = {{x, 0.0f, -4.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    const maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSetSourcePose(r->spatializer, r->source, &source) == maud_success &&
              maudSimulateDirect(r->spatializer, &listener) == maud_success,
          "a step");
    (void)maudLatchResults(r->spatializer);
    maudDirectResult result;
    CHECK(maudGetDirectResult(r->spatializer, r->source, &result) == maud_success, "its result");
    r->params.distance = result.distance;
    r->params.occlusion = result.occlusion;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        r->params.transmission[b] = result.transmission[b];
        r->params.directivity[b] = result.directivity[b];
    }
    r->pan =
        (maudBinauralParams){{result.direction.x, result.direction.y, result.direction.z}, 1.0f};
}

static void MakeWalk(Render* r, uint32_t block, float* left, float* right)
{
    (void)block;
    Mono(r);
    CHECK(maudProcessDirect(r->direct, &r->params, r->mono, r->mono, BLOCK) == maud_success,
          "the direct effect");
    float* out[2] = {left, right};
    CHECK(maudProcessBinaural(r->binaural, &r->pan, r->mono, out, BLOCK) == maud_success, "panned");
}

// The tail: 10 ms of noise 24 dB up into the reverb, then silence, the
// first-order bed decoded for both ears.
static void MakeTail(Render* r, uint32_t block, float* left, float* right)
{
    Mono(r);
    for (uint32_t i = 0; i < BLOCK; ++i)
    {
        r->mono[i] = block == 0 ? 16.0f * r->mono[i] : 0.0f;
    }
    ClearBed(r);
    float* bed[4] = {r->bed[0], r->bed[1], r->bed[2], r->bed[3]};
    const maudReverbParams params = {
        {1.2f, 0.9f, 0.5f}, {0.0f, 0.0f, 0.0f}, 0.02f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    CHECK(maudProcessReverb(r->reverb, &params, r->mono, bed, BLOCK) == maud_success, "reverb");
    float* out[2] = {left, right};
    CHECK(maudDecodeBinaural(r->decoder, (const float* const*)bed, out, BLOCK) == maud_success,
          "decoded");
}

static const maudVector3 s_wallVertices[8] = {
    {0.0f, -1.5f, -2.05f}, {4.0f, -1.5f, -2.05f}, {0.0f, 1.5f, -2.05f}, {4.0f, 1.5f, -2.05f},
    {0.0f, -1.5f, -1.95f}, {4.0f, -1.5f, -1.95f}, {0.0f, 1.5f, -1.95f}, {4.0f, 1.5f, -1.95f}};
static const uint32_t s_boxFaces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                        2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
static const uint32_t s_wallMaterials[12] = {0};

static void OpenWalk(Render* r)
{
    const maudMesh wall = {s_wallVertices, 8, s_boxFaces, s_wallMaterials, 12};
    maudAcousticSceneDef sceneDef = maudDefaultAcousticSceneDef();
    sceneDef.meshes = &wall;
    sceneDef.meshCount = 1;
    CHECK(maudCreateAcousticScene(&sceneDef, &r->scene) == maud_success, "the wall");
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.sourceCapacity = 1;
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = r->scene;
    CHECK(maudCreateSpatializer(&def, &r->spatializer) == maud_success, "a spatializer");
    const maudAcousticMaterial material = {{0.1f, 0.2f, 0.3f}, 0.1f, {0.3f, 0.1f, 0.03f}};
    CHECK(maudSetMaterials(r->spatializer, &material, 1) == maud_success, "its material");
    maudSourceDef sourceDef = maudDefaultSourceDef();
    sourceDef.occlusion = maud_occlusionVolumetric;
    sourceDef.occlusionRadius = 0.5f;
    sourceDef.occlusionSamples = 64;
    sourceDef.transmission = true;
    CHECK(maudCreateSource(r->spatializer, &sourceDef, &r->source) == maud_success, "a source");
    maudDirectEffectDef directDef = maudDefaultDirectEffectDef();
    CHECK(maudCreateDirectEffect(&directDef, &r->direct) == maud_success, "a direct effect");
    r->params = maudDefaultDirectParams();
    r->binaural = Binaural();
}

typedef struct Golden
{
    const char* name;
    Make* make;
    uint32_t blocks;
} Golden;

static const Golden s_goldens[4] = {{"hrtf-sweep", MakeSweep, 100},
                                    {"ambisonic-rotation", MakeRotation, 50},
                                    {"occlusion-walk", MakeWalk, 100},
                                    {"reverb-tail", MakeTail, 100}};

static void Open(Render* r, const Golden* g)
{
    // Field by field: a compound literal of a Render would put a second
    // one on the stack, past a WebAssembly debug build's 64 KB.
    memset(r, 0, sizeof(*r));
    r->make = g->make;
    r->blocks = g->blocks;
    r->noise = (Noise){12345u};
    if (g->make == MakeSweep)
    {
        r->binaural = Binaural();
    }
    else if (g->make == MakeRotation)
    {
        r->decoder = Decoder(3);
    }
    else if (g->make == MakeWalk)
    {
        OpenWalk(r);
    }
    else
    {
        maudReverbDef def = maudDefaultReverbDef();
        def.maxDelay = 0.05f;
        CHECK(maudCreateReverb(&def, &r->reverb) == maud_success, "a reverb");
        r->decoder = Decoder(1);
    }
}

static void Close(Render* r)
{
    maudDestroyBinaural(r->binaural);
    maudDestroyBinauralDecoder(r->decoder);
    maudDestroyDirectEffect(r->direct);
    maudDestroyReverb(r->reverb);
    maudDestroySpatializer(r->spatializer);
    maudDestroyAcousticScene(r->scene);
}

// The stream's callback: the render's next block into the stream's
// interleaved stereo.
typedef struct Pull
{
    Render* render;
    uint32_t block;
} Pull;

static void Callback(const maudStreamBlock* block, void* user)
{
    Pull* pull = user;
    float left[BLOCK];
    float right[BLOCK];
    pull->render->make(pull->render, pull->block, left, right);
    for (uint32_t i = 0; i < block->frameCount && i < BLOCK; ++i)
    {
        block->output[2 * i] = left[i];
        block->output[2 * i + 1] = right[i];
    }
    pull->block += 1;
}

// Renders a golden through an offline context's stream into out
// (interleaved stereo).
static void RenderGolden(const Golden* g, float* out)
{
    maudContextDef contextDef = maudDefaultContextDef();
    contextDef.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&contextDef, &context) == maud_success, "an offline context");
    Render render;
    Open(&render, g);
    Pull pull = {&render, 0};
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.periodFrames = BLOCK;
    def.callback = Callback;
    def.user = &pull;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStartStream(context, stream) == maud_success,
          "a stream");
    for (uint32_t b = 0; b < g->blocks; ++b)
    {
        if (g->make == MakeWalk)
        {
            StepWalk(&render, b);
        }
        CHECK(maudRenderStream(context, stream, out + 2 * (size_t)b * BLOCK, BLOCK) == maud_success,
              "a block rendered");
    }
    CHECK(maudDestroyContext(context) == maud_success, "the context");
    Close(&render);
}

// 16-bit stereo WAV at 48 kHz: the 44-byte header and the samples.
static void Put32(unsigned char* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
    {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

// Four letters of a chunk's name, without their terminator.
static void Tag(unsigned char* p, const char* tag)
{
    for (int i = 0; i < 4; ++i)
    {
        p[i] = (unsigned char)tag[i];
    }
}

static void Header(unsigned char* h, uint32_t frames)
{
    uint32_t bytes = frames * 4u;
    Tag(h, "RIFF");
    Put32(h + 4, 36u + bytes);
    Tag(h + 8, "WAVE");
    Tag(h + 12, "fmt ");
    Put32(h + 16, 16u);
    Put32(h + 20, 1u | (2u << 16));
    Put32(h + 24, RATE);
    Put32(h + 28, RATE * 4u);
    Put32(h + 32, 4u | (16u << 16));
    Tag(h + 36, "data");
    Put32(h + 40, bytes);
}

static int16_t Quantize(float v)
{
    float s = roundf(v * 32768.0f);
    return (int16_t)(s > 32767.0f ? 32767.0f : s < -32768.0f ? -32768.0f : s);
}

static char* PathOf(const Golden* g)
{
    static char path[512];
    snprintf(path, sizeof(path), "%s/golden/%s.wav", MAUD_DATA_DIR, g->name);
    return path;
}

static void Write(const Golden* g, const float* samples, uint32_t frames)
{
    FILE* file = fopen(PathOf(g), "wb");
    CHECK(file != nullptr, "a reference written");
    if (file == nullptr)
    {
        return;
    }
    unsigned char h[44];
    Header(h, frames);
    fwrite(h, 1, sizeof(h), file);
    for (uint32_t i = 0; i < 2 * frames; ++i)
    {
        int16_t s = Quantize(samples[i]);
        unsigned char b[2] = {(unsigned char)((uint16_t)s & 0xFFu),
                              (unsigned char)((uint16_t)s >> 8)};
        fwrite(b, 1, 2, file);
    }
    CHECK(fclose(file) == 0, "and closed");
}

// Reads a reference of frames frames into reference; false when it is
// missing or not this format and length.
static bool Read(const Golden* g, float* reference, uint32_t frames)
{
    FILE* file = fopen(PathOf(g), "rb");
    if (file == nullptr)
    {
        return false;
    }
    unsigned char want[44];
    unsigned char h[44];
    Header(want, frames);
    bool ok = fread(h, 1, sizeof(h), file) == sizeof(h) && memcmp(h, want, sizeof(h)) == 0;
    for (uint32_t i = 0; ok && i < 2 * frames; ++i)
    {
        unsigned char b[2];
        ok = fread(b, 1, 2, file) == 2;
        reference[i] = (float)(int16_t)(uint16_t)(b[0] | (b[1] << 8)) / 32768.0f;
    }
    unsigned char extra;
    ok = ok && fread(&extra, 1, 1, file) == 0;
    fclose(file);
    return ok;
}

// The worst block's error against its tolerance, as a ratio (at most 1
// passes), and the render's peak.
static double Compare(const float* render, const float* reference, uint32_t frames, float* peak)
{
    double worst = 0.0;
    *peak = 0.0f;
    for (uint32_t start = 0; start < frames; start += BLOCK)
    {
        for (uint32_t c = 0; c < 2; ++c)
        {
            double error = 0.0;
            double level = 0.0;
            for (uint32_t i = start; i < start + BLOCK; ++i)
            {
                double d = (double)render[2 * i + c] - (double)reference[2 * i + c];
                error += d * d;
                level += (double)reference[2 * i + c] * (double)reference[2 * i + c];
                *peak = fmaxf(*peak, fabsf(render[2 * i + c]));
            }
            double rms = sqrt(error / BLOCK);
            double allowed = fmax(RELATIVE * sqrt(level / BLOCK), FLOOR);
            worst = fmax(worst, rms / allowed);
        }
    }
    return worst;
}

static float s_first[2 * MAX_FRAMES];
static float s_second[2 * MAX_FRAMES];
static float s_reference[2 * MAX_FRAMES];

static void TestGolden(const Golden* g)
{
    uint32_t frames = g->blocks * BLOCK;
    RenderGolden(g, s_first);
    RenderGolden(g, s_second);
    CHECK(memcmp(s_first, s_second, 2 * (size_t)frames * sizeof(float)) == 0,
          "rendered twice, the same bits");
    if (getenv("MAUD_WRITE_GOLDEN") != nullptr)
    {
        Write(g, s_first, frames);
    }
    bool found = Read(g, s_reference, frames);
    CHECK(found, "the reference");
    if (!found)
    {
        return;
    }
    float peak = 0.0f;
    double worst = Compare(s_first, s_reference, frames, &peak);
    printf("%-19s %.2f s, peak %.3f, worst block at %.4f of its tolerance\n", g->name,
           (double)frames / RATE, (double)peak, worst);
    CHECK(peak < 1.0f, "within full scale");
    CHECK(worst <= 1.0, "within the tolerance of the reference");
}

int main(void)
{
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    CHECK(file != nullptr, "the shipped set");
    if (file == nullptr)
    {
        return 1;
    }
    size_t count = fread(s_hrtfBytes, 1, sizeof(s_hrtfBytes), file);
    fclose(file);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = s_hrtfBytes;
    def.byteCount = count;
    CHECK(maudLoadHrtf(&def, &s_hrtf) == maud_success, "it loads");
    for (int i = 0; i < 4; ++i)
    {
        TestGolden(&s_goldens[i]);
    }
    maudDestroyHrtf(s_hrtf);
    return s_failures == 0 ? 0 : 1;
}
