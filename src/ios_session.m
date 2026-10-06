// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The audio session. The category follows what runs: output only
// Playback, input only Record, both PlayAndRecord with the speaker as
// the default output and Bluetooth allowed. It mixes with others until
// the host asks for focus: lasting or brief focus stops or pauses them,
// brief mixed focus ducks them. With iosSilencedBySwitch, output alone
// is Ambient (or SoloAmbient with focus), which the silent switch
// silences. Manual retain and release; every call from the control
// thread.

#include "ios_session.h"

#import <AVFAudio/AVFAudio.h>

// AVAudioSessionCategoryOptionAllowBluetooth by its value: the iOS 26
// SDK renames it, and either name is deprecated in one SDK or missing in
// another.
#define ALLOW_BLUETOOTH_HFP ((AVAudioSessionCategoryOptions)0x4)

void maudIosSessionFormat(uint32_t* rate, uint32_t* channels)
{
    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        double sampleRate = session.sampleRate;
        NSInteger outputs = session.outputNumberOfChannels;
        *rate = sampleRate >= 8000.0 ? (uint32_t)(sampleRate + 0.5) : 48000u;
        *channels = outputs > 0 ? (uint32_t)outputs : 2u;
    }
}

int64_t maudIosSessionLatency(bool input)
{
    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSTimeInterval latency = input ? session.inputLatency : session.outputLatency;
        return (int64_t)((latency + session.IOBufferDuration) * 1e9);
    }
}

static AVAudioSessionCategory CategoryOf(const maudIos* ios, AVAudioSessionCategoryOptions* options)
{
    const maudIosSession* state = &ios->session;
    bool focused = state->focus == maud_focusLasting || state->focus == maud_focusBrief;
    bool silenced = ios->context->def.iosSilencedBySwitch;
    *options = 0;
    if (!focused)
    {
        *options |= AVAudioSessionCategoryOptionMixWithOthers;
    }
    if (state->focus == maud_focusBriefMixed)
    {
        *options |= AVAudioSessionCategoryOptionDuckOthers;
    }
    if (state->inputs && state->outputs)
    {
        *options |= AVAudioSessionCategoryOptionDefaultToSpeaker | ALLOW_BLUETOOTH_HFP |
                    AVAudioSessionCategoryOptionAllowBluetoothA2DP;
        return AVAudioSessionCategoryPlayAndRecord;
    }
    if (state->inputs)
    {
        *options |= ALLOW_BLUETOOTH_HFP;
        return AVAudioSessionCategoryRecord;
    }
    if (silenced)
    {
        // Ambient always mixes; SoloAmbient never does.
        AVAudioSessionCategory category =
            focused ? AVAudioSessionCategorySoloAmbient : AVAudioSessionCategoryAmbient;
        *options &= ~AVAudioSessionCategoryOptionMixWithOthers;
        return category;
    }
    return AVAudioSessionCategoryPlayback;
}

bool maudIosSessionUpdate(maudIos* ios, bool outputs, bool inputs)
{
    @autoreleasepool
    {
        maudIosSession* state = &ios->session;
        AVAudioSession* session = [AVAudioSession sharedInstance];
        bool wanted = outputs || inputs || state->focus != maud_focusRelease;
        bool changed = !state->configured || state->outputs != outputs || state->inputs != inputs;
        state->outputs = outputs;
        state->inputs = inputs;
        AVAudioSessionCategoryOptions options = 0;
        AVAudioSessionCategory category = CategoryOf(ios, &options);
        if (changed || ![session.category isEqualToString:category] ||
            session.categoryOptions != options)
        {
            if (![session setCategory:category
                                 mode:AVAudioSessionModeDefault
                              options:options
                                error:nil])
            {
                return false;
            }
            state->configured = true;
        }
        if (wanted && !state->active)
        {
            state->active = [session setActive:YES error:nil];
            return state->active;
        }
        if (!wanted && state->active)
        {
            BOOL done = [session setActive:NO
                               withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                     error:nil];
            (void)done;
            state->active = false;
        }
        return true;
    }
}
