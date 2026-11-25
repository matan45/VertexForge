package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The focus re-check's throttle (VK-1686). */
class RecheckThrottleTest {

    /** A clock the test moves by hand. */
    private static final class SettableClock extends Clock {
        private Instant now = TestSupport.T0;

        void advance(Duration duration) {
            now = now.plus(duration);
        }

        @Override
        public ZoneId getZone() {
            return ZoneOffset.UTC;
        }

        @Override
        public Clock withZone(ZoneId zone) {
            return this;
        }

        @Override
        public Instant instant() {
            return now;
        }
    }

    private final SettableClock clock = new SettableClock();
    private final RecheckThrottle throttle = new RecheckThrottle(clock, Duration.ofSeconds(10));

    @Test
    void theFirstFocusChecks() {
        assertTrue(throttle.dueOnFocus());
    }

    @Test
    void aFocusRightAfterACheckDoesNotCheckAgain() {
        throttle.checked();
        clock.advance(Duration.ofSeconds(9));

        assertFalse(throttle.dueOnFocus());
    }

    @Test
    void aFocusOnceTheGapHasPassedChecks() {
        throttle.checked();
        clock.advance(Duration.ofSeconds(10));

        assertTrue(throttle.dueOnFocus());
    }

    @Test
    void aClockThatWentBackNeverStopsTheChecks() {
        throttle.checked();
        clock.advance(Duration.ofHours(-1));

        assertTrue(throttle.dueOnFocus());
    }

    @Test
    void aNegativeGapIsRefused() {
        assertThrows(IllegalArgumentException.class, () -> new RecheckThrottle(clock, Duration.ofSeconds(-1)));
    }
}
