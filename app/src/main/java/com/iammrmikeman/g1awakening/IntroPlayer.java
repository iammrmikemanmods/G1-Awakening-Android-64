package com.iammrmikeman.g1awakening;

import android.content.Context;
import android.graphics.Matrix;
import android.graphics.SurfaceTexture;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.util.Log;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;

import java.io.File;
import java.io.IOException;

/** Plays the unchanged iOS intro, retaining its aspect ratio and lifecycle position. */
final class IntroPlayer extends TextureView implements TextureView.SurfaceTextureListener {
    interface Listener {
        void onComplete();
        void onError(String message, Throwable failure);
    }
    private static final String TAG = "G1Intro";
    private final File movie;
    private final Listener listener;
    private MediaPlayer player;
    private Surface movieSurface;
    private boolean active;
    private boolean audioFocused;
    private boolean prepared;
    private boolean ended;
    private int position;
    private int videoWidth;
    private int videoHeight;

    IntroPlayer(Context context, File movie, int savedPosition, Listener listener) {
        super(context);
        this.movie = movie;
        this.listener = listener;
        position = Math.max(0, savedPosition);
        setOpaque(false);
        setClickable(true);
        setFocusable(true);
        setSurfaceTextureListener(this);
        setContentDescription(context.getString(R.string.intro_accessibility));
    }

    void resume() {
        active = true;
        if (ended) return;
        if (player == null && isAvailable()) prepare(getSurfaceTexture());
        else if (prepared) startPrepared();
    }

    void pause() {
        active = false;
        if (player != null && prepared) {
            try {
                position = player.getCurrentPosition();
                if (player.isPlaying()) player.pause();
            } catch (IllegalStateException failure) {
                error("The opening cinematic could not be paused.", failure);
            }
        }
    }

    void releaseForBackground() {
        rememberPosition();
        releasePlayer();
    }

    int position() {
        rememberPosition();
        return position;
    }

    void skip() { finish(); }

    void setAudioFocused(boolean focused) {
        audioFocused = focused;
        if (player != null && prepared) player.setVolume(focused ? 1f : 0f, focused ? 1f : 0f);
    }

    void close() {
        ended = true;
        active = false;
        releasePlayer();
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_UP) {
            performClick();
        }
        return true;
    }

    @Override public boolean performClick() {
        super.performClick();
        finish();
        return true;
    }

    @Override public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) {
        if (!ended) prepare(texture);
    }

    @Override public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) {
        updateTransform();
    }

    @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture texture) {
        rememberPosition();
        releasePlayer();
        return true;
    }

    @Override public void onSurfaceTextureUpdated(SurfaceTexture texture) {}

    private void prepare(SurfaceTexture texture) {
        if (ended || texture == null || player != null) return;
        MediaPlayer pending = new MediaPlayer();
        player = pending;
        prepared = false;
        try {
            pending.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).build());
            movieSurface = new Surface(texture);
            pending.setSurface(movieSurface);
            pending.setDataSource(movie.getAbsolutePath());
            pending.setOnPreparedListener(ready -> {
                if (ended || ready != player) return;
                prepared = true;
                ready.setVolume(audioFocused ? 1f : 0f, audioFocused ? 1f : 0f);
                videoWidth = ready.getVideoWidth();
                videoHeight = ready.getVideoHeight();
                updateTransform();
                if (position > 0) ready.seekTo(position, MediaPlayer.SEEK_CLOSEST);
                if (active) startPrepared();
            });
            pending.setOnVideoSizeChangedListener((source, width, height) -> {
                if (source != player || ended) return;
                videoWidth = width;
                videoHeight = height;
                updateTransform();
            });
            pending.setOnCompletionListener(source -> { if (source == player) finish(); });
            pending.setOnErrorListener((source, what, extra) -> {
                if (source == player && !ended) {
                    error("The original iOS cinematic could not be decoded (" + what + ", " + extra + ").",
                            new IOException("Android MediaPlayer error " + what + "/" + extra));
                }
                return true;
            });
            pending.prepareAsync();
        } catch (IOException | IllegalStateException | IllegalArgumentException failure) {
            error("The original iOS cinematic could not be opened.", failure);
        }
    }

    private void startPrepared() {
        if (!prepared || player == null || ended || !active) return;
        try {
            player.start();
        } catch (IllegalStateException failure) {
            error("The opening cinematic could not begin playback.", failure);
        }
    }

    private void updateTransform() {
        if (getWidth() <= 0 || getHeight() <= 0 || videoWidth <= 0 || videoHeight <= 0) return;
        float fit = Math.min((float) getWidth() / videoWidth, (float) getHeight() / videoHeight);
        float width = videoWidth * fit;
        float height = videoHeight * fit;
        Matrix transform = new Matrix();
        transform.setScale(width / getWidth(), height / getHeight(), getWidth() / 2f, getHeight() / 2f);
        setTransform(transform);
    }

    private void rememberPosition() {
        if (player != null && prepared) {
            try {
                position = player.getCurrentPosition();
            } catch (IllegalStateException failure) {
                Log.w(TAG, "Intro position unavailable during lifecycle transition", failure);
            }
        }
    }

    private void releasePlayer() {
        MediaPlayer oldPlayer = player;
        player = null;
        prepared = false;
        if (oldPlayer != null) {
            oldPlayer.setOnPreparedListener(null);
            oldPlayer.setOnCompletionListener(null);
            oldPlayer.setOnErrorListener(null);
            oldPlayer.setOnVideoSizeChangedListener(null);
            oldPlayer.release();
        }
        if (movieSurface != null) {
            movieSurface.release();
            movieSurface = null;
        }
    }

    private void finish() {
        if (ended) return;
        ended = true;
        releasePlayer();
        listener.onComplete();
    }

    private void error(String message, Throwable failure) {
        if (ended) return;
        ended = true;
        releasePlayer();
        Log.e(TAG, message, failure);
        listener.onError(message, failure);
    }
}
