package com.vertexforge.launcher.session;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class EditorSessionsTest {

    @TempDir
    Path tempDir;

    private final EditorSession.Listener ignore = new EditorSession.Listener() {
        @Override
        public void phaseChanged(EditorSession session, EditorSession.Phase phase) {
        }

        @Override
        public void crashReported(EditorSession session, LaunchStatus status) {
        }

        @Override
        public void exited(EditorSession session, SessionReport report) {
        }
    };

    private EditorSession session(Path project) {
        SessionStart start = new SessionStart(FakeProcess.running(), project, "Game", tempDir.resolve("a.log"),
                Optional.empty(), Optional.of("test"), tempDir, true, List.of("Editor.exe"), TestSupport.T0);
        return new EditorSession(start, new ManualScheduler(), Runnable::run, ignore, TestSupport.fixedClock(),
                StandardCharsets.UTF_8);
    }

    /** AC3: one session per project, however its path is spelled. */
    @Test
    void aProjectHasAtMostOneLiveSession() {
        EditorSessions sessions = new EditorSessions();
        Path project = tempDir.resolve("Game/Game.vfproj");
        EditorSession first = session(project);

        sessions.add(first, McpSetting.DEFAULT);

        assertSame(first, sessions.find(tempDir.resolve("Game/../Game/Game.vfproj")).orElseThrow().session());
        assertThrows(IllegalStateException.class, () -> sessions.add(session(project), McpSetting.DEFAULT));
        assertEquals(1, sessions.liveCount());
    }

    @Test
    void windowsPathsMatchWhateverTheirCase() {
        assumeTrue(TestSupport.isWindows(), "Windows paths ignore case");
        EditorSessions sessions = new EditorSessions();
        sessions.add(session(tempDir.resolve("Game/Game.vfproj")), McpSetting.DEFAULT);

        assertTrue(sessions.find(Path.of(tempDir.toString().toUpperCase(Locale.ROOT), "GAME",
                "GAME.VFPROJ")).isPresent());
    }

    @Test
    void anEndedSessionFreesItsProject() {
        EditorSessions sessions = new EditorSessions();
        Path project = tempDir.resolve("Game/Game.vfproj");
        EditorSession first = session(project);
        sessions.add(first, McpSetting.DEFAULT);

        sessions.remove(first);

        assertEquals(Optional.empty(), sessions.find(project));
        assertEquals(0, sessions.liveCount());
        sessions.add(session(project), McpSetting.DEFAULT);
        assertEquals(1, sessions.liveCount());
    }

    @Test
    void phasesTellWhetherAnySessionMayStillFail() {
        EditorSessions sessions = new EditorSessions();
        EditorSession first = session(tempDir.resolve("A/A.vfproj"));
        EditorSession second = session(tempDir.resolve("B/B.vfproj"));
        sessions.add(first, McpSetting.DEFAULT);
        sessions.add(second, McpSetting.DEFAULT);
        assertTrue(sessions.anyBeforeReady(), "both start in STARTING");

        sessions.setPhase(first, EditorSession.Phase.READY);
        sessions.setPhase(second, EditorSession.Phase.NO_READY_SIGNAL);
        assertTrue(sessions.anyBeforeReady(), "no ready signal may still end in a failure");

        sessions.setPhase(second, EditorSession.Phase.READY);
        assertFalse(sessions.anyBeforeReady());
        assertEquals(EditorSession.Phase.READY, sessions.find(first).orElseThrow().phase());
    }

    @Test
    void theMcpPortOfARunningEditorIsFound() {
        EditorSessions sessions = new EditorSessions();
        sessions.add(session(tempDir.resolve("A/A.vfproj")), new McpSetting(true, 7878));
        sessions.add(session(tempDir.resolve("B/B.vfproj")), new McpSetting(false, 7979));

        assertEquals(tempDir.resolve("A/A.vfproj").toAbsolutePath().normalize(),
                sessions.usingMcpPort(7878).orElseThrow().project());
        assertEquals(Optional.empty(), sessions.usingMcpPort(7979), "MCP is off there");
    }

    /** VK-1686: the editor locks its whole folder, so another project file there is taken too. */
    @Test
    void aSessionIsFoundForEveryProjectFileOfItsFolder() {
        EditorSessions sessions = new EditorSessions();
        EditorSession running = session(tempDir.resolve("Game/Game.vfproj"));
        sessions.add(running, McpSetting.DEFAULT);

        assertSame(running, sessions.findInFolder(tempDir.resolve("Game/Other.vfproj")).orElseThrow().session());
        assertSame(running, sessions.findInFolder(tempDir.resolve("Game/./Game.vfproj")).orElseThrow().session());
        assertTrue(sessions.findInFolder(tempDir.resolve("Elsewhere/Game.vfproj")).isEmpty());
        assertTrue(sessions.findInFolder(tempDir.resolve("Game/Sub/Game.vfproj")).isEmpty());
        assertTrue(sessions.findInFolder(null).isEmpty());
    }
}
