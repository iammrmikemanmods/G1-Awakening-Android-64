package com.iammrmikeman.g1awakening;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;

/** Matches the source platform's system-font fallback for missing bitmap glyphs. */
public final class TextRasterizer {
    private static final int MAX_DIMENSION = 8192;
    private static final long MAX_PIXELS = 4L * 1024L * 1024L;
    private TextRasterizer() {}

    /** Returns {width, height, ARGB pixel 0, ...}, with a transparent background. */
    public static int[] rasterizeText(String text, float pixelSize) {
        if (text == null || !Float.isFinite(pixelSize) || pixelSize <= 0 || pixelSize > 512) {
            throw new IllegalArgumentException("Invalid fallback text size or content.");
        }
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        paint.setColor(Color.WHITE);
        paint.setTypeface(Typeface.MONOSPACE);
        paint.setTextSize(pixelSize);
        Rect bounds = new Rect();
        paint.getTextBounds(text, 0, text.length(), bounds);
        Paint.FontMetrics metrics = paint.getFontMetrics();
        float left = Math.min(0, bounds.left);
        float right = Math.max(paint.measureText(text), bounds.right);
        float top = Math.min(metrics.ascent, bounds.top);
        float bottom = Math.max(metrics.descent, bounds.bottom);
        final int padding = 1;
        if (!Float.isFinite(right - left) || !Float.isFinite(bottom - top)
                || right - left > MAX_DIMENSION - 2 * padding
                || bottom - top > MAX_DIMENSION - 2 * padding) {
            throw new IllegalArgumentException("Fallback text bitmap exceeds safe dimensions.");
        }
        int width = Math.max(1, (int) Math.ceil(right - left) + 2 * padding);
        int height = Math.max(1, (int) Math.ceil(bottom - top) + 2 * padding);
        if (width > MAX_DIMENSION || height > MAX_DIMENSION || (long) width * height > MAX_PIXELS) {
            throw new IllegalArgumentException("Fallback text bitmap exceeds safe dimensions.");
        }
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        try {
            Canvas canvas = new Canvas(bitmap);
            canvas.drawText(text, padding - left, padding - top, paint);
            int[] result = new int[2 + width * height];
            result[0] = width;
            result[1] = height;
            bitmap.getPixels(result, 2, width, 0, 0, width, height);
            return result;
        } finally {
            bitmap.recycle();
        }
    }
}
