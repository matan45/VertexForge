package com.vertexforge.launcher.service;

import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.env.ProcessStarter;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.session.EditorSession;
import com.vertexforge.launcher.session.LaunchRequest;
import com.vertexforge.launcher.session.SessionLogs;
import com.vertexforge.launcher.session.SessionScheduler;
import com.vertexforge.launcher.session.SessionStart;
import com.vertexforge.launcher.session.StartResult;

import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;

/**
 * Starts editors and hands them to their sessions (VK-1683).
 * <p>
 * The editor's stdout and stderr go to a new per-launch log ({@link SessionLogs}) through a file handle the editor
 * owns, so the log is complete even when the launcher exits first. The command line puts the project first, then
 * {@code --launch-status <file>} (VK-1682), {@code --mcp --mcp-port <n>} when the project asks for it, and
 * {@code --console} when the setting is on and the engine's {@code engine.json} says its editor accepts it. Editors
 * older than the launch contract ignore the flags they do not know, provided the project comes first; a strict
 * editor (VK-1682 and later) would exit with code 2 on a flag it does not know, hence the {@code launchContract} gate.
 */
public class EditorLauncher implements AutoCloseable {

    private final ProcessStarter processStarter;
    private final SessionLogs logs;
    private final Charset nativeCharset;
    private final Clock clock;
    private final SessionScheduler scheduler;

    /**
     * @param nativeCharset the ANSI code page: the editor's narrow {@code main} only receives paths it can encode
     * @param scheduler     the thread the sessions run on; closed with this launcher
     */
    public EditorLauncher(ProcessStarter processStarter, SessionLogs logs, Charset nativeCharset, Clock clock,
                          SessionScheduler scheduler) {
        this.processStarter = Objects.requireNonNull(processStarter, "processStarter");
        this.logs = Objects.requireNonNull(logs, "logs");
        this.nativeCharset = Objects.requireNonNull(nativeCharset, "nativeCharset");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.scheduler = Objects.requireNonNull(scheduler, "scheduler");
    }

    public ProcessStarter processStarter() {
        return processStarter;
    }

    public SessionLogs logs() {
        return logs;
    }

    /**
     * Starts the editor on a background thread. The file checks run there too: on a network share that has just
     * gone away they can block for many seconds, which must never happen on the FX thread (VK-1678).
     */
    public CompletableFuture<StartResult> start(LaunchRequest request) {
        Objects.requireNonNull(request, "request");
        return CompletableFuture.supplyAsync(() -> startNow(request));
    }

    /**
     * A session for an editor {@link #start} started. It does nothing until {@link EditorSession#begin()}, so the
     * caller can list it first.
     *
     * @param events where the listener is called (the FX thread)
     */
    public EditorSession newSession(SessionStart start, Executor events, EditorSession.Listener listener) {
        return new EditorSession(start, scheduler, events, listener, clock, nativeCharset);
    }

