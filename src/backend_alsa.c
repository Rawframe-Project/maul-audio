// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ALSA backend's devices. ALSA has no server and no thread: the
// devices are the default PCM and each hardware endpoint the control
// interface lists, found at creation without opening any of them.

#include "alsa_core.h"
#include "alsa_stream.h"
#include "backend.h"
#include "context.h"
#include "device.h"

#include <stdio.h>
#include <string.h>

// alsa-lib's messages go nowhere: the library prints nothing.
static void Quiet(const char* file, int line, const char* function, int error, const char* format,
                  va_list arguments)
{
    (void)file;
    (void)line;
    (void)function;
    (void)error;
    (void)format;
    (void)arguments;
}

snd_local_error_handler_t maudAlsaQuiet(const maudAlsaApi* api)
{
    return api->libErrorSetLocal(Quiet);
}

// The default PCM, for both directions.
static maudResult AddDefaults(maudContext* context)
{
    for (int direction = 0; direction < 2; ++direction)
    {
        maudDeviceSpec spec = {
            .info = {.direction = (maudDirection)direction},
            .name = "Default",
            .nameLength = 7,
            .key = "default",
            .keyLength = 7,
        };
        maudDeviceId device;
        maudResult result = maudAddDevice(context, &spec, &device);
        if (result != maud_success)
        {
            return result;
        }
        maudSetDefaultDevice(context, maud_roleGeneral, device);
        maudSetDefaultDevice(context, maud_roleCommunications, device);
    }
    return maud_success;
}

// Adds the endpoint of one card's PCM device in one direction, if the
// card has it.
static maudResult AddEndpoint(maudContext* context, snd_ctl_t* ctl, const char* cardId,
                              const char* cardName, int device, maudDirection direction)
{
    const maudAlsaApi* api = &((maudAlsa*)context->native)->api;
    alignas(max_align_t) unsigned char infoBytes[MAUD_ALSA_STRUCT_BYTES];
    snd_pcm_info_t* info = (snd_pcm_info_t*)infoBytes;
    memset(infoBytes, 0, sizeof(infoBytes));
    api->pcmInfoSetDevice(info, (unsigned int)device);
    api->pcmInfoSetSubdevice(info, 0);
    api->pcmInfoSetStream(info, direction == maud_directionOutput ? SND_PCM_STREAM_PLAYBACK
                                                                  : SND_PCM_STREAM_CAPTURE);
    if (api->ctlPcmInfo(ctl, info) < 0)
    {
        return maud_success;
    }
    char name[2 * MAUD_ALSA_NAME_BYTES];
    char key[MAUD_ALSA_NAME_BYTES];
    int nameLength = snprintf(name, sizeof(name), "%s, %s", cardName, api->pcmInfoGetName(info));
    int keyLength = snprintf(key, sizeof(key), "hw:CARD=%s,DEV=%d", cardId, device);
    if (keyLength <= 0 || (size_t)keyLength >= sizeof(key))
    {
        return maud_success;
    }
    size_t limit = context->def.limits.deviceTextBytes;
    maudDeviceSpec spec = {
        .info = {.direction = direction},
        .name = name,
        .nameLength = (size_t)nameLength < sizeof(name) ? (size_t)nameLength : sizeof(name) - 1,
        .key = key,
        .keyLength = (size_t)keyLength,
    };
    spec.nameLength = spec.nameLength < limit ? spec.nameLength : limit;
    maudDeviceId id;
    return maudAddDevice(context, &spec, &id);
}

// Adds every PCM endpoint of one card.
static maudResult AddCard(maudContext* context, int card)
{
    const maudAlsaApi* api = &((maudAlsa*)context->native)->api;
    char ctlName[32];
    snprintf(ctlName, sizeof(ctlName), "hw:%d", card);
    snd_ctl_t* ctl = nullptr;
    if (api->ctlOpen(&ctl, ctlName, SND_CTL_NONBLOCK) < 0)
    {
        return maud_success;
    }
    alignas(max_align_t) unsigned char cardBytes[MAUD_ALSA_STRUCT_BYTES];
    snd_ctl_card_info_t* cardInfo = (snd_ctl_card_info_t*)cardBytes;
    memset(cardBytes, 0, sizeof(cardBytes));
    maudResult result = maud_success;
    if (api->ctlCardInfo(ctl, cardInfo) == 0)
    {
        const char* id = api->ctlCardInfoGetId(cardInfo);
        const char* name = api->ctlCardInfoGetName(cardInfo);
        int device = -1;
        while (result == maud_success && api->ctlPcmNextDevice(ctl, &device) == 0 && device >= 0)
        {
            result = AddEndpoint(context, ctl, id, name, device, maud_directionOutput);
            result = result == maud_success
                         ? AddEndpoint(context, ctl, id, name, device, maud_directionInput)
                         : result;
        }
    }
    api->ctlClose(ctl);
    return result;
}

static void Release(maudContext* context, maudAlsa* alsa)
{
    maudUnloadAlsa(&alsa->api);
    maudContextRelease(context, alsa, alsa->bytes, alignof(maudAlsa));
    context->native = nullptr;
}

// Whether the ALSA structures the backend sets aside room for fit.
static bool StructsFit(const maudAlsaApi* api)
{
    return api->ctlCardInfoSizeof() <= MAUD_ALSA_STRUCT_BYTES &&
           api->pcmInfoSizeof() <= MAUD_ALSA_STRUCT_BYTES &&
           api->hwParamsSizeof() <= MAUD_ALSA_STRUCT_BYTES &&
           api->swParamsSizeof() <= MAUD_ALSA_STRUCT_BYTES;
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t streams = context->def.limits.streams;
    size_t bytes = sizeof(maudAlsa) + (size_t)streams * sizeof(maudAlsaStream);
    maudAlsa* alsa = maudContextAllocate(context, bytes, alignof(maudAlsa));
    if (alsa == nullptr)
    {
        return maud_errorCapacity;
    }
    *alsa = (maudAlsa){.context = context, .streams = (maudAlsaStream*)(alsa + 1), .bytes = bytes};
    memset(alsa->streams, 0, (size_t)streams * sizeof(maudAlsaStream));
    context->native = alsa;
    if (!maudLoadAlsa(&alsa->api) || !StructsFit(&alsa->api))
    {
        Release(context, alsa);
        return maud_errorUnsupported;
    }
    snd_local_error_handler_t previous = maudAlsaQuiet(&alsa->api);
    maudResult result = AddDefaults(context);
    int card = -1;
    while (result == maud_success && alsa->api.cardNext(&card) == 0 && card >= 0)
    {
        result = AddCard(context, card);
    }
    alsa->api.libErrorSetLocal(previous);
    if (result != maud_success)
    {
        Release(context, alsa);
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

// ALSA's rates are known only once a PCM is open: a native stream asks
// for the preferred rate here and takes the hardware's when it opens.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    (void)device;
    if (def->mode == maud_modePull)
    {
        return maud_errorUnsupported;
    }
    uint32_t rate = def->ratePolicy == maud_rateNative ? MAUD_ALSA_PREFERRED_RATE : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static const maudBackend s_alsa = {
    .kind = maud_backendAlsa,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = nullptr,
    .openStream = OpenStream,
    .attachStream = maudAlsaAttachStream,
    .detachStream = maudAlsaDetachStream,
    .setStreamActive = maudAlsaSetStreamActive,
    .retargetStream = nullptr,
    .rendersOnCaller = false,
};

const maudBackend* maudGetAlsaBackend(void)
{
    return &s_alsa;
}
