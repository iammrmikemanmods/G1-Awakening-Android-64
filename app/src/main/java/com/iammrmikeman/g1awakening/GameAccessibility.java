package com.iammrmikeman.g1awakening;

import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewParent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Virtual accessibility views for the controls actually rendered by AndroidUI.
 * No native/game calls occur here. The owner queues ordinary pixel-coordinate
 * input on its render thread, preserving the same hit tests and disabled gates
 * as touch. Node snapshots must come from a successfully presented frame.
 *
 * See https://developer.android.com/reference/android/view/accessibility/AccessibilityNodeProvider
 */
final class GameAccessibility {
    interface Input {
        void tap(float x, float y);
        void scroll(float x, float y, float dy);
    }

    private static final String TAG = "G1Accessibility";
    private static final int NONE = Integer.MIN_VALUE;
    private final View host;
    private final Input input;
    private final AccessibilityManager manager;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicLong revision = new AtomicLong();
    private final AccessibilityNodeProvider provider = new Provider();
    private volatile String lastSubmitted = "";
    // All state below is confined to the Android UI thread.
    private Snapshot snapshot = Snapshot.EMPTY;
    private int accessibilityFocus = NONE;
    private int inputFocus = NONE;
    private int hovered = NONE;
    private int pendingScroll = NONE;
    private boolean contentEventPending;

    GameAccessibility(View host, Input input) {
        this.host = host;
        this.input = input;
        manager = host.getContext().getSystemService(AccessibilityManager.class);
        host.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
    }

    AccessibilityNodeProvider provider() { return provider; }

    /** Safe to call on the render thread. Identical snapshots do no UI work. */
    void update(String json) {
        if (json == null || json.equals(lastSubmitted)) return;
        lastSubmitted = json;
        final long current = revision.incrementAndGet();
        Snapshot decoded;
        try {
            decoded = Snapshot.parse(json);
        } catch (JSONException | IllegalArgumentException error) {
            Log.w(TAG, "Rejected malformed accessibility snapshot", error);
            decoded = Snapshot.EMPTY;
        }
        final Snapshot ready = decoded;
        main.post(() -> { if (revision.get() == current) apply(ready); });
    }

    /** Call when the game surface is hidden, detached, or no longer valid. */
    void clear() {
        lastSubmitted = "";
        final long current = revision.incrementAndGet();
        main.post(() -> { if (revision.get() == current) apply(Snapshot.EMPTY); });
    }

