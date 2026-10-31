package com.iammrmikeman.g1awakening;

import android.view.Surface;

/** JNI boundary. GameSurfaceView serializes every call on its render thread. */
final class NativeBridge {
    static {
        System.loadLibrary("g1awakening");
    }

    static final int TOUCH_TAP = 0;
    static final int TOUCH_PAN = 1;
    static final int TOUCH_ZOOM = 2;
    static final int TOUCH_SCROLL = 3;

    private NativeBridge() {}

    static native long nativeCreate(String assetRoot, String saveRoot, boolean modern);
    static native void nativeSurfaceCreated(long handle, Surface surface);
    static native void nativeSurfaceDestroyed(long handle);
    static native void nativeResize(long handle, int width, int height,
                                    int insetLeft, int insetTop, int insetRight, int insetBottom);
    /** elapsedNanos is the delta from the preceding frame; zero after resume. */
    static native void nativeRender(long handle, long elapsedNanos);
    /** Coordinates and pan deltas are surface pixels; zoom is a relative factor. */
    static native void nativeTouch(long handle, int action, float x, float y,
                                   float dx, float dy, float scale);
    static native boolean nativeBack(long handle);
    static native void nativePause(long handle);
    static native void nativeResume(long handle);
    static native void nativeAudioFocus(long handle, boolean focused);
    static native void nativeResourcePressure(long handle, boolean lowPower, boolean thermalSerious,
                                              boolean memoryPressure);
    static native String nativeAccessibilitySnapshot(long handle, boolean force);
    static native void nativeDestroy(long handle);
}
