package com.vertexforge.launcher.ui;

import com.vertexforge.launcher.settings.LauncherState.SavedWindow;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * Where the main window opens (VK-1694): where it was, as long as that is on a screen that is still there; otherwise
 * centred on the primary screen. Pure, so the rules run in unit tests; {@link WindowStateKeeper} feeds it JavaFX's
 * screens.
 * <p>
 * Everything is in JavaFX's logical pixels, in which each screen reports its own bounds (a 1920 by 1080 screen at 200 %
 * scaling is 960 by 540), so a saved rectangle is only ever compared with the screens there are now.
 */
public final class WindowPlacement {

    /** The part of the window's top edge that must lie on a screen: its title bar, without the corners. */
    static final double TITLE_INSET = 8;
    static final double TITLE_HEIGHT = 32;
    /** How much of that strip must be on one screen for the window to count as reachable. */
    static final double MIN_VISIBLE_WIDTH = 100;
    static final double MIN_VISIBLE_HEIGHT = 16;

    private WindowPlacement() {
        // Utility class - prevent instantiation
    }

    /** A rectangle in logical pixels. */
    public record Rect(double x, double y, double width, double height) {

        public double maxX() {
            return x + width;
        }

        public double maxY() {
            return y + height;
        }

        /** The overlap with {@code other}; its width or height is 0 when they do not overlap. */
        Rect intersection(Rect other) {
            double left = Math.max(x, other.x);
            double top = Math.max(y, other.y);
            double right = Math.min(maxX(), other.maxX());
            double bottom = Math.min(maxY(), other.maxY());
            return new Rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top));
        }
    }

    /**
     * Where to put the window.
     *
     * @param bounds    its normal bounds (also what it returns to when it is un-maximized)
     * @param maximized whether to maximize it on top of them
     * @param minWidth  its minimum width: the launcher's own, or the screen's when the screen is smaller
     * @param minHeight its minimum height, likewise
     */
    public record Placement(Rect bounds, boolean maximized, double minWidth, double minHeight) {
    }

    /**
     * @param saved         where the window was last time; empty for its first run
     * @param screens       the screens' usable areas (without the taskbar); the primary screen first
     * @param minWidth      the launcher's minimum width
     * @param minHeight     the launcher's minimum height
     * @param defaultWidth  the size of a first run
     * @param defaultHeight likewise
     */
    public static Placement fit(Optional<SavedWindow> saved, List<Rect> screens, double minWidth, double minHeight,
                                double defaultWidth, double defaultHeight) {
        Objects.requireNonNull(saved, "saved");
        List<Rect> usable = screens.stream().filter(WindowPlacement::hasArea).toList();
        Rect primary = usable.isEmpty()
                ? new Rect(0, 0, Math.max(minWidth, defaultWidth), Math.max(minHeight, defaultHeight))
                : usable.getFirst();
        if (saved.isEmpty()) {
            return centred(primary, defaultWidth, defaultHeight, false, minWidth, minHeight);
        }

        SavedWindow window = saved.get();
        Rect bounds = new Rect(window.x(), window.y(), window.width(), window.height());
        Optional<Rect> screen = screenShowingTitleBar(bounds, usable);
        if (screen.isEmpty()) {
            // Its monitor is gone (or the window was pushed off every screen): start again on the primary screen.
            return centred(primary, window.width(), window.height(), window.maximized(), minWidth, minHeight);
        }
        Rect area = screen.get();
        double effectiveMinWidth = Math.min(minWidth, area.width());
        double effectiveMinHeight = Math.min(minHeight, area.height());
        double width = clamp(window.width(), effectiveMinWidth, area.width());
        double height = clamp(window.height(), effectiveMinHeight, area.height());
        double x = window.x();
        double y = window.y();
        if (width != window.width() || height != window.height()) {
            // It no longer fits that screen: shrunk, and moved inside it.
            x = clamp(x, area.x(), area.maxX() - width);
            y = clamp(y, area.y(), area.maxY() - height);
        } else if (y < area.y()) {
            // A title bar above the screen's top edge cannot be grabbed.
            y = area.y();
        }
        return new Placement(new Rect(x, y, width, height), window.maximized(), effectiveMinWidth,
                effectiveMinHeight);
    }

    /** Whether the window's title bar is far enough on some screen to be grabbed; that screen, the most covered one. */
    static Optional<Rect> screenShowingTitleBar(Rect window, List<Rect> screens) {
        double stripWidth = window.width() - 2 * TITLE_INSET;
        if (stripWidth <= 0) {
            return Optional.empty();
        }
        Rect strip = new Rect(window.x() + TITLE_INSET, window.y(), stripWidth, TITLE_HEIGHT);
        double needWidth = Math.min(MIN_VISIBLE_WIDTH, strip.width());
        Rect best = null;
        double bestArea = 0;
        for (Rect screen : screens) {
            Rect overlap = strip.intersection(screen);
            double area = overlap.width() * overlap.height();
            if (overlap.width() >= needWidth && overlap.height() >= MIN_VISIBLE_HEIGHT && area > bestArea) {
                best = screen;
                bestArea = area;
            }
        }
        return Optional.ofNullable(best);
    }

    private static Placement centred(Rect screen, double width, double height, boolean maximized, double minWidth,
                                     double minHeight) {
        double effectiveMinWidth = Math.min(minWidth, screen.width());
        double effectiveMinHeight = Math.min(minHeight, screen.height());
        double w = clamp(width, effectiveMinWidth, screen.width());
        double h = clamp(height, effectiveMinHeight, screen.height());
        double x = screen.x() + (screen.width() - w) / 2;
        double y = screen.y() + (screen.height() - h) / 2;
        return new Placement(new Rect(x, y, w, h), maximized, effectiveMinWidth, effectiveMinHeight);
    }

    private static boolean hasArea(Rect screen) {
        return screen != null && Double.isFinite(screen.x()) && Double.isFinite(screen.y())
                && Double.isFinite(screen.width()) && Double.isFinite(screen.height())
                && screen.width() > 0 && screen.height() > 0;
    }

    /** {@code value} within {@code min}..{@code max}; {@code max} wins when they cross. */
    private static double clamp(double value, double min, double max) {
        return Math.min(Math.max(value, min), max);
    }
}
