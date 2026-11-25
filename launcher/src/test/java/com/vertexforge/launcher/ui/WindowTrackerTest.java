package com.vertexforge.launcher.ui;

import com.vertexforge.launcher.settings.LauncherState.SavedWindow;
import com.vertexforge.launcher.ui.WindowPlacement.Rect;
import org.junit.jupiter.api.Test;

import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;

/** What the main window remembers (VK-1694). */
class WindowTrackerTest {

    private static final Rect START = new Rect(100, 100, 1024, 768);

    @Test
    void boundsCountOnlyOnceTheySettleWhileTheWindowIsNormal() {
        WindowTracker tracker = new WindowTracker(START, false);

        tracker.boundsChanged(new Rect(200, 150, 1100, 800));
        assertEquals(Optional.of(new SavedWindow(100, 100, 1024, 768, false)), tracker.state(), "not settled yet");

        tracker.settled(false, false);
        assertEquals(Optional.of(new SavedWindow(200, 150, 1100, 800, false)), tracker.state());
    }

    /** JDK-8191470: the maximized rectangle is reported before the window says it is maximized. */
    @Test
    void theBoundsOfMaximizingAreNeverTaken() {
        WindowTracker tracker = new WindowTracker(START, false);

        tracker.boundsChanged(new Rect(-8, -8, 1936, 1056));
        tracker.maximizedChanged(true, false);
        tracker.settled(true, false);

        assertEquals(Optional.of(new SavedWindow(100, 100, 1024, 768, true)), tracker.state());
    }

    @Test
    void unMaximizingAndMovingIsRemembered() {
        WindowTracker tracker = new WindowTracker(START, true);

        tracker.maximizedChanged(false, false);
        tracker.boundsChanged(new Rect(300, 200, 900, 700));
        tracker.settled(false, false);

        assertEquals(Optional.of(new SavedWindow(300, 200, 900, 700, false)), tracker.state());
    }

    @Test
    void aMinimizedWindowsParkingPlaceIsNeverTaken() {
        WindowTracker tracker = new WindowTracker(START, false);

        tracker.boundsChanged(new Rect(-32000, -32000, 160, 28));
        tracker.settled(false, true);

        assertEquals(Optional.of(new SavedWindow(100, 100, 1024, 768, false)), tracker.state());
    }

    @Test
    void aMaximizedWindowStaysMaximizedWhileMinimized() {
        WindowTracker tracker = new WindowTracker(START, true);

        // Minimizing may report the window as no longer maximized.
        tracker.maximizedChanged(false, true);

        assertEquals(Optional.of(new SavedWindow(100, 100, 1024, 768, true)), tracker.state());
    }

    @Test
    void invalidBoundsAreNeverTaken() {
        WindowTracker tracker = new WindowTracker(START, false);

        tracker.boundsChanged(new Rect(Double.NaN, 0, 800, 600));
        tracker.settled(false, false);
        tracker.boundsChanged(new Rect(0, 0, 0, 600));
        tracker.settled(false, false);

        assertEquals(Optional.of(new SavedWindow(100, 100, 1024, 768, false)), tracker.state());
    }
}
