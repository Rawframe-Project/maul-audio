// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on iOS. Each stream has a RemoteIO unit, taking or giving
// 32-bit float interleaved frames at the stream's rate, which the unit
// converts to the session's. The unit's callback, on the system's IO
// thread, moves each buffer through the stream's fixed-period adapter.
// The session's category follows the streams that run (ios_session.m).

#include "ios_stream.h"

#include "clock.h"
#include "context.h"
#include "device.h"
#include "ios_core.h"
#include "ios_session.h"
#include "period.h"
#include "thread.h"
#include "voice.h"
#include "xrun.h"

#include <string.h>

// The most frames the unit may ask for in one render.
#define MAX_SLICE_FRAMES 4096u

static maudIosStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudIos* ios = context->native;
    return &ios->streams[slot - context->streams.slots];
}

// Counts skipped cycles: an IO cycle that starts past where the last one
// ended on the unit's sample clock.
static void CheckSampleTime(maudIosStream* entry, const AudioTimeStamp* time, UInt32 frames)
{
    if ((time->mFlags & kAudioTimeStampSampleTimeValid) == 0)
    {
        return;
    }
    if (entry->nextSampleTime >= 0.0 && time->mSampleTime > entry->nextSampleTime + 0.5)
    {
        maudCountXrun(entry->core);
    }
    entry->nextSampleTime = time->mSampleTime + frames;
}

// A buffer's host time, mach_absolute_time's units, in nanoseconds, or
// now when the time stamp has none.
static int64_t HostNanoseconds(const maudIos* ios, const AudioTimeStamp* time)
{
    if ((time->mFlags & kAudioTimeStampHostTimeValid) == 0)
    {
        return maudNowNanoseconds();
    }
    return (int64_t)((double)time->mHostTime * ios->timebaseNumer / ios->timebaseDenom);
}

// Fills the unit's buffer on the IO thread: the stream's frames while it
// runs, silence otherwise.
static OSStatus Render(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                       UInt32 bus, UInt32 frames, AudioBufferList* data)
{
    (void)bus;
    maudIosStream* entry = user;
    maudStreamCore* core = entry->core;
    const maudIos* ios = entry->owner;
    float* out = data->mBuffers[0].mData;
    CheckSampleTime(entry, time, frames);
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (running)
    {
        maudPullPeriod(&core->period, out, frames);
    }
    else
    {
        memset(out, 0, (size_t)frames * core->period.channelCount * sizeof(float));
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    // The buffer reaches the hardware at its host time; the session's
    // latency comes after that.
    int64_t latency = entry->sessionLatency + HostNanoseconds(ios, time) - maudNowNanoseconds();
    maudStampOutputClock(core, latency > 0 ? latency : 0);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return noErr;
}

// Takes an input buffer on the IO thread and pushes it to the stream
// while it runs. The buffer's host time is its first frame's at the
// hardware; the session's input latency came before it.
static OSStatus Capture(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                        UInt32 bus, UInt32 frames, AudioBufferList* unused)
{
    (void)unused;
    maudIosStream* entry = user;
    maudStreamCore* core = entry->core;
    const maudIos* ios = entry->owner;
    AudioBufferList* list = entry->captured;
    UInt32 channels = core->period.channelCount;
    UInt32 frameBytes = channels * (UInt32)sizeof(float);
    if (frames > MAX_SLICE_FRAMES)
    {
        return kAudioUnitErr_TooManyFramesToProcess;
    }
    CheckSampleTime(entry, time, frames);
    float* data = list->mBuffers[0].mData;
    list->mBuffers[0].mDataByteSize = MAX_SLICE_FRAMES * frameBytes;
    memset(data, 0, (size_t)frames * frameBytes);
    OSStatus status = AudioUnitRender(entry->unit, flags, time, bus, frames, list);
    if (status != noErr)
    {
        return status;
    }
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (running)
    {
        maudPushPeriod(&core->period, data, frames);
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    int64_t latency = entry->sessionLatency + maudNowNanoseconds() - HostNanoseconds(ios, time);
    maudStampInputClock(core, latency > 0 ? latency : 0);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return noErr;
}

// The client side's format: 32-bit float interleaved frames at the
// stream's rate.
static AudioStreamBasicDescription FormatOf(const maudStreamCore* core)
{
    UInt32 channels = core->period.channelCount;
    return (AudioStreamBasicDescription){
        .mSampleRate = core->format.sampleRate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = channels * (UInt32)sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = channels * (UInt32)sizeof(float),
        .mChannelsPerFrame = channels,
        .mBitsPerChannel = 32,
    };
}

// Sets the unit's direction, client format, callback and slice size.
static bool Configure(maudIosStream* entry)
{
    const maudStreamCore* core = entry->core;
    AudioStreamBasicDescription format = FormatOf(core);
    UInt32 slice = MAX_SLICE_FRAMES;
    AudioUnit unit = entry->unit;
    if (core->def.direction == maud_directionInput)
    {
        // Input on bus 1 only; the frames come out of its output scope.
        UInt32 on = 1;
        UInt32 off = 0;
        AURenderCallbackStruct callback = {.inputProc = Capture, .inputProcRefCon = entry};
        return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input,
                                    1, &on, sizeof(on)) == noErr &&
               AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output,
                                    0, &off, sizeof(off)) == noErr &&
               AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                    1, &format, sizeof(format)) == noErr &&
               AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback,
                                    kAudioUnitScope_Global, 0, &callback,
                                    sizeof(callback)) == noErr &&
               AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                    kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
    }
    AURenderCallbackStruct callback = {.inputProc = Render, .inputProcRefCon = entry};
    return AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                &format, sizeof(format)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                0, &callback, sizeof(callback)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
}

