// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.audio;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;

/**
 * Maul Audio's devices on Android: the device lists, their changes and
 * the microphone permission, for a context made with a Java VM and an
 * Android Context. The library creates and closes it; an application
 * only compiles it in.
 */
public final class Devices extends AudioDeviceCallback {
    private final Context context;
    private final AudioManager manager;
    private final Handler main = new Handler(Looper.getMainLooper());
    // The native context's change flag, 0 once closed; read and cleared
    // under the object's lock, so no report reaches a closed context.
    private long flag;
    private boolean asked;

    Devices(Context context, long flag) {
        this.context = context;
        this.flag = flag;
        manager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        manager.registerAudioDeviceCallback(this, main);
    }

    void close() {
        manager.unregisterAudioDeviceCallback(this);
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