    /** Starts the editor on the calling thread; never throws. */
    StartResult startNow(LaunchRequest request) {
        EngineOption engine = request.engine();
        if (!engine.isOk() || engine.executable() == null) {
            return new StartResult.Failed("The engine cannot be used: " + engine.health().reason());
        }
        Path editor = engine.executable();
        Path workingDirectory = engine.health().workingDirectory();
        Path project = request.project();
        EngineManifest manifest = engine.health().manifest();
        boolean contractKnown = manifest != null && manifest.launchContract() != null;

        if (!Files.isRegularFile(editor) || !Files.isExecutable(editor)) {
            return new StartResult.Failed("Editor not found: " + editor);
        }
        if (!Files.isDirectory(workingDirectory)) {
            return new StartResult.Failed("Working directory not found: " + workingDirectory);
        }
        // An editor with a launch contract reports a missing project itself (code 3, with its reason). An older one
        // would open an empty editor and exit with 0, so it is never started on a project that is not there.
        if (!contractKnown && !Files.exists(project)) {
            return new StartResult.Failed("Project file not found: " + project);
        }

        Instant now = clock.instant();
        SessionLogs.LaunchFiles files;
        try {
            files = logs.create(request.projectName(), now);
        } catch (IOException | RuntimeException e) {
            return new StartResult.Failed("Could not create the launch log in " + logs.directory() + ": "
                    + e.getMessage());
        }
        logs.prune(SessionLogs.KEEP);

        Path status = files.status();
        Optional<String> statusOmitted = nativeCharset.newEncoder().canEncode(status.toString())
                ? Optional.empty()
                : Optional.of("the launcher's logs folder " + logs.directory() + " has characters this system's code"
                        + " page cannot pass to the editor, so the editor cannot report when it is ready");

        Optional<Path> statusFile = statusOmitted.isEmpty() ? Optional.of(status) : Optional.empty();
        boolean console = request.consoleWanted() && manifest != null && manifest.acceptsConsoleFlag();
        List<String> command = command(editor, project, statusFile, request.mcp(), console);

        List<String> header = new ArrayList<>();
        header.add("VertexForge Launcher: opening " + project);
        header.add("Engine: " + editor + " (" + engine.versionText() + ")");
        header.add("Working directory: " + workingDirectory);
        header.add("Command line: " + displayCommand(command));
        statusOmitted.ifPresent(reason -> header.add("No launch status: " + reason));
        header.add("Started: " + now);
        header.add("----");
        try {
            SessionLogs.writeHeader(files.log(), header);
        } catch (IOException e) {
            return new StartResult.Failed("Could not write the launch log " + files.log() + ": " + e.getMessage());
        }

        ProcessBuilder builder = new ProcessBuilder(command);
        builder.directory(workingDirectory.toFile());
        builder.redirectErrorStream(true);
        builder.redirectOutput(ProcessBuilder.Redirect.appendTo(files.log().toFile()));
        Process process;
        try {
            process = processStarter.start(builder);
        } catch (IOException | RuntimeException e) {
            try {
                SessionLogs.appendFooter(files.log(), List.of("VertexForge Launcher: the editor could not be started: "
                        + e.getMessage()));
            } catch (IOException ignored) {
                // The failure is reported in the window anyway.
            }
            return new StartResult.Failed("Failed to start editor: " + e.getMessage());
        }
        // The editor never reads its input; closing the pipe at once leaves nothing waiting on it.
        try {
            process.getOutputStream().close();
        } catch (IOException e) {
            // Nothing to do.
        }

        return new StartResult.Started(new SessionStart(process, project, request.projectName(), files.log(),
                statusFile, statusOmitted, workingDirectory, contractKnown, command, now));
    }

    /** The editor's command line: the project always first (older editors take the first argument as the project). */
    static List<String> command(Path editor, Path project, Optional<Path> statusFile, McpSetting mcp,
                                boolean console) {
        List<String> command = new ArrayList<>();
        command.add(editor.toString());
        command.add(project.toString());
        statusFile.ifPresent(file -> {
            command.add("--launch-status");
            command.add(file.toString());
        });
        if (mcp.enabled()) {
            // Never --mcp-token: a command line is visible to every process on the machine.
            command.add("--mcp");
            command.add("--mcp-port");
            command.add(Integer.toString(mcp.port()));
        }
        if (console) {
            command.add("--console");
        }
        return command;
    }

    /** The command line for the log: arguments with spaces in quotes. */
    static String displayCommand(List<String> command) {
        StringBuilder out = new StringBuilder();
        for (String argument : command) {
            if (!out.isEmpty()) {
                out.append(' ');
            }
            out.append(argument.isEmpty() || argument.indexOf(' ') >= 0 ? "\"" + argument + "\"" : argument);
        }
        return out.toString();
    }

    /** Stops the sessions' thread; running editors are not touched. */
    @Override
    public void close() {
        scheduler.close();
    }
}