static void Disconnect(maudContext* context, maudIosStream* entry)
{
    if (entry->unit != nullptr)
    {
        if (entry->playing)
        {
            OSStatus stopped = AudioOutputUnitStop(entry->unit);
            (void)stopped;
        }
        OSStatus uninitialized = AudioUnitUninitialize(entry->unit);
        OSStatus disposed = AudioComponentInstanceDispose(entry->unit);
        (void)uninitialized;
        (void)disposed;
    }
    if (entry->captured != nullptr)
    {
        maudContextRelease(context, entry->captured, entry->capturedBytes,
                           alignof(AudioBufferList));
    }
    maudStreamCore* core = entry->core;
    const maudIos* owner = entry->owner;
    *entry = (maudIosStream){.core = core, .owner = owner};
}

// One buffer of interleaved frames for an input, after the list's
// header.
static bool AllocateCaptured(maudContext* context, maudIosStream* entry)
{
    uint32_t channels = entry->core->period.channelCount;
    size_t data = (size_t)MAX_SLICE_FRAMES * channels * sizeof(float);
    entry->capturedBytes = sizeof(AudioBufferList) + data;
    entry->captured = maudContextAllocate(context, entry->capturedBytes, alignof(AudioBufferList));
    if (entry->captured == nullptr)
    {
        return false;
    }
    entry->captured->mNumberBuffers = 1;
    entry->captured->mBuffers[0] = (AudioBuffer){
        .mNumberChannels = channels,
        .mDataByteSize = (UInt32)data,
        .mData = entry->captured + 1,
    };
    return true;
}

// Points the session's preferred input at the port a pinned input
// stream is on; a stream on the default leaves it. false when the port
// is no longer available.
static bool PreferPort(maudContext* context, const maudStreamCore* core)
{
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.current);
    static const char prefix[] = "port:";
    size_t skip = sizeof(prefix) - 1;
    if (core->def.direction != maud_directionInput || device == nullptr ||
        device->key.length <= skip || memcmp(device->key.bytes, prefix, skip) != 0)
    {
        return true;
    }
    return maudIosSessionPreferInput(device->key.bytes + skip, device->key.length - skip);
}

static maudResult Connect(maudContext* context, maudIosStream* entry)
{
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr || AudioComponentInstanceNew(component, &entry->unit) != noErr)
    {
        entry->unit = nullptr;
        return maud_errorPlatform;
    }
    bool input = entry->core->def.direction == maud_directionInput;
    if (input && !AllocateCaptured(context, entry))
    {
        return maud_errorCapacity;
    }
    // The session takes the stream's direction before its unit
    // initializes.
    bool initialized = Configure(entry) && maudIosUpdateSession(context, true) &&
                       PreferPort(context, entry->core) &&
                       AudioUnitInitialize(entry->unit) == noErr;
    // Back to what runs: an initialized unit outlives the session's
    // deactivation, as it does an interruption's.
    bool settled = maudIosUpdateSession(context, false);
    (void)settled;
    if (!initialized)
    {
        return maud_errorPlatform;
    }
    entry->sessionLatency = maudIosSessionLatency(input);
    // RemoteIO processes nothing; Voice-Processing I/O is the session's
    // voice mode, a later slice.
    if (input)
    {
        maudReportVoice(entry->core, maud_voiceNone);
    }
    return maud_success;
}

maudResult maudIosAttachStream(maudContext* context, maudStreamSlot* slot)
{
    maudIosStream* entry = EntryOf(context, slot);
    *entry = (maudIosStream){.core = &slot->core, .owner = context->native};
    maudResetVoice(&slot->core);
    maudResult result = Connect(context, entry);
    if (result != maud_success)
    {
        Disconnect(context, entry);
    }
    return result;
}

void maudIosDetachStream(maudContext* context, maudStreamSlot* slot)
{
    Disconnect(context, EntryOf(context, slot));
    bool updated = maudIosUpdateSession(context, false);
    (void)updated;
}

// The category follows every stream that has a unit, including one
// being attached (its slot goes live after); activation, the streams
// that play, and the unit being made. A unit exists only between attach
// and detach.
bool maudIosUpdateSession(maudContext* context, bool preparing)
{
    bool outputs = false;
    bool inputs = false;
    bool running = false;
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        const maudIosStream* entry = EntryOf(context, slot);
        if (entry->unit != nullptr)
        {
            bool input = slot->core.def.direction == maud_directionInput;
            inputs = inputs || input;
            outputs = outputs || !input;
            running = running || entry->playing;
        }
    }
    return maudIosSessionUpdate(context->native, outputs, inputs, running || preparing);
}

void maudIosSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudIosStream* entry = EntryOf(context, slot);
    if (entry->unit == nullptr || entry->playing == active)
    {
        return;
    }
    if (!active)
    {
        OSStatus stopped = AudioOutputUnitStop(entry->unit);
        (void)stopped;
        entry->playing = false;
        bool updated = maudIosUpdateSession(context, false);
        (void)updated;
        return;
    }
    // The session takes the stream's direction before the unit starts.
    entry->playing = true;
    entry->nextSampleTime = -1.0;
    entry->playing =
        maudIosUpdateSession(context, false) && AudioOutputUnitStart(entry->unit) == noErr;
}
