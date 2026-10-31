package com.iammrmikeman.g1awakening;

import android.content.Context;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.view.Choreographer;
import android.view.Display;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeProvider;

import java.io.File;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Owns one native game session and serializes EGL, input and lifecycle work. */
final class GameSurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    interface FailureListener { void onFailure(String detail, Throwable failure); }
    private static final String TAG = "G1Surface";
    private final HandlerThread renderThread = new HandlerThread("G1-Render");
    private final Handler renderHandler;
    private final FailureListener failureListener;
    private final CountDownLatch shutdownReceipt = new CountDownLatch(1);
    private final GestureDetector gestures;
    private final ScaleGestureDetector scales;
    private final GameAccessibility accessibility;
    private final AccessibilityManager accessibilityManager;
    private volatile boolean accessibilityEnabled;
    private final AccessibilityManager.AccessibilityStateChangeListener accessibilityStateListener =
            this::accessibilityStateChanged;
    private volatile boolean closed;
    private volatile int surfaceGeneration;
    private int surfaceWidth;
    private int surfaceHeight;
    private int insetLeft;
    private int insetTop;
    private int insetRight;
    private int insetBottom;

    // The fields below belong exclusively to renderThread.
    private long handle;
    private Choreographer choreographer;
    private boolean resumed;
    private boolean surfaceReady;
    private boolean surfaceSized;
    private boolean framePending;
    private boolean failed;
    private long previousFrame;
    private boolean accessibilityForce = true;
    private final Choreographer.FrameCallback frameCallback = this::drawFrame;

    GameSurfaceView(Context context, File assetRoot, File saveRoot, FailureListener listener) {
        super(context);
        failureListener = listener;
        setFocusable(true);
        setFocusableInTouchMode(true);
        getHolder().addCallback(this);
        renderThread.start();
        renderHandler = new Handler(renderThread.getLooper());
        dispatch(() -> {
            choreographer = Choreographer.getInstance();
            handle = NativeBridge.nativeCreate(assetRoot.getAbsolutePath(), saveRoot.getAbsolutePath(), false);
            if (handle == 0) throw new IllegalStateException("Native session initialization returned no session.");
            NativeBridge.nativePause(handle);
        });
        scales = new ScaleGestureDetector(context, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override public boolean onScale(ScaleGestureDetector detector) {
                sendTouch(NativeBridge.TOUCH_ZOOM, detector.getFocusX(), detector.getFocusY(),
                        0, 0, detector.getScaleFactor());
                return true;
            }
        });
        gestures = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent event) { return true; }

            @Override public boolean onSingleTapUp(MotionEvent event) {
                performClick();
                sendTouch(NativeBridge.TOUCH_TAP, event.getX(), event.getY(), 0, 0, 1);
                return true;
            }

            @Override public boolean onScroll(MotionEvent first, MotionEvent current, float dx, float dy) {
                if (!scales.isInProgress() && current.getPointerCount() == 1) {
                    sendTouch(NativeBridge.TOUCH_PAN, current.getX(), current.getY(), -dx, -dy, 1);
                }
                return true;
            }
        });
        gestures.setIsLongpressEnabled(false);
        accessibility = new GameAccessibility(this, new GameAccessibility.Input() {
            @Override public void tap(float x, float y) {
                sendTouch(NativeBridge.TOUCH_TAP, x, y, 0, 0, 1);
            }
            @Override public void scroll(float x, float y, float dy) {
                sendTouch(NativeBridge.TOUCH_PAN, x, y, 0, dy, 1);
            }
        });
        accessibilityManager = context.getSystemService(AccessibilityManager.class);
        accessibilityEnabled = accessibilityManager != null && accessibilityManager.isEnabled();
        if (accessibilityManager != null) {
            accessibilityManager.addAccessibilityStateChangeListener(accessibilityStateListener);
        }
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
    }

    @Override public AccessibilityNodeProvider getAccessibilityNodeProvider() {
        return accessibility == null ? super.getAccessibilityNodeProvider() : accessibility.provider();
    }

    @Override public boolean dispatchHoverEvent(MotionEvent event) {
        return (accessibility != null && accessibility.dispatchHoverEvent(event)) || super.dispatchHoverEvent(event);
    }

    private void accessibilityStateChanged(boolean enabled) {
        accessibilityEnabled = enabled;
        if (!enabled) accessibility.clear();
        dispatch(() -> accessibilityForce = true);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (closed) return false;
        scales.onTouchEvent(event);
        gestures.onTouchEvent(event);
        return true;
    }

    @Override public boolean performClick() {
        super.performClick();
        return true;
    }

    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
            sendTouch(NativeBridge.TOUCH_SCROLL, event.getX(), event.getY(),
                    event.getAxisValue(MotionEvent.AXIS_HSCROLL),
                    event.getAxisValue(MotionEvent.AXIS_VSCROLL), 1);
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    private void sendTouch(int action, float x, float y, float dx, float dy, float scale) {
        dispatch(() -> {
            if (handle != 0 && resumed && surfaceReady) {
                NativeBridge.nativeTouch(handle, action, x, y, dx, dy, scale);
            }
        });
    }

    void back(Runnable unhandled) {
        dispatch(() -> {
            if (handle != 0 && !NativeBridge.nativeBack(handle)) {
                post(() -> { if (!closed) unhandled.run(); });
            }
        });
    }

    void setAudioFocused(boolean focused) {
        dispatch(() -> { if (handle != 0) NativeBridge.nativeAudioFocus(handle, focused); });
    }

    void setResourcePressure(boolean lowPower, boolean thermalSerious, boolean memoryPressure) {
        dispatch(() -> {
            if (handle != 0) NativeBridge.nativeResourcePressure(handle, lowPower, thermalSerious, memoryPressure);
        });
    }

    void setResumed(boolean active) {
        dispatch(() -> {
            if (resumed == active) return;
            resumed = active;
            previousFrame = 0;
            accessibilityForce = true;
            if (!active) post(accessibility::clear);
            if (handle != 0) {
                if (active) NativeBridge.nativeResume(handle);
                else NativeBridge.nativePause(handle);
            }
            if (active) scheduleFrame();
            else cancelFrame();
        });
    }

    void setSafeInsets(int left, int top, int right, int bottom) {
        insetLeft = left;
        insetTop = top;
        insetRight = right;
        insetBottom = bottom;
        sendResize();
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        final int generation = ++surfaceGeneration;
        surfaceWidth = 0;
        surfaceHeight = 0;
        Surface surface = holder.getSurface();
        requestRefreshRate(surface);
        dispatch(() -> {
            if (handle != 0 && generation == surfaceGeneration && surface.isValid()) {
                NativeBridge.nativeSurfaceCreated(handle, surface);
                surfaceReady = true;
                surfaceSized = false;
                previousFrame = 0;
                accessibilityForce = true;
                if (resumed) NativeBridge.nativeResume(handle);
                scheduleFrame();
            }
        });
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        surfaceWidth = width;
        surfaceHeight = height;
        sendResize();
    }

    private void sendResize() {
        final int generation = surfaceGeneration;
        int width = surfaceWidth, height = surfaceHeight;
        int left = insetLeft, top = insetTop, right = insetRight, bottom = insetBottom;
        if (width <= 0 || height <= 0) return;
        dispatch(() -> {
            if (handle != 0 && surfaceReady && generation == surfaceGeneration) {
                NativeBridge.nativeResize(handle, width, height, left, top, right, bottom);
                surfaceSized = true;
                scheduleFrame();
            }
        });
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        ++surfaceGeneration;
        surfaceWidth = 0;
        surfaceHeight = 0;
        if (closed) {
            awaitReceipt(shutdownReceipt, "Native session shutdown");
            return;
        }
        CountDownLatch receipt = new CountDownLatch(1);
        renderHandler.post(() -> {
            try {
                surfaceReady = false;
                surfaceSized = false;
                accessibilityForce = true;
                post(accessibility::clear);
                cancelFrame();
                previousFrame = 0;
                if (handle != 0) NativeBridge.nativeSurfaceDestroyed(handle);
            } catch (Throwable failure) {
                fail("Could not release the game surface.", failure);
            } finally {
                receipt.countDown();
            }
        });
        awaitReceipt(receipt, "Native surface release");
    }

    void close() {
        if (closed) return;
        closed = true;
        accessibility.clear();
        if (accessibilityManager != null) {
            accessibilityManager.removeAccessibilityStateChangeListener(accessibilityStateListener);
        }
        renderHandler.post(() -> {
            try {
                resumed = false;
                cancelFrame();
                if (handle != 0) {
                    NativeBridge.nativePause(handle);
                    if (surfaceReady) NativeBridge.nativeSurfaceDestroyed(handle);
                    surfaceReady = false;
                    NativeBridge.nativeDestroy(handle);
                    handle = 0;
                }
            } catch (Throwable failure) {
                Log.e(TAG, "Native session shutdown failed", failure);
            } finally {
                shutdownReceipt.countDown();
                renderThread.quitSafely();
            }
        });
        awaitReceipt(shutdownReceipt, "Native session shutdown");
    }

    private static void awaitReceipt(CountDownLatch receipt, String operation) {
        try {
            if (!receipt.await(2, TimeUnit.SECONDS)) Log.e(TAG, operation + " exceeded two seconds.");
        } catch (InterruptedException interruption) {
            Thread.currentThread().interrupt();
        }
    }

    private void drawFrame(long frameTimeNanos) {
        framePending = false;
        if (!canRender()) return;
        try {
            long elapsed = previousFrame == 0 ? 0 : Math.max(0, frameTimeNanos - previousFrame);
            previousFrame = frameTimeNanos;
            NativeBridge.nativeRender(handle, elapsed);
            if (accessibilityEnabled) {
                String snapshot = NativeBridge.nativeAccessibilitySnapshot(handle, accessibilityForce);
                if (snapshot != null) {
                    accessibilityForce = false;
                    accessibility.update(snapshot);
                }
            }
            scheduleFrame();
        } catch (Throwable failure) {
            fail("The native renderer could not present a frame.", failure);
        }
    }

    private boolean canRender() {
        return !closed && !failed && resumed && surfaceReady && surfaceSized && handle != 0;
    }

    private void scheduleFrame() {
        if (canRender() && !framePending && choreographer != null) {
            framePending = true;
            choreographer.postFrameCallback(frameCallback);
        }
    }

    private void cancelFrame() {
        if (framePending && choreographer != null) choreographer.removeFrameCallback(frameCallback);
        framePending = false;
    }

    private void dispatch(Runnable operation) {
        if (closed) return;
        renderHandler.post(() -> {
            if (closed || failed) return;
            try {
                operation.run();
            } catch (Throwable failure) {
                fail("The native game session could not continue.", failure);
            }
        });
    }

    private void fail(String detail, Throwable failure) {
        if (failed) return;
        failed = true;
        cancelFrame();
        Log.e(TAG, detail, failure);
        post(() -> {
            if (accessibility != null) accessibility.clear();
            if (!closed) failureListener.onFailure(detail, failure);
        });
    }

    private void requestRefreshRate(Surface surface) {
        Display display = getDisplay();
        if (display == null || !surface.isValid()) return;
        float maximum = 60;
        Display.Mode active = display.getMode();
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() == active.getPhysicalWidth()
                    && mode.getPhysicalHeight() == active.getPhysicalHeight()
                    && mode.getRefreshRate() <= 120.1f) {
                maximum = Math.max(maximum, mode.getRefreshRate());
            }
        }
        try {
            surface.setFrameRate(maximum, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
                    Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
            Log.i(TAG, "Requested display refresh: " + maximum + " Hz; simulation timing remains independent.");
        } catch (IllegalArgumentException | IllegalStateException unavailable) {
            Log.w(TAG, "Display did not accept the refresh request", unavailable);
        }
    }
}
