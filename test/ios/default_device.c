// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A macOS host tool for the iOS CI cell: makes the device of a UID the
// system's default input and output, which the simulator's microphone
// and speaker follow. Usage: default_device UID. Exits 1 when the
// device is missing or either default cannot be set.

#include <CoreAudio/CoreAudio.h>
#include <stdio.h>

static AudioObjectID DeviceOfUid(const char* uid)
{
    CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, uid, kCFStringEncodingUTF8);
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyTranslateUIDToDevice,
                                          kAudioObjectPropertyScopeGlobal, 0};
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    OSStatus found = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(text),
                                                (const void*)&text, &size, &device);
    CFRelease(text);
    return found == noErr ? device : kAudioObjectUnknown;
}

static bool MakeDefault(AudioObjectPropertySelector selector, AudioObjectID device)
{
    AudioObjectPropertyAddress address = {selector, kAudioObjectPropertyScopeGlobal, 0};
    return AudioObjectSetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr,
                                      sizeof(device), &device) == noErr;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: default_device UID\n");
        return 1;
    }
    AudioObjectID device = DeviceOfUid(argv[1]);
    if (device == kAudioObjectUnknown)
    {
        fprintf(stderr, "no device %s\n", argv[1]);
        return 1;
    }
    if (!MakeDefault(kAudioHardwarePropertyDefaultInputDevice, device) ||
        !MakeDefault(kAudioHardwarePropertyDefaultOutputDevice, device))
    {
        fprintf(stderr, "cannot make %s the default\n", argv[1]);
        return 1;
    }
    printf("%s is the default input and output\n", argv[1]);
    return 0;
}
