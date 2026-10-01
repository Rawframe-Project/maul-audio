// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on CoreAudio. Each stream has an AUHAL output unit on its
// device, taking 32-bit float interleaved frames at the stream's rate on
// its input scope and converting them to the device's format. The
// unit's render callback, on the HAL's IO thread, moves each buffer
// through the stream's fixed-period adapter. Channels go to the
// device's channels in order.

#include "coreaudio_stream.h"

#include "context.h"
#include "coreaudio_core.h"
#include "period.h"
#include "thread.h"

#include <string.h>

// The most frames the unit may ask for in one render.
#define MAX_SLICE_FRAMES 4096u

static maudCoreAudioStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudCoreAudio* coreaudio = context->native;
    return &coreaudio->streams[slot - context->streams.slots];
}

// Fills the unit's buffer on the IO thread: the stream's frames while it
// runs, silence otherwise.
static OSStatus Render(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                       UInt32 bus, UInt32 frames, AudioBufferList* data)
{
    (void)time;
    (void)bus;
    maudStreamCore* core = ((maudCoreAudioStream*)user)->core;
    float* out = data->mBuffers[0].mData;
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
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return noErr;
}

// The HAL object of the device whose UID is the stream's device key, or
// kAudioObjectUnknown.
static AudioObjectID ObjectOf(maudContext* context, const maudStreamCore* core)
{
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.current);
    if (device == nullptr)
    {
        return kAudioObjectUnknown;
    }
    CFStringRef uid =
        CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)device->key.bytes,
                                (CFIndex)device->key.length, kCFStringEncodingUTF8, false);
    if (uid == nullptr)
    {
        return kAudioObjectUnknown;
    }
    AudioObjectPropertyAddress address = maudCoreAudioAddress(
        kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal);
    AudioObjectID object = kAudioObjectUnknown;
    UInt32 size = sizeof(object);
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(uid),
                                                 (const void*)&uid, &size, &object);
    CFRelease(uid);
    return status == noErr ? object : kAudioObjectUnknown;
}

// Asks the device for an IO buffer of the stream's period, within the
// device's range; the HAL keeps one size per process and device.
static void AskBufferFrames(AudioObjectID object, uint32_t period)
{
    AudioObjectPropertyAddress address = maudCoreAudioAddress(
        kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal);
    AudioValueRange range = {0};
    UInt32 size = sizeof(range);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &range) != noErr)
    {
        return;
    }
    UInt32 frames = period;
    frames = frames < (UInt32)range.mMinimum ? (UInt32)range.mMinimum : frames;
    frames = frames > (UInt32)range.mMaximum ? (UInt32)range.mMaximum : frames;
    address.mSelector = kAudioDevicePropertyBufferFrameSize;
    OSStatus status =
        AudioObjectSetPropertyData(object, &address, 0, nullptr, sizeof(frames), &frames);
    (void)status;
}

// Sets the unit's device, client format, callback and slice size.
static bool Configure(maudCoreAudioStream* entry, AudioObjectID object)
{
    const maudStreamCore* core = entry->core;
    UInt32 channels = core->period.channelCount;
    AudioStreamBasicDescription format = {
        .mSampleRate = core->format.sampleRate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = channels * (UInt32)sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = channels * (UInt32)sizeof(float),
        .mChannelsPerFrame = channels,
        .mBitsPerChannel = 32,
    };
    AURenderCallbackStruct callback = {.inputProc = Render, .inputProcRefCon = entry};
    UInt32 slice = MAX_SLICE_FRAMES;
    AudioUnit unit = entry->unit;
    return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &object, sizeof(object)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                &format, sizeof(format)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                0, &callback, sizeof(callback)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
}

static void Disconnect(maudCoreAudioStream* entry)
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
    maudStreamCore* core = entry->core;
    *entry = (maudCoreAudioStream){.core = core};
}

// Makes and initializes the stream's unit on its device.
static maudResult Connect(maudContext* context, maudCoreAudioStream* entry)
{
    AudioObjectID object = ObjectOf(context, entry->core);
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (object == kAudioObjectUnknown || component == nullptr ||
        AudioComponentInstanceNew(component, &entry->unit) != noErr)
    {
        entry->unit = nullptr;
        return maud_errorPlatform;
    }
    if (!Configure(entry, object))
    {
        return maud_errorPlatform;
    }
    AskBufferFrames(object, entry->core->format.periodFrames);
    return AudioUnitInitialize(entry->unit) == noErr ? maud_success : maud_errorPlatform;
}

// Connects the stream's unit if it has a device; a stream without one
// waits.
static maudResult Open(maudContext* context, maudStreamSlot* slot)
{
    maudCoreAudioStream* entry = EntryOf(context, slot);
    *entry = (maudCoreAudioStream){.core = &slot->core};
    if (slot->core.binding.current.index1 == 0)
    {
        return maud_success;
    }
    maudResult result = Connect(context, entry);
    if (result != maud_success)
    {
        Disconnect(entry);
    }
    return result;
}

static void Start(maudCoreAudioStream* entry)
{
    if (entry->unit != nullptr && !entry->playing)
    {
        entry->playing = AudioOutputUnitStart(entry->unit) == noErr;
    }
}

// Stops the unit; when AudioOutputUnitStop returns, the IO thread has
// left the render callback.
static void Stop(maudCoreAudioStream* entry)
{
    if (entry->playing)
    {
        OSStatus status = AudioOutputUnitStop(entry->unit);
        (void)status;
        entry->playing = false;
    }
}

static bool Running(const maudStreamSlot* slot)
{
    return atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning;
}

maudResult maudCoreAudioAttachStream(maudContext* context, maudStreamSlot* slot)
{
    return Open(context, slot);
}

void maudCoreAudioDetachStream(maudContext* context, maudStreamSlot* slot)
{
    Disconnect(EntryOf(context, slot));
}

void maudCoreAudioRetargetStream(maudContext* context, maudStreamSlot* slot)
{
    maudCoreAudioDetachStream(context, slot);
    // A failure leaves the stream without a unit; the drain tries again
    // while it runs.
    if (Open(context, slot) == maud_success && Running(slot))
    {
        Start(EntryOf(context, slot));
    }
}

void maudCoreAudioSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudCoreAudioStream* entry = EntryOf(context, slot);
    if (!active)
    {
        Stop(entry);
        return;
    }
    if (entry->unit == nullptr)
    {
        maudResult result = Open(context, slot);
        (void)result;
    }
    Start(entry);
}

void maudCoreAudioResumeStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        if (slot->live && Running(slot) && slot->core.binding.current.index1 != 0 &&
            EntryOf(context, slot)->unit == nullptr)
        {
            maudCoreAudioRetargetStream(context, slot);
        }
    }
}
