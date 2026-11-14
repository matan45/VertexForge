package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.ProcessStarter;
import com.vertexforge.launcher.service.EditorLauncher.LaunchResult;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class EditorLauncherTest {

    @TempDir
    Path tempDir;

    /** A process that is either still running or already exited with a code. */
    static final class FakeProcess extends Process {
        private final Integer exitCode;

        FakeProcess(Integer exitCode) {
            this.exitCode = exitCode;
        }

        static FakeProcess running() {
            return new FakeProcess(null);
        }

        static FakeProcess exited(int code) {
            return new FakeProcess(code);
        }

        @Override
        public OutputStream getOutputStream() {
            return OutputStream.nullOutputStream();
        }

        @Override
        public InputStream getInputStream() {
            return new ByteArrayInputStream(new byte[0]);
        }

        @Override
        public InputStream getErrorStream() {
            return new ByteArrayInputStream(new byte[0]);
        }

        @Override
        public int waitFor() {
            if (exitCode == null) {
                throw new UnsupportedOperationException("a running fake process never exits");
            }
            return exitCode;
        }

        @Override
        public boolean isAlive() {
            return exitCode == null;
        }

        @Override
        public int exitValue() {
            if (exitCode == null) {
                throw new IllegalThreadStateException("process has not exited");
            }
            return exitCode;
        }

        @Override
        public void destroy() {
        }
    }

    /** Records every builder it is given and answers with a fixed process (or failure). */
    static final class RecordingStarter implements ProcessStarter {
        final List<ProcessBuilder> started = new ArrayList<>();
        private final Process process;
        private final IOException failure;

        RecordingStarter(Process process, IOException failure) {
            this.process = process;
            this.failure = failure;
        }

        @Override
        public Process start(ProcessBuilder builder) throws IOException {
            started.add(builder);
            if (failure != null) {
                throw failure;
            }
            return process;
        }
    }

    private Path editor() throws IOException {
        Path editor = Files.createDirectories(tempDir.resolve("bin")).resolve("Editor.exe");
        Files.writeString(editor, "fake");
        editor.toFile().setExecutable(true);
        return editor;
    }

    private Path project() throws IOException {
        return Files.writeString(tempDir.resolve("Game.vfproj"), "{}");
    }

    /** Launches in the editor's own folder (an install without engine.json). */
    private static LaunchResult launch(RecordingStarter starter, Path editor, Path project) {
        return launch(starter, editor, editor.getParent(), project);
    }

    private static LaunchResult launch(RecordingStarter starter, Path editor, Path workingDirectory, Path project) {
        return new EditorLauncher(starter, Duration.ZERO).launchAsync(editor, workingDirectory, project).join();
    }

    @Test
    void aMissingEditorFailsWithoutStartingAnything() throws IOException {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path missing = tempDir.resolve("bin/Editor.exe");

        LaunchResult result = launch(starter, missing, project());

        assertEquals(LaunchResult.fail("Editor not found: " + missing), result);
        assertTrue(starter.started.isEmpty());
    }

    @Test
    void aMissingProjectFailsWithoutStartingAnything() throws IOException {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path missing = tempDir.resolve("Gone.vfproj");

        LaunchResult result = launch(starter, editor(), missing);

        assertEquals(LaunchResult.fail("Project file not found: " + missing), result);
        assertTrue(starter.started.isEmpty());
    }

    @Test
    void aRunningEditorIsASuccess() throws IOException {
        assertEquals(LaunchResult.ok(),
                launch(new RecordingStarter(FakeProcess.running(), null), editor(), project()));
    }

    @Test
    void anEditorThatExitedCleanlyIsASuccess() throws IOException {
        assertEquals(LaunchResult.ok(),
                launch(new RecordingStarter(FakeProcess.exited(0), null), editor(), project()));
    }

    @Test
    void anEditorThatExitedWithAnErrorFails() throws IOException {
        assertEquals(LaunchResult.fail("Editor exited with code 3: the project failed to load"),
                launch(new RecordingStarter(FakeProcess.exited(3), null), editor(), project()));
    }

    @Test
    void aStartFailureIsReported() throws IOException {
        RecordingStarter starter = new RecordingStarter(null, new IOException("access denied"));

        assertEquals(LaunchResult.fail("Failed to start editor: access denied"),
                launch(starter, editor(), project()));
    }

    @Test
    void aFolderNamedLikeTheEditorIsNotAnEditor() throws IOException {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path folder = Files.createDirectories(tempDir.resolve("bin/Editor.exe"));

        assertEquals(LaunchResult.fail("Editor not found: " + folder), launch(starter, folder, project()));
        assertTrue(starter.started.isEmpty());
    }

    @Test
    void aMissingWorkingDirectoryFailsWithoutStartingAnything() throws IOException {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path missing = tempDir.resolve("VFEngine/editor");

        LaunchResult result = launch(starter, editor(), missing, project());

        assertEquals(LaunchResult.fail("Working directory not found: " + missing), result);
        assertTrue(starter.started.isEmpty());
    }

    /** VK-1681: a dev build starts in the folder its engine.json names, not in its own (it loads its shaders there). */
    @Test
    void startsTheEditorWithTheProjectInTheGivenWorkingDirectory() throws IOException {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path editor = editor();
        Path workingDirectory = Files.createDirectories(tempDir.resolve("VFEngine/editor"));
        Path project = project();

        launch(starter, editor, workingDirectory, project);

        assertEquals(1, starter.started.size());
        ProcessBuilder builder = starter.started.getFirst();
        assertEquals(List.of(editor.toString(), project.toString()), builder.command());
        assertEquals(workingDirectory.toFile(), builder.directory());
        assertEquals(ProcessBuilder.Redirect.INHERIT, builder.redirectInput());
        assertEquals(ProcessBuilder.Redirect.INHERIT, builder.redirectOutput());
        assertEquals(ProcessBuilder.Redirect.INHERIT, builder.redirectError());
    }

    @Test
    void theDefaultCrashWindowIsHalfASecond() {
        assertEquals(Duration.ofMillis(500), EditorLauncher.DEFAULT_CRASH_WINDOW);
    }
}
