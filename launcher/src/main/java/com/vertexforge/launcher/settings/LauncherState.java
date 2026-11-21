package com.vertexforge.launcher.settings;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * What the launcher remembers by itself between runs (VK-1694), from {@code ~/.vertexforge/launcher-state.json}:
 * where its window was, and how the project list was sorted. Unlike {@link LauncherSettings}, nobody edits these by
 * hand, and they are written in the background.
 *
 * @param window      the main window's last normal bounds and whether it was maximized; empty when never saved
 * @param projectSort the project list's column sort, first key first; empty for the list's own order
 */
public record LauncherState(Optional<SavedWindow> window, List<SortKey> projectSort) {

    /** Nothing remembered yet. */
    public static final LauncherState EMPTY = new LauncherState(Optional.empty(), List.of());

    public LauncherState {
        Objects.requireNonNull(window, "window");
        projectSort = List.copyOf(projectSort);
    }

    /**
     * The main window's NORMAL bounds (never the maximized or minimized ones), in JavaFX's logical pixels, and whether
     * it was maximized on top of them.
     */
    public record SavedWindow(double x, double y, double width, double height, boolean maximized) {
        public SavedWindow {
            if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(width) || !Double.isFinite(height)
                    || width <= 0 || height <= 0) {
                throw new IllegalArgumentException("Invalid window bounds: " + x + ", " + y + ", " + width + " x "
                        + height);
            }
        }

        /** Whether {@link #SavedWindow} accepts these values. */
        public static boolean valid(double x, double y, double width, double height) {
            return Double.isFinite(x) && Double.isFinite(y) && Double.isFinite(width) && Double.isFinite(height)
                    && width > 0 && height > 0;
        }
    }

    /**
     * One key of the project list's sort.
     *
     * @param column    the column's stable key (see {@code ProjectSort})
     * @param ascending its direction
     */
    public record SortKey(String column, boolean ascending) {
        public SortKey {
            Objects.requireNonNull(column, "column");
        }
    }

    public LauncherState withWindow(SavedWindow newWindow) {
        return new LauncherState(Optional.of(Objects.requireNonNull(newWindow, "newWindow")), projectSort);
    }

    public LauncherState withProjectSort(List<SortKey> newSort) {
        return new LauncherState(window, newSort);
    }
}
