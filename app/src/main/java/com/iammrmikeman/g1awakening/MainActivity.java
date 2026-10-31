package com.iammrmikeman.g1awakening;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.graphics.Insets;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Android platform shell. All game UI and game rules come from the iOS source. */
public final class MainActivity extends Activity {
    private static final String TAG = "G1Activity";
    private static final String STATE_INTRO_COMPLETE = "introComplete";
    private static final String STATE_INTRO_POSITION = "introPosition";
    private final ExecutorService importer = Executors.newSingleThreadExecutor(runnable ->
            new Thread(runnable, "G1-Asset-Verification"));
    private final OnBackInvokedCallback backCallback = this::handleBack;
    private FrameLayout root;
    private TextView loading;
    private GameSurfaceView game;
    private IntroPlayer intro;
    private File assetRoot;
    private File saveRoot;
    private boolean resumed;
    private boolean closed;
    private boolean failed;
    private boolean introComplete;
    private int introPosition;
    private Insets safeInsets = Insets.NONE;
    private AudioManager audioManager;
    private AudioFocusRequest audioFocusRequest;
    private boolean audioFocused;
    private PowerManager powerManager;
    private boolean memoryPressure;
    private boolean powerReceiverRegistered;
    private final PowerManager.OnThermalStatusChangedListener thermalListener = status -> updateResourcePressure();
    private final BroadcastReceiver powerReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) { updateResourcePressure(); }
    };

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        powerManager = getSystemService(PowerManager.class);
        powerManager.addThermalStatusListener(getMainExecutor(), thermalListener);
        registerReceiver(powerReceiver, new IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED),
                Context.RECEIVER_NOT_EXPORTED);
        powerReceiverRegistered = true;
        audioManager = getSystemService(AudioManager.class);
        audioFocusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setWillPauseWhenDucked(true)
                .setOnAudioFocusChangeListener(change -> {
                    if (!closed) setAudioFocused(resumed && change == AudioManager.AUDIOFOCUS_GAIN);
                }, new Handler(Looper.getMainLooper()))
                .build();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setDecorFitsSystemWindows(false);
        WindowManager.LayoutParams parameters = getWindow().getAttributes();
        parameters.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        getWindow().setAttributes(parameters);
        if (state != null) {
            introComplete = state.getBoolean(STATE_INTRO_COMPLETE, false);
            introPosition = state.getInt(STATE_INTRO_POSITION, 0);
        }
        // Development-only route lets automated tests reach the actual native menu promptly.
        if (BuildConfig.DEBUG && getIntent().getBooleanExtra("skip_intro", false)) introComplete = true;
        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            safeInsets = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            if (game != null) applySafeInsets();
            return insets;
        });
        loading = new TextView(this);
        loading.setText(R.string.preparing_game);
        loading.setTextColor(Color.WHITE);
        loading.setTextSize(16);
        loading.setGravity(Gravity.CENTER);
        root.addView(loading, fillParent());
        setContentView(root);
        getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        immersive();
        importer.execute(() -> {
            try {
                File installed = AssetInstaller.install(getAssets(), getFilesDir(), (completed, total) -> {
                    if (completed == total || completed % 8 == 0) {
                        runOnUiThread(() -> {
                            if (!closed && loading != null) loading.setText(
                                    getString(R.string.verifying_game, completed, total));
                        });
                    }
                });
                File saves = new File(getFilesDir(), "saves");
                if (!saves.isDirectory() && !saves.mkdirs()) {
                    throw new IOException("The private save directory could not be created.");
                }
                runOnUiThread(() -> {
                    if (closed || isFinishing()) return;
                    assetRoot = installed;
                    saveRoot = saves;
                    showContent();
                });
            } catch (Exception failure) {
                runOnUiThread(() -> showFailure("Game assets could not be verified.", failure));
            }
        });
    }

    private void showContent() {
        if (closed || failed) return;
        root.removeAllViews();
        loading = null;
        if (introComplete) {
            startGame();
            return;
        }
        intro = new IntroPlayer(this, new File(assetRoot, "G1OpeningXR.mp4"), introPosition,
                new IntroPlayer.Listener() {
                    @Override public void onComplete() {
                        introComplete = true;
                        if (intro != null) {
                            root.removeView(intro);
                            intro = null;
                        }
                        startGame();
                    }
                    @Override public void onError(String message, Throwable failure) {
                        showFailure(message, failure);
                    }
                });
        root.addView(intro, fillParent());
        intro.setAudioFocused(audioFocused);
        if (resumed) intro.resume();
    }

    private void startGame() {
        if (closed || failed || game != null) return;
        game = new GameSurfaceView(this, assetRoot, saveRoot, this::showFailure);
        root.addView(game, fillParent());
        applySafeInsets();
        game.setAudioFocused(audioFocused);
        updateResourcePressure();
        game.setResumed(resumed);
        game.requestFocus();
        root.requestApplyInsets();
        immersive();
    }

    private void applySafeInsets() {
        game.setSafeInsets(safeInsets.left, safeInsets.top, safeInsets.right, safeInsets.bottom);
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        getSystemService(ActivityManager.class).getMemoryInfo(memory);
        memoryPressure = memory.lowMemory;
        updateResourcePressure();
        setAudioFocused(audioManager.requestAudioFocus(audioFocusRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
        immersive();
        if (intro != null) intro.resume();
        if (game != null) game.setResumed(true);
    }

    @Override protected void onPause() {
        resumed = false;
        if (intro != null) intro.pause();
        if (game != null) game.setResumed(false);
        audioManager.abandonAudioFocusRequest(audioFocusRequest);
        setAudioFocused(false);
        super.onPause();
    }

    @Override protected void onStop() {
        if (intro != null) intro.releaseForBackground();
        super.onStop();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putBoolean(STATE_INTRO_COMPLETE, introComplete);
        state.putInt(STATE_INTRO_POSITION, intro == null ? introPosition : intro.position());
        super.onSaveInstanceState(state);
    }

    @Override protected void onDestroy() {
        closed = true;
        powerManager.removeThermalStatusListener(thermalListener);
        if (powerReceiverRegistered) unregisterReceiver(powerReceiver);
        getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
        importer.shutdownNow();
        if (intro != null) intro.close();
        if (game != null) game.close();
        super.onDestroy();
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) immersive();
    }

    private void handleBack() {
        if (intro != null) intro.skip();
        else if (game != null) game.back(this::finish);
        else finish();
    }

    private void setAudioFocused(boolean focused) {
        audioFocused = focused;
        if (game != null) game.setAudioFocused(focused);
        if (intro != null) intro.setAudioFocused(focused);
    }

    @Override public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        // UI_HIDDEN alone is a lifecycle event, not a memory-pressure signal.
        if ((level >= TRIM_MEMORY_RUNNING_MODERATE && level <= TRIM_MEMORY_RUNNING_CRITICAL)
                || level >= TRIM_MEMORY_BACKGROUND) {
            memoryPressure = true;
            updateResourcePressure();
        }
    }

    private void updateResourcePressure() {
        if (!closed && game != null) {
            game.setResourcePressure(powerManager.isPowerSaveMode(),
                    powerManager.getCurrentThermalStatus() >= PowerManager.THERMAL_STATUS_SEVERE,
                    memoryPressure);
        }
    }

    private void immersive() {
        WindowInsetsController controller = getWindow().getInsetsController();
        if (controller != null) {
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            controller.hide(WindowInsets.Type.systemBars());
        }
    }

    private void showFailure(String message, Throwable failure) {
        if (closed || failed || isFinishing()) return;
        failed = true;
        Log.e(TAG, message, failure);
        if (game != null) game.setResumed(false);
        if (intro != null) intro.pause();
        String detail = failure.getMessage();
        new AlertDialog.Builder(this)
                .setTitle(R.string.start_failed)
                .setMessage(message + (detail == null ? "" : "\n\n" + detail))
                .setCancelable(false)
                .setPositiveButton(R.string.close_game, (dialog, which) -> finish())
                .show();
    }

    private static FrameLayout.LayoutParams fillParent() {
        return new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT);
    }
}
