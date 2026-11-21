package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.env.ProcessStarter;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.session.LaunchRequest;
import com.vertexforge.launcher.session.ManualScheduler;
import com.vertexforge.launcher.session.SessionLogs;
import com.vertexforge.launcher.session.SessionStart;
import com.vertexforge.launcher.session.StartResult;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Public only so the session tests can share its fakes ({@link FakeProcess}, {@link RecordingStarter}). */
public class EditorLauncherTest {

    @TempDir
    Path tempDir;

    /** A process that runs until the test ends it; its exit completes {@link #onExit()}. */
    public static final class FakeProcess extends Process {
        private final CompletableFuture<Process> exit = new CompletableFuture<>();
        private final long pid;
        private volatile Integer exitCode;
        private volatile boolean inputClosed;

        public FakeProcess(long pid, Integer exitCode) {
            this.pid = pid;
            if (exitCode != null) {
                this.exitCode = exitCode;
                exit.complete(this);
            }
        }

        public static FakeProcess running() {
            return new FakeProcess(4242, null);
        }

        public static FakeProcess exited(int code) {
            return new FakeProcess(4242, code);
        }

        /** Ends the process with {@code code}. */
        public void exit(int code) {
            exitCode = code;
            exit.complete(this);
        }

        public boolean inputClosed() {
            return inputClosed;
        }

        @Override
        public OutputStream getOutputStream() {
            return new OutputStream() {
                @Override
                public void write(int b) {
                }

                @Override
                public void close() {
                    inputClosed = true;
                }
            };
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
            exit.join();
            return exitCode;
        }

        @Override
        public CompletableFuture<Process> onExit() {
            return exit;
        }

        @Override
        public long pid() {
            return pid;
        }

        @Override
        public boolean isAlive() {
            return exitCode == null;
        }

        @Override
        public int exitValue() {
            Integer code = exitCode;
            if (code == null) {
                throw new IllegalThreadStateException("process has not exited");
            }
            return code;
        }

        @Override
        public void destroy() {
        }
    }

    /** Records every builder it is given (and the log's content at that moment) and answers with a fixed process. */
    public static final class RecordingStarter implements ProcessStarter {
        public final List<ProcessBuilder> started = new ArrayList<>();
        public final List<String> logAtStart = new ArrayList<>();
        private final Process process;
        private final IOException failure;

        public RecordingStarter(Process process, IOException failure) {
            this.process = process;
            this.failure = failure;
        }

        @Override
        public Process start(ProcessBuilder builder) throws IOException {
            started.add(builder);
            ProcessBuilder.Redirect output = builder.redirectOutput();
            // Only the launch log (DISCARD's file is the NUL device).
            if (output.type() == ProcessBuilder.Redirect.Type.APPEND && Files.exists(output.file().toPath())) {
                logAtStart.add(Files.readString(output.file().toPath(), StandardCharsets.UTF_8));
            }
            if (failure != null) {
                throw failure;
            }
            return process;
        }
    }

    private static final String MANIFEST = """
            {"manifestVersion": 1, "engineVersion": "1.0.0", "configuration": "Development",
             "editorExe": "Editor.exe", "workingDirectory": "."%s}
            """;

    private Path logsDir() {
        return tempDir.resolve("config/logs");
    }

    private Path editor() {
        return TestSupport.fakeEditor(tempDir.resolve("bin"));
    }

    private Path project() throws IOException {
        return Files.writeString(tempDir.resolve("Game.vfproj"), "{}");
    }

    /** An OK engine: its editor, its working directory and, when {@code contract} is given, a manifest with it. */
    private static EngineOption engine(Path editor, Path workingDirectory, Integer contract) throws Exception {
        EngineManifest manifest = contract == null ? null
                : EngineManifest.parse(MANIFEST.formatted(", \"launchContract\": " + contract));
        return new EngineOption(Optional.empty(), editor, editor.toString(), EngineHealth.ok(manifest, workingDirectory),
                false, false, false);
    }

    private EditorLauncher launcher(RecordingStarter starter, Charset charset) {
        return new EditorLauncher(starter, new SessionLogs(logsDir(), message -> {
        }), charset, TestSupport.fixedClock(), new ManualScheduler());
    }

