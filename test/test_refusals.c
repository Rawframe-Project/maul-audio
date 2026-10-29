// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every object the library makes takes its memory from the host's
// allocator. Refusing each of an object's blocks in turn gives
// maud_errorCapacity, never a crash or a half-made object, and holds
// nothing; once every block is given, it is made. The objects: an HRTF
// set, the effects and decoders, the voice processors, a scene, a
// spatializer with reflections and paths whose probe set is baked,
// saved and loaded, and an offline context with an output and a duplex
// stream.

#include "test_harness.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/binaural.h"
#include "maul-audio/context.h"
#include "maul-audio/direct.h"
#include "maul-audio/reverb.h"
#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/speakers.h"
#include "maul-audio/stream.h"
#include "maul-audio/voice.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// fopen reads the shipped set; the C runtime's warning that it is unsafe
// does not apply to a test.
#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif

// An allocator that refuses its call numbered failAt, counting from 1,
// and counts the blocks still held. Blocks are aligned by hand.
typedef struct Refusing
{
    int calls;
    int failAt;
    int live;
} Refusing;

static void* RefusingAlloc(size_t size, size_t alignment, void* context)
{
    Refusing* refusing = context;
    if (++refusing->calls == refusing->failAt)
    {
        return nullptr;
    }
    unsigned char* raw = malloc(size + alignment + sizeof(void*));
    if (raw == nullptr)
    {
        return nullptr;
    }
    uintptr_t start = (uintptr_t)(raw + sizeof(void*));
    unsigned char* block = (unsigned char*)((start + alignment - 1) / alignment * alignment);
    ((void**)block)[-1] = raw;
    refusing->live++;
    return block;
}

static void RefusingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    Refusing* refusing = context;
    refusing->live--;
    free(((void**)memory)[-1]);
}

// Makes an object with allocator into *made, or returns why it could
// not, having released everything it made. Undo destroys the object
// whose address it is given.
typedef maudResult MakeFn(const maudAllocator* allocator, void* made);
typedef void UndoFn(void* made);

// Refuses each block of Make's in turn, then lets it succeed.
static void Sweep(const char* what, MakeFn* make, UndoFn* undo)
{
    Refusing refusing = {0};
    maudAllocator allocator = {RefusingAlloc, RefusingFree, &refusing};
    void* made = nullptr;
    bool done = false;
    int refused = 0;
    bool clean = true;
    for (int failAt = 1; failAt < 512 && !done; ++failAt)
    {
        refusing = (Refusing){.failAt = failAt};
        maudResult result = make(&allocator, &made);
        done = result == maud_success;
        if (done)
        {
            undo(&made);
        }
        else
        {
            refused++;
            if (result != maud_errorCapacity || refusing.live != 0)
            {
                printf("%s: block %d refused gave %s, %d held\n", what, failAt,
                       maudResultName(result), refusing.live);
                clean = false;
            }
        }
    }
    printf("%s: %d blocks refused in turn\n", what, refused);
    CHECK(done && refused > 0, "made once every block is given");
    CHECK(clean, "each refusal a capacity error, nothing held");
    CHECK(refusing.live == 0, "every block back after destruction");
}

static maudHrtf* s_hrtf;
static unsigned char s_hrtfBytes[900000];
static size_t s_hrtfCount;

static maudResult MakeHrtf(const maudAllocator* allocator, void* made)
{
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = s_hrtfBytes;
    def.byteCount = s_hrtfCount;
    def.allocator = *allocator;
    return maudLoadHrtf(&def, made);
}

static void UndoHrtf(void* made)
{
    maudDestroyHrtf(*(maudHrtf**)made);
}

static maudResult MakeBinaural(const maudAllocator* allocator, void* made)
{
    maudBinauralDef def = maudDefaultBinauralDef();
    def.hrtf = s_hrtf;
    def.allocator = *allocator;
    return maudCreateBinaural(&def, made);
}

static void UndoBinaural(void* made)
{
    maudDestroyBinaural(*(maudBinaural**)made);
}

static maudResult MakeBinauralDecoder(const maudAllocator* allocator, void* made)
{
    maudBinauralDecoderDef def = maudDefaultBinauralDecoderDef();
    def.hrtf = s_hrtf;
    def.order = 3;
    def.allocator = *allocator;
    return maudCreateBinauralDecoder(&def, made);
}

static void UndoBinauralDecoder(void* made)
{
    maudDestroyBinauralDecoder(*(maudBinauralDecoder**)made);
}

