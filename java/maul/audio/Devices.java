// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.audio;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;

/**
 * Maul Audio's devices on Android: the device lists, their changes, the
 * microphone permission and audio focus, for a context made with a Java
 * VM and an Android Context. The library creates and closes it; an
 * application only compiles it in.
 */
public final class Devices extends AudioDeviceCallback {
    private final Context context;
    private final AudioManager manager;
    private final Handler main = new Handler(Looper.getMainLooper());
    // The native context's signals, 0 once closed; read and cleared under
    // the object's lock, so no report reaches a closed context.
    private long flag;
    private boolean asked;
    // The focus request held, or null.
    private AudioFocusRequest focus;

    Devices(Context context, long flag) {
        this.context = context;
        this.flag = flag;
        manager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        manager.registerAudioDeviceCallback(this, main);
    }

    void close() {
        manager.unregisterAudioDeviceCallback(this);
        requestFocus(0, false);
        synchronized (this) {
            flag = 0;
        }
    }

    @Override
    public void onAudioDevicesAdded(AudioDeviceInfo[] added) {
        changed();
    }

    @Override
    public void onAudioDevicesRemoved(AudioDeviceInfo[] removed) {
        changed();
    }

    private void changed() {
        synchronized (this) {
            if (flag != 0) {
                raise(flag);
            }
        }
    }

    private static native void raise(long flag);

    private static native void focus(long flag, int change);

    private void focusChanged(int change) {
        synchronized (this) {
            if (flag != 0) {
                focus(flag, change);
            }
        }
    }

    /**
     * Asks for audio focus (1 for good, 2 briefly, 3 briefly over others)
     * or gives it back (0), for a call or for media. Returns
     * AudioManager's result: 0 refused, 1 granted, 2 delayed. A duck is
     * reported, not applied: the host decides.
     */
    int requestFocus(int kind, boolean call) {
        if (focus != null) {
            manager.abandonAudioFocusRequest(focus);
            focus = null;
        }
        if (kind == 0) {
            return AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        }
        int gain = kind == 1 ? AudioManager.AUDIOFOCUS_GAIN
                : kind == 2 ? AudioManager.AUDIOFOCUS_GAIN_TRANSIENT
                            : AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK;
        AudioAttributes attributes =
                new AudioAttributes.Builder()
                        .setUsage(call ? AudioAttributes.USAGE_VOICE_COMMUNICATION
                                       : AudioAttributes.USAGE_MEDIA)
                        .setContentType(call ? AudioAttributes.CONTENT_TYPE_SPEECH
                                             : AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build();
        AudioFocusRequest request =
                new AudioFocusRequest.Builder(gain)
                        .setAudioAttributes(attributes)
                        .setAcceptsDelayedFocusGain(true)
                        .setWillPauseWhenDucked(true)
                        .setOnAudioFocusChangeListener(this::focusChanged, main)
                        .build();
        int result = manager.requestAudioFocus(request);
        if (result != AudioManager.AUDIOFOCUS_REQUEST_FAILED) {
            focus = request;
        }
        return result;
    }

    /**
     * The devices of one direction: in the first array five numbers each
     * (the id, the type, the most channels, the lowest and highest rate,
     * 0 where the device takes any), in the second two strings each (the
     * address and the product name).
     */
    Object[] list(boolean outputs) {
        AudioDeviceInfo[] devices =
                manager.getDevices(outputs ? AudioManager.GET_DEVICES_OUTPUTS
                                           : AudioManager.GET_DEVICES_INPUTS);
        int[] numbers = new int[devices.length * 5];
        String[] texts = new String[devices.length * 2];
        for (int i = 0; i < devices.length; ++i) {
            AudioDeviceInfo device = devices[i];
            int channels = 0;
            for (int count : device.getChannelCounts()) {
                channels = Math.max(channels, count);
            }
            int low = 0;
            int high = 0;
            for (int rate : device.getSampleRates()) {
                low = low == 0 ? rate : Math.min(low, rate);
                high = Math.max(high, rate);
            }
            numbers[i * 5] = device.getId();
            numbers[i * 5 + 1] = device.getType();
            numbers[i * 5 + 2] = channels;
            numbers[i * 5 + 3] = low;
            numbers[i * 5 + 4] = high;
            texts[i * 2] = device.getAddress();
            CharSequence product = device.getProductName();
            texts[i * 2 + 1] = product != null ? product.toString() : "";
        }
        return new Object[] {numbers, texts};
    }

    boolean mayRecord() {
        return context.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                == PackageManager.PERMISSION_GRANTED;
    }

    /** Asks for the microphone once, where the Context is an Activity. */
    void askToRecord() {
        if (asked || !(context instanceof Activity)) {
            return;
        }
        asked = true;
        Activity activity = (Activity) context;
        main.post(() -> activity.requestPermissions(
                new String[] {Manifest.permission.RECORD_AUDIO}, 0x6d61));
    }
}
