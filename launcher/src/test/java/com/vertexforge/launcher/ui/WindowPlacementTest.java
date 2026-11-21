package com.vertexforge.launcher.ui;

import com.vertexforge.launcher.settings.LauncherState.SavedWindow;
import com.vertexforge.launcher.ui.WindowPlacement.Placement;
import com.vertexforge.launcher.ui.WindowPlacement.Rect;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Where the main window opens (VK-1694), in logical pixels. */
class WindowPlacementTest {

    private static final double MIN_W = 800;
    private static final double MIN_H = 600;
    private static final double DEF_W = 1024;
    private static final double DEF_H = 768;

    /** A 1920 by 1080 screen at 100 %, with a 40 px taskbar at the bottom. */
    private static final Rect PRIMARY = new Rect(0, 0, 1920, 1040);
    /** A second monitor to the right. */
    private static final Rect RIGHT = new Rect(1920, 0, 2560, 1400);
    /** A second monitor to the left: negative coordinates. */
    private static final Rect LEFT = new Rect(-1600, 100, 1600, 860);

    private static Placement fit(SavedWindow saved, Rect... screens) {
        return WindowPlacement.fit(Optional.ofNullable(saved), List.of(screens), MIN_W, MIN_H, DEF_W, DEF_H);
    }

    @Test
    void aFirstRunIsCentredOnThePrimaryScreenAtTheDefaultSize() {
        Placement placement = fit(null, PRIMARY, RIGHT);

        assertEquals(new Rect(448, 136, 1024, 768), placement.bounds());
        assertFalse(placement.maximized());
        assertEquals(MIN_W, placement.minWidth());
        assertEquals(MIN_H, placement.minHeight());
    }

    @Test
    void aWindowOnAScreenThatIsStillThereReopensWhereItWas() {
        SavedWindow onRight = new SavedWindow(2100.5, 200.25, 1300, 900, false);

        assertEquals(new Rect(2100.5, 200.25, 1300, 900), fit(onRight, PRIMARY, RIGHT).bounds());
    }

    @Test
    void aWindowWhoseMonitorIsGoneIsCentredOnThePrimaryScreen() {
        SavedWindow onRight = new SavedWindow(2100, 200, 1300, 900, false);

        assertEquals(new Rect(310, 70, 1300, 900), fit(onRight, PRIMARY).bounds());
    }

    @Test
    void aWindowWhoseTitleBarIsOffEveryScreenIsReset() {
        // Mostly visible, but its title bar is above the top of the screen.
        SavedWindow titleAbove = new SavedWindow(100, -200, 1000, 900, false);
        // Only its title bar's corner reaches the screen: under 100 px of it.
        SavedWindow cornerOnly = new SavedWindow(-950, 10, 1000, 700, false);

        assertEquals(new Rect(460, 70, 1000, 900), fit(titleAbove, PRIMARY).bounds());
        assertEquals(new Rect(460, 170, 1000, 700), fit(cornerOnly, PRIMARY).bounds());
    }

    @Test
    void aPartlyVisibleWindowKeepsItsPlace() {
        // Hangs off the right edge, but most of its title bar is on the screen.
        SavedWindow hanging = new SavedWindow(1500, 300, 1000, 700, false);

        assertEquals(new Rect(1500, 300, 1000, 700), fit(hanging, PRIMARY).bounds());
    }

    @Test
    void aTitleBarPartlyAboveTheScreenIsMovedDown() {
        SavedWindow slightlyAbove = new SavedWindow(100, -10, 1000, 700, false);

        assertEquals(new Rect(100, 0, 1000, 700), fit(slightlyAbove, PRIMARY).bounds());
    }

    @Test
    void aWindowLargerThanItsScreenShrinksAndMovesInside() {
        SavedWindow tooBig = new SavedWindow(1700, 50, 2500, 1300, false);

        assertEquals(new Rect(0, 0, 1920, 1040), fit(tooBig, PRIMARY).bounds());
    }

    @Test
    void aWindowOnALeftMonitorKeepsItsNegativeCoordinates() {
        SavedWindow onLeft = new SavedWindow(-1500, 150, 1200, 800, false);

        assertEquals(new Rect(-1500, 150, 1200, 800), fit(onLeft, PRIMARY, LEFT).bounds());
        assertEquals(new Rect(360, 120, 1200, 800), fit(onLeft, PRIMARY).bounds(), "left monitor unplugged");
    }

    @Test
    void aScreenSmallerThanTheMinimumLowersIt() {
        // 1920 by 1080 at 200 %: 960 by 540 logical pixels, about 516 without the taskbar.
        Rect small = new Rect(0, 0, 960, 516);

        Placement first = fit(null, small);
        Placement saved = fit(new SavedWindow(0, 0, 1024, 768, false), small);

        assertEquals(new Rect(0, 0, 960, 516), first.bounds());
        assertEquals(MIN_W, first.minWidth());
        assertEquals(516, first.minHeight());
        assertEquals(new Rect(0, 0, 960, 516), saved.bounds());
        assertEquals(516, saved.minHeight());
    }

    @Test
    void aWindowSmallerThanTheMinimumIsEnlarged() {
        SavedWindow tiny = new SavedWindow(50, 50, 300, 200, false);

        assertEquals(new Rect(50, 50, 800, 600), fit(tiny, PRIMARY).bounds());
    }

    @Test
    void aMaximizedWindowIsMaximizedAgainOnItsScreen() {
        SavedWindow maximizedOnRight = new SavedWindow(2000, 100, 1200, 800, true);

        Placement placement = fit(maximizedOnRight, PRIMARY, RIGHT);

        assertTrue(placement.maximized());
        assertEquals(new Rect(2000, 100, 1200, 800), placement.bounds(), "its normal bounds, to return to");
    }

    @Test
    void aMaximizedWindowWhoseMonitorIsGoneIsMaximizedOnThePrimaryScreen() {
        Placement placement = fit(new SavedWindow(2000, 100, 1200, 800, true), PRIMARY);

        assertTrue(placement.maximized());
        assertTrue(WindowPlacement.screenShowingTitleBar(placement.bounds(), List.of(PRIMARY)).isPresent());
    }

    @Test
    void theScreenThatShowsMostOfTheTitleBarIsChosen() {
        // Across both monitors, mostly on the right one. It is taller than the primary screen (1040) but fits the
        // right one (1400), so it would only be shrunk if the primary screen were chosen.
        SavedWindow across = new SavedWindow(1800, 100, 1400, 1250, false);

        assertEquals(Optional.of(RIGHT), WindowPlacement.screenShowingTitleBar(
                new Rect(1800, 100, 1400, 1250), List.of(PRIMARY, RIGHT)));
        assertEquals(new Rect(1800, 100, 1400, 1250), fit(across, PRIMARY, RIGHT).bounds());
        assertEquals(1040, fit(across, PRIMARY).bounds().height(), "on the primary screen alone it is shrunk");
    }

    @Test
    void screensWithoutAnAreaAreIgnored() {
        Placement placement = WindowPlacement.fit(Optional.empty(), List.of(new Rect(0, 0, 0, 0), PRIMARY),
                MIN_W, MIN_H, DEF_W, DEF_H);
        Placement none = WindowPlacement.fit(Optional.empty(), List.of(), MIN_W, MIN_H, DEF_W, DEF_H);

        assertEquals(new Rect(448, 136, 1024, 768), placement.bounds());
        assertEquals(new Rect(0, 0, 1024, 768), none.bounds());
    }
}