    boolean dispatchHoverEvent(MotionEvent event) {
        if (manager == null || !manager.isEnabled() || !manager.isTouchExplorationEnabled()) return false;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_HOVER_ENTER:
            case MotionEvent.ACTION_HOVER_MOVE: {
                final int found = hit(event.getX(), event.getY());
                final boolean hadHover = hovered != NONE;
                hover(found);
                return found != NONE || hadHover;
            }
            case MotionEvent.ACTION_HOVER_EXIT:
                if (hovered == NONE) return false;
                hover(NONE);
                return true;
            default:
                return false;
        }
    }

    private void apply(Snapshot next) {
        if (accessibilityFocus != NONE && !next.byId.containsKey(accessibilityFocus)) {
            event(accessibilityFocus, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
            accessibilityFocus = NONE;
        }
        if (hovered != NONE && !next.byId.containsKey(hovered)) hover(NONE);
        if (inputFocus != NONE && !next.byId.containsKey(inputFocus)) inputFocus = NONE;
        snapshot = next;
        if (pendingScroll != NONE) {
            if (snapshot.byId.containsKey(pendingScroll)) event(pendingScroll, AccessibilityEvent.TYPE_VIEW_SCROLLED);
            pendingScroll = NONE;
        }
        if (!contentEventPending) {
            contentEventPending = true;
            main.postDelayed(() -> {
                contentEventPending = false;
                event(AccessibilityNodeProvider.HOST_VIEW_ID, AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
            }, 100);
        }
    }

    private void hover(int id) {
        if (hovered == id) return;
        final int previous = hovered;
        hovered = id;
        if (previous != NONE) event(previous, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
        if (id != NONE) event(id, AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
    }

    private int hit(float x, float y) {
        Node best = null;
        for (Node node : snapshot.nodes) {
            if (!node.bounds.contains((int) x, (int) y)) continue;
            // A control or clipped list row wins over its enclosing text or
            // scroll container; disabled controls remain discoverable.
            if (best == null || (node.action != 0 && best.action == 0) ||
                    ((node.action == 0) == (best.action == 0) && node.area() < best.area())) best = node;
        }
        return best == null ? NONE : best.id;
    }

    private boolean visible(Node node) {
        Rect visible = new Rect();
        return host.isShown() && host.isAttachedToWindow() && host.getLocalVisibleRect(visible)
                && visible.intersect(node.bounds);
    }

    private void event(int id, int type) {
        if (manager == null || !manager.isEnabled() || !host.isAttachedToWindow()) return;
        ViewParent parent = host.getParent();
        if (parent == null) return;
        AccessibilityEvent event = new AccessibilityEvent(type);
        event.setPackageName(host.getContext().getPackageName());
        event.setSource(host, id);
        Node node = snapshot.byId.get(id);
        event.setClassName(node == null ? "android.view.ViewGroup" : node.className());
        event.setEnabled(node == null || node.enabled);
        if (node != null) event.setContentDescription(node.label);
        if (type == AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED)
            event.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
        parent.requestSendAccessibilityEvent(host, event);
    }

    private final class Provider extends AccessibilityNodeProvider {
        @Override public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
            if (id == HOST_VIEW_ID) {
                AccessibilityNodeInfo info = new AccessibilityNodeInfo(host);
                host.onInitializeAccessibilityNodeInfo(info);
                info.setClassName("android.view.ViewGroup");
                info.setContentDescription(null);
                info.setScreenReaderFocusable(false);
                info.setFocusable(false);
                for (Node node : snapshot.children(HOST_VIEW_ID)) info.addChild(host, node.id);
                return info;
            }
            final Node node = snapshot.byId.get(id);
            if (node == null) return null;
            AccessibilityNodeInfo info = new AccessibilityNodeInfo(host, id);
            final int parentId = snapshot.parent(id);
            if (parentId == HOST_VIEW_ID) info.setParent(host);
            else info.setParent(host, parentId);
            info.setPackageName(host.getContext().getPackageName());
            info.setClassName(node.className());
            info.setContentDescription(node.label);
            if (!node.hint.isEmpty()) info.setHintText(node.hint);
            if (!node.value.isEmpty()) info.setStateDescription(node.value);
            info.setEnabled(node.enabled);
            info.setFocusable(true);
            info.setScreenReaderFocusable(true);
            info.setFocused(inputFocus == id);
            info.setAccessibilityFocused(accessibilityFocus == id);
            info.setClickable(node.action != 0 && node.enabled);
            info.setScrollable(node.forward || node.backward);
            info.setVisibleToUser(visible(node));
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                Rect window = new Rect(node.bounds);
                int[] windowLocation = new int[2];
                host.getLocationInWindow(windowLocation);
                window.offset(windowLocation[0], windowLocation[1]);
                info.setBoundsInWindow(window);
            } else {
                // Snapshot rectangles use host coordinates; virtual children
                // need bounds relative to their virtual scroll-container parent.
                Rect parentBounds = new Rect(node.bounds);
                Node parentNode = snapshot.byId.get(parentId);
                if (parentNode != null) {
                    parentBounds.offset(-parentNode.bounds.left, -parentNode.bounds.top);
                }
                info.setBoundsInParent(parentBounds);
            }
            Rect screen = new Rect(node.bounds);
            Rect visible = new Rect();
            if (host.getLocalVisibleRect(visible)) screen.intersect(visible);
            int[] location = new int[2];
            host.getLocationOnScreen(location);
            screen.offset(location[0], location[1]);
            info.setBoundsInScreen(screen);
            for (Node child : snapshot.children(id)) info.addChild(host, child.id);
            final List<Node> siblings = snapshot.children(parentId);
            for (int i = 1; i < siblings.size(); ++i)
                if (siblings.get(i).id == id) info.setTraversalAfter(host, siblings.get(i - 1).id);
            info.addAction(accessibilityFocus == id ? AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_ACCESSIBILITY_FOCUS
                    : AccessibilityNodeInfo.AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS);
            info.addAction(inputFocus == id ? AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_FOCUS
                    : AccessibilityNodeInfo.AccessibilityAction.ACTION_FOCUS);
            if (node.enabled) {
                if (node.action != 0) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLICK);
                if (node.forward) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);
                if (node.backward) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);
            }
            return info;
        }

        @Override public List<AccessibilityNodeInfo> findAccessibilityNodeInfosByText(String text, int root) {
            if (text == null || text.isEmpty()) return Collections.emptyList();
            String query = text.toLowerCase(Locale.ROOT);
            List<AccessibilityNodeInfo> result = new ArrayList<>();
            for (Node node : snapshot.nodes) {
                if (root != HOST_VIEW_ID && node.id != root && snapshot.parent(node.id) != root) continue;
                if (node.label.toLowerCase(Locale.ROOT).contains(query)) result.add(createAccessibilityNodeInfo(node.id));
            }
            return result;
        }

        @Override public AccessibilityNodeInfo findFocus(int focus) {
            final int id = focus == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY ? accessibilityFocus : inputFocus;
            return id == NONE ? null : createAccessibilityNodeInfo(id);
        }

        @Override public boolean performAction(int id, int action, Bundle arguments) {
            if (id == HOST_VIEW_ID) return host.performAccessibilityAction(action, arguments);
            Node node = snapshot.byId.get(id);
            if (node == null || !visible(node)) return false;
            switch (action) {
                case AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS:
                    if (manager == null || !manager.isEnabled() || accessibilityFocus == id) return false;
                    if (accessibilityFocus != NONE) event(accessibilityFocus, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                    accessibilityFocus = id;
                    event(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED);
                    host.invalidate();
                    return true;
                case AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS:
                    if (accessibilityFocus != id) return false;
                    accessibilityFocus = NONE;
                    event(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                    host.invalidate();
                    return true;
                case AccessibilityNodeInfo.ACTION_FOCUS:
                    if (inputFocus == id || !host.requestFocus()) return false;
                    inputFocus = id;
                    event(id, AccessibilityEvent.TYPE_VIEW_FOCUSED);
                    return true;
                case AccessibilityNodeInfo.ACTION_CLEAR_FOCUS:
                    if (inputFocus != id) return false;
                    inputFocus = NONE;
                    return true;
                case AccessibilityNodeInfo.ACTION_CLICK:
                    if (!node.enabled || node.action == 0) return false;
                    input.tap(node.centerX, node.centerY);
                    event(id, AccessibilityEvent.TYPE_VIEW_CLICKED);
                    return true;
                case AccessibilityNodeInfo.ACTION_SCROLL_FORWARD:
                case AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD:
                    if (!node.enabled || (action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ? !node.forward : !node.backward)) return false;
                    pendingScroll = id;
                    input.scroll(node.centerX, node.centerY,
                            node.height * (action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ? -.75f : .75f));
                    return true;
                default:
                    return false;
            }
        }
    }

    private static final class Node {
        final int id, action;
        final String label, hint, value;
        final Rect bounds;
        final float centerX, centerY, height;
        final boolean enabled, forward, backward;
        Node(JSONObject json) throws JSONException {
            id = json.getInt("id"); action = json.optInt("action", 0);
            label = json.getString("label"); hint = json.optString("hint", ""); value = json.optString("value", "");
            double x = json.getDouble("x"), y = json.getDouble("y");
            double width = json.getDouble("width"), height = json.getDouble("height");
            if (id < 0 || action < 0 || label.isEmpty() || !Double.isFinite(x) || !Double.isFinite(y)
                    || !Double.isFinite(width) || !Double.isFinite(height)
                    || x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > 100000 || y + height > 100000)
                throw new IllegalArgumentException("Invalid rendered accessibility node");
            bounds = new Rect((int) Math.floor(x), (int) Math.floor(y),
                    (int) Math.ceil(x + width), (int) Math.ceil(y + height));
            // Do not move a click outside a subpixel-clipped production row
            // by rounding its accessibility rectangle to integer pixels.
            centerX = (float) (x + width / 2); centerY = (float) (y + height / 2);
            this.height = (float) height;
            enabled = json.optBoolean("enabled", true);
            forward = json.optBoolean("scroll_forward", false); backward = json.optBoolean("scroll_backward", false);
        }
        long area() { return (long) bounds.width() * bounds.height(); }
        String className() { return action != 0 ? "android.widget.Button" :
                (forward || backward ? "android.widget.ScrollView" : "android.widget.TextView"); }
    }

    private static final class Snapshot {
        static final Snapshot EMPTY = new Snapshot(Collections.emptyList());
        final List<Node> nodes;
        final Map<Integer, Node> byId;
        final Map<Integer, Integer> parents;
        Snapshot(List<Node> list) {
            nodes = Collections.unmodifiableList(list);
            Map<Integer, Node> index = new HashMap<>();
            for (Node node : nodes) if (index.put(node.id, node) != null)
                throw new IllegalArgumentException("Duplicate accessibility node ID");
            byId = Collections.unmodifiableMap(index);
            Map<Integer, Integer> hierarchy = new HashMap<>();
            for (Node child : nodes) {
                Node best = null;
                if (child.action != 0) for (Node candidate : nodes)
                    if (candidate.action == 0 && (candidate.forward || candidate.backward)
                            && candidate.bounds.contains(child.bounds) && (best == null || candidate.area() < best.area())) best = candidate;
                if (best != null) hierarchy.put(child.id, best.id);
            }
            parents = Collections.unmodifiableMap(hierarchy);
        }
        int parent(int id) { Integer parent = parents.get(id); return parent == null ? AccessibilityNodeProvider.HOST_VIEW_ID : parent; }
        List<Node> children(int id) {
            List<Node> result = new ArrayList<>();
            for (Node node : nodes) if (parent(node.id) == id) result.add(node);
            return result;
        }
        static Snapshot parse(String json) throws JSONException {
            JSONArray values = new JSONObject(json).getJSONArray("nodes");
            if (values.length() > 1024) throw new IllegalArgumentException("Accessibility snapshot exceeds rendered-control limit");
            List<Node> nodes = new ArrayList<>();
            for (int i = 0; i < values.length(); ++i) nodes.add(new Node(values.getJSONObject(i)));
            return new Snapshot(nodes);
        }
    }
}
