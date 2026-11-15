package com.vertexforge.launcher.session;

import com.vertexforge.launcher.session.LaunchStatus.State;
import org.junit.jupiter.api.Test;

import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SessionOutcomeTest {

    private static Optional<LaunchStatus> status(State state, int code, String message) {
        return Optional.of(new LaunchStatus(state, code, message, 7));
    }

    private static SessionOutcome classify(int exitCode, Optional<LaunchStatus> last) {
        return SessionOutcome.classify(exitCode, last, true, true);
    }

    @Test
    void exitCodeZeroIsANormalCloseWhateverTheFileSays() {
        assertInstanceOf(SessionOutcome.Normal.class, classify(0, status(State.READY, 0, "")));
        assertInstanceOf(SessionOutcome.Normal.class, classify(0, status(State.LOADING, 0, "")),
                "the window was closed while the project loaded");
        assertInstanceOf(SessionOutcome.Normal.class, classify(0, status(State.FAILED, 3, "x")));
        assertFalse(classify(0, status(State.READY, 0, "")).showsFailureDialog());
    }

    /** AC5: an editor older than VK-1682 exits 0 even on a bad project and writes no status. */
    @Test
    void exitCodeZeroWithoutAnyStatusIsNoReadySignal() {
        SessionOutcome outcome = SessionOutcome.classify(0, Optional.empty(), true, false);

        assertInstanceOf(SessionOutcome.NoReadySignal.class, outcome);
        assertFalse(outcome.showsFailureDialog());
        assertTrue(outcome.headline("Game").contains("without ever reporting that it was ready"),
                outcome.headline("Game"));
        // Without a status file there was nothing to wait for.
        assertInstanceOf(SessionOutcome.Normal.class, SessionOutcome.classify(0, Optional.empty(), false, false));
    }

    /** AC1: a deleted .vfproj gives code 3 with the editor's reason. */
    @Test
    void aRefusalCarriesTheCodeAndTheEditorsReason() {
        SessionOutcome outcome = classify(3, status(State.FAILED, 3, "cannot open project C:\\G\\G.vfproj: it does"
                + " not exist"));

        SessionOutcome.Refused refused = assertInstanceOf(SessionOutcome.Refused.class, outcome);
        assertEquals(3, refused.code());
        assertTrue(outcome.showsFailureDialog());
        assertTrue(outcome.details().contains("code 3: the project failed to load"), outcome.details());
        assertTrue(outcome.details().contains("it does not exist"), outcome.details());
        assertEquals("The editor could not open \"G\".", outcome.headline("G"));
    }

    @Test
    void aProjectOpenElsewhereIsABannerNotAFailure() {
        SessionOutcome outcome = classify(5, status(State.FAILED, 5, "held by process 99"));

        assertInstanceOf(SessionOutcome.Refused.class, outcome);
        assertFalse(outcome.showsFailureDialog());
        assertTrue(outcome.headline("G").contains("already open in another editor: held by process 99"),
                outcome.headline("G"));
    }

    @Test
    void aContractCodeWithoutAStatusCountsOnlyForAKnownContract() {
        assertInstanceOf(SessionOutcome.Refused.class, SessionOutcome.classify(4, Optional.empty(), false, true));
        assertInstanceOf(SessionOutcome.Died.class, SessionOutcome.classify(4, Optional.empty(), false, false));
    }

    @Test
    void anUnknownConsoleFlagGetsAHint() {
        SessionOutcome outcome = classify(2, status(State.FAILED, 2, "bad arguments: unknown argument --console"));

        assertTrue(outcome.details().contains("turn off \"Show editor console\""), outcome.details());
        assertFalse(classify(2, status(State.FAILED, 2, "bad arguments: --mcp-port 1")).details()
                .contains("Show editor console"));
    }

    @Test
    void aCrashReportedThroughTheStatusFileIsACrash() {
        SessionOutcome outcome = classify(0xC0000005, status(State.FAILED, 0xC0000005,
                "Editor crashed: ACCESS_VIOLATION (0xc0000005); crash dump crashes/crash_1.dmp"));

        SessionOutcome.Crashed crashed = assertInstanceOf(SessionOutcome.Crashed.class, outcome);
        assertEquals(0xC0000005, crashed.code());
        assertTrue(outcome.showsFailureDialog());
        assertTrue(outcome.details().contains("0xC0000005"), outcome.details());
    }

    /** A Debug build's CRT assertion dialog ends the editor with 3; the status file tells it apart. */
    @Test
    void aDebugAbortThatExitsWithThreeIsACrash() {
        assertInstanceOf(SessionOutcome.Crashed.class, classify(3, status(State.FAILED, 0xC0000409,
                "Editor aborted (abort(), std::terminate or a failed assertion)")));
    }

    /** A crashed editor's console closed behind its crash box still reports the crash. */
    @Test
    void aCrashWinsOverAClosedConsole() {
        assertInstanceOf(SessionOutcome.Crashed.class, classify(SessionOutcome.CONSOLE_CLOSED_CODE,
                status(State.FAILED, 0xC0000005, "Editor crashed")));
    }

    @Test
    void closingTheConsoleIsItsOwnOutcome() {
        SessionOutcome outcome = classify(SessionOutcome.CONSOLE_CLOSED_CODE, status(State.READY, 0, ""));

        assertInstanceOf(SessionOutcome.ConsoleClosed.class, outcome);
        assertFalse(outcome.showsFailureDialog());
        assertEquals(-1073741510, SessionOutcome.CONSOLE_CLOSED_CODE);
    }

    @Test
    void aNonZeroExitWithoutAReasonDied() {
        SessionOutcome killed = classify(1, status(State.READY, 0, ""));
        SessionOutcome assertion = classify(-1, status(State.LOADING, 0, ""));
        SessionOutcome silent = SessionOutcome.classify(0xC0000005, Optional.empty(), false, false);

        assertEquals(new SessionOutcome.Died(1, Optional.of(State.READY)), killed);
        assertTrue(killed.details().contains("after it was ready"), killed.details());
        assertTrue(assertion.details().contains("while it was still loading"), assertion.details());
        assertEquals(new SessionOutcome.Died(0xC0000005, Optional.empty()), silent);
        assertTrue(silent.showsFailureDialog());
    }
}