static maudResult MakeSpeakerDecoder(const maudAllocator* allocator, void* made)
{
    maudSpeakerDecoderDef def = maudDefaultSpeakerDecoderDef();
    def.layout = maud_layout7Point1Point4;
    def.order = 3;
    def.allocator = *allocator;
    return maudCreateSpeakerDecoder(&def, made);
}

static void UndoSpeakerDecoder(void* made)
{
    maudDestroySpeakerDecoder(*(maudSpeakerDecoder**)made);
}

static maudResult MakeSpeakerPanner(const maudAllocator* allocator, void* made)
{
    maudSpeakerPannerDef def = maudDefaultSpeakerPannerDef();
    def.layout = maud_layout7Point1Point4;
    def.allocator = *allocator;
    return maudCreateSpeakerPanner(&def, made);
}

static void UndoSpeakerPanner(void* made)
{
    maudDestroySpeakerPanner(*(maudSpeakerPanner**)made);
}

static maudResult MakeDirectEffect(const maudAllocator* allocator, void* made)
{
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    def.allocator = *allocator;
    return maudCreateDirectEffect(&def, made);
}

static void UndoDirectEffect(void* made)
{
    maudDestroyDirectEffect(*(maudDirectEffect**)made);
}

static maudResult MakeReverb(const maudAllocator* allocator, void* made)
{
    maudReverbDef def = maudDefaultReverbDef();
    def.tail = true;
    def.allocator = *allocator;
    return maudCreateReverb(&def, made);
}

static void UndoReverb(void* made)
{
    maudDestroyReverb(*(maudReverb**)made);
}

static maudResult MakeVoiceDetector(const maudAllocator* allocator, void* made)
{
    maudVoiceDetectorDef def = maudDefaultVoiceDetectorDef();
    def.allocator = *allocator;
    return maudCreateVoiceDetector(&def, made);
}

static void UndoVoiceDetector(void* made)
{
    maudDestroyVoiceDetector(*(maudVoiceDetector**)made);
}

static maudResult MakeGainControl(const maudAllocator* allocator, void* made)
{
    maudGainControlDef def = maudDefaultGainControlDef();
    def.allocator = *allocator;
    return maudCreateGainControl(&def, made);
}

static void UndoGainControl(void* made)
{
    maudDestroyGainControl(*(maudGainControl**)made);
}

static maudResult MakeNoiseSuppressor(const maudAllocator* allocator, void* made)
{
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    def.allocator = *allocator;
    return maudCreateNoiseSuppressor(&def, made);
}

static void UndoNoiseSuppressor(void* made)
{
    maudDestroyNoiseSuppressor(*(maudNoiseSuppressor**)made);
}

static maudResult MakeEchoCanceller(const maudAllocator* allocator, void* made)
{
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    def.allocator = *allocator;
    return maudCreateEchoCanceller(&def, made);
}

static void UndoEchoCanceller(void* made)
{
    maudDestroyEchoCanceller(*(maudEchoCanceller**)made);
}

// A closed 5 by 4 by 3 m office.
static maudResult MakeOffice(const maudAllocator* allocator, maudAcousticScene** sceneOut)
{
    static maudVector3 v[8];
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0};
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? 5.0f : 0.0f, (i & 2) ? 4.0f : 0.0f, (i & 4) ? 3.0f : 0.0f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    def.allocator = *allocator;
    return maudCreateAcousticScene(&def, sceneOut);
}

static maudResult MakeScene(const maudAllocator* allocator, void* made)
{
    return MakeOffice(allocator, made);
}

static void UndoScene(void* made)
{
    maudDestroyAcousticScene(*(maudAcousticScene**)made);
}

// The scene the spatializer's rays are cast against, made with the C
// allocator: only the spatializer's blocks are refused.
static maudAcousticScene* s_office;
static unsigned char s_bake[1 << 20];

