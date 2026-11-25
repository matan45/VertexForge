package com.vertexforge.launcher.session;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class EditorSessionTest {

    private static final long PID = 77;
    private static final Duration POLL = EditorSession.POLL_INTERVAL;

    @TempDir
    Path tempDir;

    private final ManualScheduler scheduler = new ManualScheduler();
    private final List<String> events = new ArrayList<>();
    private final List<SessionReport> reports = new ArrayList<>();
    private Path log;
    private Path status;
    private Path workingDirectory;
    private FakeProcess process;

    private final EditorSession.Listener listener = new EditorSession.Listener() {
        @Override
        public void phaseChanged(EditorSession session, EditorSession.Phase phase) {
            events.add("phase " + phase);
        }

        @Override
        public void crashReported(EditorSession session, LaunchStatus crash) {
            events.add("crash " + crash.code());
        }

        @Override
        public void exited(EditorSession session, SessionReport report) {
            events.add("exited " + report.outcome().getClass().getSimpleName());
            reports.add(report);
        }
    };

    @BeforeEach
    void setUp() throws IOException {
        SessionLogs.LaunchFiles files = new SessionLogs(tempDir.resolve("logs"), message -> {
        }).create("Game", TestSupport.T0);
        log = files.log();
        status = files.status();
        workingDirectory = Files.createDirectories(tempDir.resolve("engine"));
        process = new FakeProcess(PID, null);
    }

    private EditorSession session(Optional<Path> statusFile, boolean contractKnown, Executor executor) {
        SessionStart start = new SessionStart(process, tempDir.resolve("Game/Game.vfproj"), "Game", log, statusFile,
                statusFile.isPresent() ? Optional.empty() : Optional.of("no status"), workingDirectory, contractKnown,
                List.of("Editor.exe"), TestSupport.T0);
        return new EditorSession(start, scheduler, executor, listener, TestSupport.fixedClock(),
                StandardCharsets.UTF_8);
    }

    private EditorSession begun() {
        EditorSession session = session(Optional.of(status), true, Runnable::run);
        session.begin();
        scheduler.runDue();
        return session;
    }

    private void writeStatus(String state, int code, String message) throws IOException {
        writeStatus(state, code, message, PID);
    }

    private void writeStatus(String state, int code, String message, long pid) throws IOException {
        Files.writeString(status, "{\"state\":\"" + state + "\",\"code\":" + code + ",\"message\":\"" + message
                + "\",\"pid\":" + pid + "}\n");
    }

    /** A VK-1732 line: the crash folder after pid, JSON-escaped. */
    private void writeStatus(String state, int code, String message, Path crashDir) throws IOException {
        Files.writeString(status, "{\"state\":\"" + state + "\",\"code\":" + code + ",\"message\":\"" + message
                + "\",\"pid\":" + PID + ",\"crashDir\":\"" + crashDir.toString().replace("\\", "\\\\") + "\"}\n");
    }

    /** The per-user crash folder of a VK-1732 editor, outside its working directory. */
    private Path userCrashes() throws IOException {
        return Files.createDirectories(tempDir.resolve("profile/.vertexforge/editor/crashes"));
    }

    private void exit(int code) {
        process.exit(code);
        scheduler.runDue();
    }

    @Test
    void loadingThenReadyAreEachReportedOnce() throws IOException {
        begun();
        scheduler.advance(POLL);
        assertEquals(List.of(), events, "nothing written yet");

        writeStatus("loading", 0, "Loading project");
        scheduler.advance(POLL);
        scheduler.advance(POLL);
        writeStatus("ready", 0, "Editor ready");
        scheduler.advance(POLL);
        scheduler.advance(POLL.multipliedBy(10));

        assertEquals(List.of("phase LOADING", "phase READY"), events);
    }

    @Test
    void noStatusAfterTheTimeoutIsNoReadySignalAndALateReadyStillCounts() throws IOException {
        begun();
        scheduler.advance(EditorSession.READY_TIMEOUT.minus(POLL));
        assertEquals(List.of(), events);

        scheduler.advance(POLL);
        assertEquals(List.of("phase NO_READY_SIGNAL"), events);

        writeStatus("ready", 0, "Editor ready");
        scheduler.advance(POLL);
        assertEquals(List.of("phase NO_READY_SIGNAL", "phase READY"), events);
    }

    @Test
    void aLongLoadNeverTimesOut() throws IOException {
        begun();
        writeStatus("loading", 0, "Loading project");
        scheduler.advance(POLL);

        scheduler.advance(Duration.ofMinutes(10));

        assertEquals(List.of("phase LOADING"), events);
    }

    @Test
    void anotherProcessesStatusFileIsNotThisEditors() throws IOException {
        begun();
        writeStatus("ready", 0, "Editor ready", PID + 1);

        scheduler.advance(EditorSession.READY_TIMEOUT);

        assertEquals(List.of("phase NO_READY_SIGNAL"), events);
    }

    /** AC1: the editor writes "failed" and exits at once; the read after the exit still finds the reason. */
    @Test
    void theExitReadsTheStatusOneLastTime() throws IOException {
        begun();
        Files.writeString(log, "Editor.exe: cannot open project Game.vfproj: it does not exist\n",
                StandardOpenOption.APPEND);
        writeStatus("failed", 3, "cannot open project Game.vfproj: it does not exist");

        exit(3);

        assertEquals(List.of("exited Refused"), events);
        SessionReport report = reports.getFirst();
        SessionOutcome.Refused refused = assertInstanceOf(SessionOutcome.Refused.class, report.outcome());
        assertEquals(3, refused.code());
        assertEquals(Optional.of("cannot open project Game.vfproj: it does not exist"), refused.message());
        assertEquals(3, report.exitCode());
        assertEquals(List.of("Editor.exe: cannot open project Game.vfproj: it does not exist"), report.logTail());
        assertEquals(Optional.empty(), report.crashReport());
    }

    @Test
    void aCrashWhileAliveIsReportedOnceAndItsDetailsComeAtTheExit() throws IOException {
        begun();
        writeStatus("ready", 0, "Editor ready");
        scheduler.advance(POLL);
        writeStatus("failed", 0xC0000005, "Editor crashed: ACCESS_VIOLATION");
        scheduler.advance(POLL);
        scheduler.advance(POLL.multipliedBy(4));
        Path crashes = Files.createDirectories(workingDirectory.resolve("crashes"));
        Files.writeString(crashes.resolve("crash_20260102_030500.txt"), "VertexForge crash report");

        exit(0xC0000005);

        assertEquals(List.of("phase READY", "crash " + 0xC0000005, "exited Crashed"), events);
        SessionReport report = reports.getFirst();
        assertEquals("VertexForge crash report", report.crashReport().orElseThrow().content());
        // An editor older than VK-1732 names no folder: its working directory's.
        assertEquals(workingDirectory.resolve("crashes"), report.crashFolder());
    }

    /** VK-1732: a folder named in a status line polled earlier still counts when the last read finds nothing. */
    @Test
    void theCrashFolderComesFromTheStatusLine() throws IOException {
        Path userCrashes = userCrashes();
        begun();
        writeStatus("ready", 0, "Editor ready", userCrashes);
        scheduler.advance(POLL);
        writeStatus("failed", 0xC0000005, "Editor crashed: ACCESS_VIOLATION", userCrashes);
        scheduler.advance(POLL);
        Files.delete(status);
        Path mine = Files.writeString(userCrashes.resolve("crash_20260102_030500_" + PID + ".txt"),
                "VertexForge crash report");
        Files.writeString(userCrashes.resolve("crash_20260102_030501_" + (PID + 1) + ".txt"), "another editor's");
        Path oldFolder = Files.createDirectories(workingDirectory.resolve("crashes"));
        Files.writeString(oldFolder.resolve("crash_20260102_030502.txt"), "not where this editor writes");

        exit(0xC0000005);

        assertEquals(List.of("phase READY", "crash " + 0xC0000005, "exited Crashed"), events);
        SessionReport report = reports.getFirst();
        assertEquals(userCrashes, report.crashFolder());
        assertEquals(mine, report.crashReport().orElseThrow().text());
        assertEquals("VertexForge crash report", report.crashReport().orElseThrow().content());
    }

    /** VK-1732: the read after the exit can be the first to name the folder. */
    @Test
    void theLastLineAloneCanNameTheCrashFolder() throws IOException {
        Path userCrashes = userCrashes();
        begun();
        writeStatus("failed", 0xC0000005, "Editor crashed: ACCESS_VIOLATION", userCrashes);
        Path mine = Files.writeString(userCrashes.resolve("crash_20260102_030500_" + PID + ".txt"),
                "VertexForge crash report");

        exit(0xC0000005);

        assertEquals(List.of("exited Crashed"), events);
        SessionReport report = reports.getFirst();
        assertEquals(userCrashes, report.crashFolder());
        assertEquals(mine, report.crashReport().orElseThrow().text());
    }

    /** Another editor's report in a shared folder is never the session's, even when it is the only one. */
    @Test
    void anotherEditorsCrashReportIsNotTheSessions() throws IOException {
        Path userCrashes = userCrashes();
        begun();
        writeStatus("failed", 0xC0000005, "Editor crashed: ACCESS_VIOLATION", userCrashes);
        Files.writeString(userCrashes.resolve("crash_20260102_030500_" + (PID + 1) + ".txt"), "another editor's");

        exit(0xC0000005);

        SessionReport report = reports.getFirst();
        assertEquals(userCrashes, report.crashFolder());
        assertEquals(Optional.empty(), report.crashReport());
    }

    @Test
    void aNormalCloseGathersNothingAndAppendsTheFooter() throws IOException {
        begun();
        writeStatus("ready", 0, "Editor ready");
        scheduler.advance(POLL);

        exit(0);

        assertEquals(List.of("phase READY", "exited Normal"), events);
        SessionReport report = reports.getFirst();
        assertEquals(List.of(), report.logTail());
        String text = Files.readString(log);
        assertTrue(text.endsWith("VertexForge Launcher: the editor exited with code 0: the editor closed normally at "
                + TestSupport.T0 + " after 0 s.\r\n"), text);
    }

    @Test
    void nothingHappensAfterTheExit() throws IOException {
        begun();
        exit(0);
        writeStatus("ready", 0, "late");

        scheduler.advance(Duration.ofMinutes(1));
        process.exit(0);
        scheduler.runDue();

        assertEquals(List.of("exited NoReadySignal"), events, "exited exactly once, last");
    }

    /** AC5: an editor older than VK-1682 never writes the status file and exits 0 even after a failed load. */
    @Test
    void anOldEditorIsNoReadySignalNotASuccess() {
        EditorSession session = session(Optional.of(status), false, Runnable::run);
        session.begin();
        scheduler.runDue();

        scheduler.advance(EditorSession.READY_TIMEOUT);
        exit(0);

        assertEquals(List.of("phase NO_READY_SIGNAL", "exited NoReadySignal"), events);
    }

    @Test
    void withoutAStatusFileNothingIsPolledAndACleanExitIsNormal() {
        EditorSession session = session(Optional.empty(), true, Runnable::run);
        session.begin();
        scheduler.runDue();
        assertEquals(1, scheduler.pending(), "only the ready timeout");

        scheduler.advance(EditorSession.READY_TIMEOUT);
        exit(0);

        assertEquals(List.of("phase NO_READY_SIGNAL", "exited Normal"), events);
    }

    @Test
    void anEditorThatExitedBeforeTheSessionBeganIsReportedOnce() throws IOException {
        writeStatus("failed", 4, "the project was saved by a newer VertexForge");
        process.exit(4);

        EditorSession session = session(Optional.of(status), true, Runnable::run);
        session.begin();
        scheduler.runDue();
        scheduler.advance(EditorSession.READY_TIMEOUT);

        assertEquals(List.of("exited Refused"), events);
    }

    @Test
    void aWindowThatIsGoneNeverBreaksTheSession() {
        Executor closed = runnable -> {
            throw new RejectedExecutionException("closed");
        };
        EditorSession session = session(Optional.of(status), true, closed);

        assertDoesNotThrow(() -> {
            session.begin();
            scheduler.runDue();
            scheduler.advance(EditorSession.READY_TIMEOUT);
            exit(1);
        });
    }

    @Test
    void durationsReadLikeAPersonWouldSayThem() {
        assertEquals("42 s", EditorSession.formatDuration(Duration.ofSeconds(42)));
        assertEquals("3 min 5 s", EditorSession.formatDuration(Duration.ofSeconds(185)));
        assertEquals("2 h 1 min", EditorSession.formatDuration(Duration.ofMinutes(121)));
        assertEquals("0 s", EditorSession.formatDuration(Duration.ofSeconds(-3)));
    }
}