    private StartResult start(RecordingStarter starter, EngineOption engine, Path project, McpSetting mcp,
                              boolean console) {
        return launcher(starter, StandardCharsets.UTF_8)
                .startNow(new LaunchRequest(engine, project, "Game", mcp, console));
    }

    private static SessionStart started(StartResult result) {
        return assertInstanceOf(StartResult.Started.class, result, result::toString).start();
    }

    @Test
    void aMissingEditorFailsWithoutStartingAnything() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path missing = tempDir.resolve("bin/Editor.exe");

        StartResult result = start(starter, engine(missing, tempDir, null), project(), McpSetting.DEFAULT, false);

        assertEquals(new StartResult.Failed("Editor not found: " + missing), result);
        assertTrue(starter.started.isEmpty());
    }

    @Test
    void aFolderNamedLikeTheEditorIsNotAnEditor() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path folder = Files.createDirectories(tempDir.resolve("bin/Editor.exe"));

        StartResult result = start(starter, engine(folder, tempDir, null), project(), McpSetting.DEFAULT, false);

        assertEquals(new StartResult.Failed("Editor not found: " + folder), result);
        assertTrue(starter.started.isEmpty());
    }

    @Test
    void aMissingWorkingDirectoryFailsWithoutStartingAnything() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path missing = tempDir.resolve("VFEngine/editor");

        StartResult result = start(starter, engine(editor(), missing, null), project(), McpSetting.DEFAULT, false);

        assertEquals(new StartResult.Failed("Working directory not found: " + missing), result);
        assertTrue(starter.started.isEmpty());
    }

    @Test
    void anEngineThatIsNotOkIsRefused() {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        EngineOption broken = new EngineOption(Optional.empty(), tempDir.resolve("Editor.exe"), "x",
                EngineHealth.broken("the editor is gone"), false, false, false);

        StartResult result = start(starter, broken, tempDir.resolve("Game.vfproj"), McpSetting.DEFAULT, false);

        assertEquals(new StartResult.Failed("The engine cannot be used: the editor is gone"), result);
        assertTrue(starter.started.isEmpty());
    }

    /** An editor without a launch contract would open an empty editor and exit 0, so it is never started. */
    @Test
    void aMissingProjectIsRefusedForAnEngineWithoutAContract() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path missing = tempDir.resolve("Gone.vfproj");

        StartResult result = start(starter, engine(editor(), tempDir, null), missing, McpSetting.DEFAULT, false);

        assertEquals(new StartResult.Failed("Project file not found: " + missing), result);
        assertTrue(starter.started.isEmpty());
    }

    /** AC1: an editor with a launch contract reports the missing project itself (exit code 3, with its reason). */
    @Test
    void aMissingProjectStillStartsAnEditorWithAContract() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path missing = tempDir.resolve("Gone.vfproj");

        SessionStart start = started(start(starter, engine(editor(), tempDir, 1), missing, McpSetting.DEFAULT, false));

        assertEquals(missing, start.project());
        assertTrue(start.contractKnown());
        assertEquals(1, starter.started.size());
    }

    /** The project first: editors older than the contract take the first argument as the project. */
    @Test
    void startsTheEditorWithTheProjectFirstAndItsStatusFile() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path editor = editor();
        Path workingDirectory = Files.createDirectories(tempDir.resolve("VFEngine/editor"));
        Path project = project();

        SessionStart start = started(start(starter, engine(editor, workingDirectory, null), project,
                McpSetting.DEFAULT, true));

        Path status = start.statusFile().orElseThrow();
        assertTrue(status.isAbsolute());
        assertEquals(logsDir().toAbsolutePath().normalize(), status.getParent());
        assertTrue(status.getFileName().toString().endsWith(SessionLogs.STATUS_SUFFIX), status.toString());
        assertFalse(Files.exists(status), "the editor must create the status file itself");
        ProcessBuilder builder = starter.started.getFirst();
        assertEquals(List.of(editor.toString(), project.toString(), "--launch-status", status.toString()),
                builder.command(), "no --console for an engine without a contract");
        assertEquals(workingDirectory.toFile(), builder.directory());
        assertEquals(workingDirectory, start.workingDirectory());
        assertFalse(start.contractKnown());
    }

    @Test
    void theOutputGoesToANewLogWithTheHeaderWrittenFirst() throws Exception {
        FakeProcess process = FakeProcess.running();
        RecordingStarter starter = new RecordingStarter(process, null);
        Path project = project();

        SessionStart start = started(start(starter, engine(editor(), tempDir, 1), project, McpSetting.DEFAULT,
                false));

        ProcessBuilder builder = starter.started.getFirst();
        assertTrue(builder.redirectErrorStream(), "stderr goes to the same log as stdout");
        assertEquals(ProcessBuilder.Redirect.appendTo(start.log().toFile()), builder.redirectOutput());
        assertEquals(ProcessBuilder.Redirect.PIPE, builder.redirectInput());
        assertTrue(process.inputClosed(), "the editor's input is closed at once");
        String header = starter.logAtStart.getFirst();
        assertTrue(header.startsWith("VertexForge Launcher: opening " + project + "\r\n"), header);
        assertTrue(header.contains("Command line: "), header);
        assertTrue(header.contains("Started: " + TestSupport.T0), header);
        assertTrue(header.endsWith("----\r\n"), header);
        assertTrue(start.log().getFileName().toString().startsWith("Game-20260102T030405000Z"),
                start.log().toString());
        assertEquals(TestSupport.T0, start.startedAt());
    }

    @Test
    void mcpAddsItsFlagsButNeverAToken() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);

        started(start(starter, engine(editor(), tempDir, 1), project(), new McpSetting(true, 7979), false));

        List<String> command = starter.started.getFirst().command();
        assertEquals(List.of("--mcp", "--mcp-port", "7979"), command.subList(4, 7));
        assertFalse(command.contains("--mcp-token"));
        assertFalse(command.contains("--console"));
    }

    @Test
    void aDisabledMcpSettingAddsNothing() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);

        started(start(starter, engine(editor(), tempDir, 1), project(), new McpSetting(false, 7979), false));

        assertEquals(4, starter.started.getFirst().command().size());
    }

    @Test
    void theConsoleFlagGoesOnlyToAnEditorWhoseContractHasIt() throws Exception {
        Path editor = editor();
        Path project = project();

        RecordingStarter withContract = new RecordingStarter(FakeProcess.running(), null);
        started(start(withContract, engine(editor, tempDir, 1), project, new McpSetting(true, 7878), true));
        List<String> command = withContract.started.getFirst().command();
        assertEquals("--console", command.getLast());
        assertEquals(List.of("--mcp", "--mcp-port", "7878", "--console"), command.subList(4, 8));

        RecordingStarter newer = new RecordingStarter(FakeProcess.running(), null);
        started(start(newer, engine(editor, tempDir, 7), project, McpSetting.DEFAULT, true));
        assertEquals("--console", newer.started.getFirst().command().getLast());

        RecordingStarter withoutContract = new RecordingStarter(FakeProcess.running(), null);
        started(start(withoutContract, engine(editor, tempDir, null), project, McpSetting.DEFAULT, true));
        assertFalse(withoutContract.started.getFirst().command().contains("--console"));

        RecordingStarter turnedOff = new RecordingStarter(FakeProcess.running(), null);
        started(start(turnedOff, engine(editor, tempDir, 1), project, McpSetting.DEFAULT, false));
        assertFalse(turnedOff.started.getFirst().command().contains("--console"));
    }

    /**
     * An editor older than VK-1684 would receive a status path the code page cannot encode as "?", and exit 2.
     */
    @Test
    void aStatusPathTheCodePageCannotEncodeIsLeftOut() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path editor = editor();
        Path project = project();
        EditorLauncher launcher = new EditorLauncher(starter, new SessionLogs(tempDir.resolve("lögs"), message -> {
        }), StandardCharsets.US_ASCII, TestSupport.fixedClock(), new ManualScheduler());

        SessionStart start = started(launcher.startNow(new LaunchRequest(engine(editor, tempDir, 1), project, "Game",
                McpSetting.DEFAULT, false)));

        assertEquals(List.of(editor.toString(), project.toString()), starter.started.getFirst().command());
        assertTrue(start.statusFile().isEmpty());
        assertTrue(start.statusOmitted().orElseThrow().contains("code page"), start.statusOmitted().toString());
        assertTrue(starter.logAtStart.getFirst().contains("No launch status: "), starter.logAtStart.getFirst());
    }

    /** VK-1684: an editor of contract 2 runs in the UTF-8 code page, so it gets any status path. */
    @Test
    void anEditorThatTakesUnicodeArgumentsGetsAnyStatusPath() throws Exception {
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);
        Path editor = editor();
        Path project = project();
        EditorLauncher launcher = new EditorLauncher(starter, new SessionLogs(tempDir.resolve("lögs"), message -> {
        }), StandardCharsets.US_ASCII, TestSupport.fixedClock(), new ManualScheduler());

        SessionStart start = started(launcher.startNow(new LaunchRequest(
                engine(editor, tempDir, EngineManifest.UNICODE_ARGUMENTS_CONTRACT), project, "Game",
                McpSetting.DEFAULT, false)));

        Path status = start.statusFile().orElseThrow();
        assertTrue(status.toString().contains("lögs"), status.toString());
        assertEquals(List.of(editor.toString(), project.toString(), "--launch-status", status.toString()),
                starter.started.getFirst().command());
        assertTrue(start.statusOmitted().isEmpty(), start.statusOmitted().toString());
        assertFalse(starter.logAtStart.getFirst().contains("No launch status"), starter.logAtStart.getFirst());
    }

    @Test
    void aStartFailureIsReportedAndLoggedInTheLaunchLog() throws Exception {
        RecordingStarter starter = new RecordingStarter(null, new IOException("access denied"));

        StartResult result = start(starter, engine(editor(), tempDir, 1), project(), McpSetting.DEFAULT, false);

        assertEquals(new StartResult.Failed("Failed to start editor: access denied"), result);
        Path log = starter.started.getFirst().redirectOutput().file().toPath();
        assertTrue(Files.readString(log).endsWith("the editor could not be started: access denied\r\n"),
                Files.readString(log));
    }

    @Test
    void eachLaunchPrunesTheLogsToTheNewestTwenty() throws Exception {
        Path logs = Files.createDirectories(logsDir());
        for (int i = 0; i < 25; i++) {
            Path old = Files.writeString(logs.resolve("Old-" + i + SessionLogs.LOG_SUFFIX), "old");
            Files.setLastModifiedTime(old, FileTime.fromMillis(1_000_000L + i));
        }
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);

        SessionStart start = started(start(starter, engine(editor(), tempDir, 1), project(), McpSetting.DEFAULT,
                false));

        List<String> left = TestSupport.fileNames(logs).stream().filter(name -> name.endsWith(".log")).toList();
        assertEquals(SessionLogs.KEEP, left.size(), left.toString());
        assertTrue(left.contains(start.log().getFileName().toString()));
        assertFalse(left.contains("Old-0.log"), "the oldest went first");
    }

    /** VK-1694: the Settings dialog's log retention decides how many are kept, the new log included. */
    @Test
    void aLaunchKeepsAsManyLogsAsTheRequestAsks() throws Exception {
        Path logs = Files.createDirectories(logsDir());
        for (int i = 0; i < 6; i++) {
            Path old = Files.writeString(logs.resolve("Old-" + i + SessionLogs.LOG_SUFFIX), "old");
            Files.setLastModifiedTime(old, FileTime.fromMillis(1_000_000L + i));
        }
        RecordingStarter starter = new RecordingStarter(FakeProcess.running(), null);

        SessionStart start = started(launcher(starter, StandardCharsets.UTF_8).startNow(new LaunchRequest(
                engine(editor(), tempDir, 1), project(), "Game", McpSetting.DEFAULT, false, 3)));

        List<String> left = TestSupport.fileNames(logs).stream().filter(name -> name.endsWith(".log")).toList();
        assertEquals(3, left.size(), left.toString());
        assertTrue(left.contains(start.log().getFileName().toString()), "the new log is always kept");
        assertTrue(left.containsAll(List.of("Old-5.log", "Old-4.log")), left.toString());
        assertThrows(IllegalArgumentException.class, () -> new LaunchRequest(engine(editor(), tempDir, 1),
                project(), "Game", McpSetting.DEFAULT, false, 0));
    }

    @Test
    void theDisplayedCommandQuotesArgumentsWithSpaces() {
        assertEquals("C:\\a\\Editor.exe \"C:\\My Games\\G.vfproj\" --mcp",
                EditorLauncher.displayCommand(List.of("C:\\a\\Editor.exe", "C:\\My Games\\G.vfproj", "--mcp")));
    }
}