// A spatializer with reverb, reflections, paths and two probe sets: one
// baked and saved, the other loaded from those bytes.
static maudResult MakeSpatializer(const maudAllocator* allocator, void* made)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudFindAnyHit;
    def.closestHit = maudFindClosestHit;
    def.rayContext = s_office;
    def.reverbRays = 256;
    def.reflectionOrder = 1;
    def.reflectionDuration = 0.1f;
    def.probeSetCapacity = 2;
    def.maxPaths = 4;
    def.allocator = *allocator;
    maudSpatializer* s = nullptr;
    maudResult result = maudCreateSpatializer(&def, &s);
    if (result != maud_success)
    {
        return result;
    }
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.3f, {0.0f, 0.0f, 0.0f}};
    maudVector3 points[4] = {
        {1.0f, 1.5f, 1.0f}, {2.5f, 1.5f, 1.0f}, {2.5f, 1.5f, 2.0f}, {4.0f, 1.5f, 2.0f}};
    maudProbeSetDef setDef = maudDefaultProbeSetDef();
    setDef.points = points;
    setDef.pointCount = 4;
    setDef.range = 3.0f;
    maudProbeSetId set = {0, 0};
    maudProbeSetId loaded = {0, 0};
    size_t size = 0;
    result = maudSpatializer_SetMaterials(s, &wall, 1);
    result = result == maud_success ? maudCreateProbeSet(s, &setDef, &set) : result;
    result = result == maud_success ? maudProbeSet_Bake(s, set) : result;
    result = result == maud_success ? maudProbeSet_Save(s, set, nullptr, 0, &size) : result;
    result = result == maud_success && size > sizeof(s_bake) ? maud_errorState : result;
    result = result == maud_success ? maudProbeSet_Save(s, set, s_bake, size, &size) : result;
    result = result == maud_success ? maudLoadProbeSet(s, s_bake, size, &loaded) : result;
    if (result != maud_success)
    {
        maudDestroySpatializer(s);
        return result;
    }
    *(maudSpatializer**)made = s;
    return maud_success;
}

static void UndoSpatializer(void* made)
{
    maudDestroySpatializer(*(maudSpatializer**)made);
}

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

// An offline context with an output stream and a duplex one.
static maudResult MakeContext(const maudAllocator* allocator, void* made)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    def.allocator = *allocator;
    maudContext* context = nullptr;
    maudResult result = maudCreateContext(&def, &context);
    if (result != maud_success)
    {
        return result;
    }
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.mode = maud_modePull;
    streamDef.callback = Silence;
    maudStreamId output = {0, 0};
    maudStreamId duplex = {0, 0};
    result = maudCreateStream(context, &streamDef, &output);
    streamDef.direction = maud_directionDuplex;
    result = result == maud_success ? maudCreateStream(context, &streamDef, &duplex) : result;
    if (result != maud_success)
    {
        CHECK(maudDestroyContext(context) == maud_success, "destroyed after a refusal");
        return result;
    }
    *(maudContext**)made = context;
    return maud_success;
}

static void UndoContext(void* made)
{
    CHECK(maudDestroyContext(*(maudContext**)made) == maud_success, "destroyed");
}

static bool ReadShippedSet(void)
{
#ifdef __EMSCRIPTEN__
    printf("the HRTF objects skipped on the web: no file system\n");
    return false;
#else
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    CHECK(file != nullptr, "the shipped set");
    if (file == nullptr)
    {
        return false;
    }
    s_hrtfCount = fread(s_hrtfBytes, 1, sizeof(s_hrtfBytes), file);
    fclose(file);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = s_hrtfBytes;
    def.byteCount = s_hrtfCount;
    CHECK(maudLoadHrtf(&def, &s_hrtf) == maud_success, "the shipped set loads");
    return s_hrtf != nullptr;
#endif
}

int main(void)
{
    if (ReadShippedSet())
    {
        Sweep("hrtf", MakeHrtf, UndoHrtf);
        Sweep("binaural", MakeBinaural, UndoBinaural);
        Sweep("binaural decoder", MakeBinauralDecoder, UndoBinauralDecoder);
        maudDestroyHrtf(s_hrtf);
    }
    Sweep("speaker decoder", MakeSpeakerDecoder, UndoSpeakerDecoder);
    Sweep("speaker panner", MakeSpeakerPanner, UndoSpeakerPanner);
    Sweep("direct effect", MakeDirectEffect, UndoDirectEffect);
    Sweep("reverb", MakeReverb, UndoReverb);
    Sweep("voice detector", MakeVoiceDetector, UndoVoiceDetector);
    Sweep("gain control", MakeGainControl, UndoGainControl);
    Sweep("noise suppressor", MakeNoiseSuppressor, UndoNoiseSuppressor);
    Sweep("echo canceller", MakeEchoCanceller, UndoEchoCanceller);
    Sweep("scene", MakeScene, UndoScene);
    maudAllocator c = {0};
    CHECK(MakeOffice(&c, &s_office) == maud_success, "the office");
    Sweep("spatializer", MakeSpatializer, UndoSpatializer);
    maudDestroyAcousticScene(s_office);
    Sweep("context", MakeContext, UndoContext);
    return s_failures == 0 ? 0 : 1;
}
