package com.vertexforge.launcher.controller;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.Objects;

/**
 * How often the window checks every project again when it gets the focus back (VK-1686): at most once per
 * {@link #DEFAULT_GAP}, counted from the last check of any kind (startup, Refresh, focus). Free of the JavaFX
 * toolkit; owned by the FX thread.
 */
final class RecheckThrottle {

    static final Duration DEFAULT_GAP = Duration.ofSeconds(10);

    private final Clock clock;
    private final Duration gap;
    private Instant last;

    RecheckThrottle(Clock clock, Duration gap) {
        this.clock = Objects.requireNonNull(clock, "clock");
        this.gap = Objects.requireNonNull(gap, "gap");
        if (gap.isNegative()) {
            throw new IllegalArgumentException("gap must not be negative");
        }
    }

    /** Records that every project is being checked now. */
    void checked() {
        last = clock.instant();
    }

    /**
     * Whether a focus change should check every project again: nothing was checked yet, the gap has passed, or the
     * clock went back (the system time was changed), which must never stop the checks for good.
     */
    boolean dueOnFocus() {
        if (last == null) {
            return true;
        }
        Duration since = Duration.between(last, clock.instant());
        return since.isNegative() || since.compareTo(gap) >= 0;
    }
}
