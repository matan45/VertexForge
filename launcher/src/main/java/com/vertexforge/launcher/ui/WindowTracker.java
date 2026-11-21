package com.vertexforge.launcher.ui;

import com.vertexforge.launcher.settings.LauncherState.SavedWindow;
import com.vertexforge.launcher.ui.WindowPlacement.Rect;

import java.util.Objects;
import java.util.Optional;

/**
 * Follows the main window to remember its NORMAL bounds and whether it is maximized (VK-1694). Pure, so the rules run
 * in unit tests; {@link WindowStateKeeper} feeds it the stage's events.
 * <ul>
 *   <li>Bounds count only once they have settled while the window is neither maximized nor minimized. JavaFX reports
 *       a window's new bounds before it reports that it was maximized (JDK-8191470), and Windows parks a minimized
 *       window far off screen, so bounds taken at once would remember the maximized rectangle or the parking place.</li>
 *   <li>A change of {@code maximized} while minimized is ignored: the window comes back as it was.</li>
 * </ul>
 */
public final class WindowTracker {

    private Rect normal;
    private Rect candidate;
    private boolean maximized;

    /**
     * @param normal    the normal bounds the window was given
     * @param maximized whether it was maximized on top of them
     */
    public WindowTracker(Rect normal, boolean maximized) {
        this.normal = Objects.requireNonNull(normal, "normal");
        this.maximized = maximized;
    }

    /** The window moved or was resized; nothing counts until {@link #settled}. */
    public void boundsChanged(Rect bounds) {
        candidate = Objects.requireNonNull(bounds, "bounds");
    }

    /** The bounds have stopped changing; they count when the window is normal now. */
    public void settled(boolean maximizedNow, boolean iconifiedNow) {
        if (candidate != null && !maximizedNow && !iconifiedNow
                && SavedWindow.valid(candidate.x(), candidate.y(), candidate.width(), candidate.height())) {
            normal = candidate;
        }
        candidate = null;
    }

    /** The window was maximized or restored; ignored while it is minimized. */
    public void maximizedChanged(boolean maximizedNow, boolean iconifiedNow) {
        if (!iconifiedNow) {
            maximized = maximizedNow;
        }
    }

    /** What to remember now. */
    public Optional<SavedWindow> state() {
        if (!SavedWindow.valid(normal.x(), normal.y(), normal.width(), normal.height())) {
            return Optional.empty();
        }
        return Optional.of(new SavedWindow(normal.x(), normal.y(), normal.width(), normal.height(), maximized));
    }
}
