package com.vertexforge.launcher.session;

import com.vertexforge.launcher.service.EditorExitCode;
import com.vertexforge.launcher.session.CrashReports.CrashReport;

import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalLong;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Consumer;

/**
 * Watches one editor the launcher started (VK-1683): Starting until the editor's {@code --launch-status} file says
 * {@code loading}, then Loading, then Ready; No ready signal when no status has arrived after {@link #READY_TIMEOUT};
 * Exited, with a {@link SessionReport}, once the process ends ({@link Process#onExit()}).
 * <p>
 * Threads: all of the session's own work (polls every {@link #POLL_INTERVAL}, the timeout, the exit handling, the
 * reads of the log and the crash folder) runs on the {@link SessionScheduler}'s single thread, so its state needs no
 * locks and the read after the exit always follows any poll already in flight. The listener is called through
 * {@code events} (the FX thread), in order: every phase change at most once, and {@code exited} exactly once, last.
 */
public final class EditorSession {

    public enum Phase {
        /** Started; nothing heard from the editor yet. */
        STARTING("Starting…"),
        /** The editor reported {@code loading}. */
        LOADING("Loading…"),
        /** The project and its startup scene have loaded. */
        READY("Running"),
        /** Still running, but no status arrived within {@link #READY_TIMEOUT}. */
        NO_READY_SIGNAL("Running (no ready signal)"),
        /** The process ended. */
        EXITED("Closed");

        private final String label;

        Phase(String label) {
            this.label = label;
        }

        /** The Status column's text while the session is live. */
        public String label() {
            return label;
        }

        /** Whether the editor has not reported Ready (it may still fail to open the project). */
        public boolean beforeReady() {
            return this == STARTING || this == LOADING || this == NO_READY_SIGNAL;
        }
    }

    /** What the window learns about a session; every call arrives on the {@code events} executor. */
    public interface Listener {
        void phaseChanged(EditorSession session, Phase phase);

        /** The editor's crash handler wrote {@code failed}; the editor may still be alive behind its crash box. */
        void crashReported(EditorSession session, LaunchStatus status);

        /** The editor ended; called once, after every other call. */
        void exited(EditorSession session, SessionReport report);
    }

    public static final Duration POLL_INTERVAL = Duration.ofMillis(250);
    public static final Duration READY_TIMEOUT = Duration.ofSeconds(15);
    public static final int LOG_TAIL_LINES = 30;

    private final SessionStart start;
    private final SessionScheduler scheduler;
    private final Executor events;
    private final Listener listener;
    private final Clock clock;
    private final Charset nativeCharset;

    // Session thread only.
    private Phase phase = Phase.STARTING;
    private LaunchStatus last;
    /** The latest crash folder any status line named (VK-1732); null while none has (or for an older editor). */
    private Path reportedCrashDir;
    private boolean crashSignalled;
    private boolean begun;
    private SessionScheduler.Cancellable poll;
    private SessionScheduler.Cancellable readyTimeout;

    /**
     * @param events        where the listener is called (the FX thread); a rejection (the toolkit is gone) is ignored
     * @param nativeCharset the ANSI code page, for log and crash-report lines that are not UTF-8 (an editor older
     *                      than VK-1684)
     */
    public EditorSession(SessionStart start, SessionScheduler scheduler, Executor events, Listener listener,
                         Clock clock, Charset nativeCharset) {
        this.start = Objects.requireNonNull(start, "start");
        this.scheduler = Objects.requireNonNull(scheduler, "scheduler");
        this.events = Objects.requireNonNull(events, "events");
        this.listener = Objects.requireNonNull(listener, "listener");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.nativeCharset = Objects.requireNonNull(nativeCharset, "nativeCharset");
    }

    public SessionStart start() {
        return start;
    }

    /** Starts watching the editor. Call once; any thread. */
    public void begin() {
        scheduler.execute(() -> {
            if (begun) {
                return;
            }
            begun = true;
            if (phase == Phase.EXITED) {
                return;
            }
            readyTimeout = scheduler.schedule(READY_TIMEOUT, this::onReadyTimeout);
            if (start.statusFile().isPresent()) {
                poll = scheduler.schedule(POLL_INTERVAL, this::poll);
            }
        });
        // Queued after the task above, even when the editor has already exited.
        start.process().onExit().whenComplete((process, error) -> scheduler.execute(this::onExit));
    }

    private void poll() {
        if (phase == Phase.EXITED) {
            return;
        }
        readStatus().ifPresent(this::apply);
        if (phase != Phase.EXITED) {
            poll = scheduler.schedule(POLL_INTERVAL, this::poll);
        }
    }

    private Optional<LaunchStatus> readStatus() {
        if (start.statusFile().isEmpty()) {
            return Optional.empty();
        }
        OptionalLong pid = editorPid();
        return pid.isPresent()
                ? LaunchStatusReader.read(start.statusFile().get(), pid.getAsLong())
                : LaunchStatusReader.read(start.statusFile().get());
    }

    /** The editor's process id; empty when the platform cannot tell. */
    private OptionalLong editorPid() {
        try {
            return OptionalLong.of(start.process().pid());
        } catch (UnsupportedOperationException e) {
            return OptionalLong.empty();
        }
    }

    /** Keeps {@code status} as the last one heard, and the crash folder it names. */
    private void remember(LaunchStatus status) {
        last = status;
        status.crashDir().ifPresent(dir -> reportedCrashDir = dir);
    }

    /** Where the editor writes its crash reports: the folder its status named, else its working directory's. */
    private Path crashFolder() {
        return reportedCrashDir != null ? reportedCrashDir : CrashReports.folder(start.workingDirectory());
    }

    private void apply(LaunchStatus status) {
        remember(status);
        switch (status.state()) {
            case LOADING -> {
                if (phase == Phase.STARTING || phase == Phase.NO_READY_SIGNAL) {
                    setPhase(Phase.LOADING);
                }
            }
            case READY -> {
                if (phase != Phase.READY) {
                    setPhase(Phase.READY);
                }
            }
            case FAILED -> {
                // A refusal (2 to 5) is followed by the exit within moments; only a crash can leave the editor alive.
                if (status.isCrash() && !crashSignalled) {
                    crashSignalled = true;
                    emit(target -> target.crashReported(this, status));
                }
            }
        }
    }

    private void onReadyTimeout() {
        if (phase == Phase.STARTING && last == null) {
            setPhase(Phase.NO_READY_SIGNAL);
        }
    }

    private void setPhase(Phase next) {
        phase = next;
        emit(target -> target.phaseChanged(this, next));
    }

    private void onExit() {
        if (phase == Phase.EXITED) {
            return;
        }
        cancel(poll);
        cancel(readyTimeout);
        phase = Phase.EXITED;

        SessionReport report;
        try {
            // The last line can land between the last poll and the exit.
            readStatus().ifPresent(this::remember);
            int exitCode = start.process().exitValue();
            Optional<LaunchStatus> lastStatus = Optional.ofNullable(last);
            SessionOutcome outcome = SessionOutcome.classify(exitCode, lastStatus, start.statusFile().isPresent(),
                    start.contractKnown());
            boolean failure = outcome.showsFailureDialog();
            List<String> tail = failure
                    ? SessionLogs.tail(start.log(), LOG_TAIL_LINES, nativeCharset)
                    : List.of();
            Path crashFolder = crashFolder();
            // VK-1732: the folder may be shared by several editors; the report tagged with this one's pid is its own.
            Optional<CrashReport> crash = failure
                    ? CrashReports.newestIn(crashFolder, start.startedAt(), editorPid(), nativeCharset)
                    : Optional.empty();
            Instant now = clock.instant();
            appendFooter(exitCode, now);
            report = new SessionReport(outcome, exitCode, lastStatus, tail, crash, start.log(),
                    start.workingDirectory(), crashFolder, now);
        } catch (RuntimeException e) {
            // Never leave the window waiting for an exit that was already seen.
            report = new SessionReport(new SessionOutcome.Died(-1, Optional.ofNullable(last).map(LaunchStatus::state)),
                    -1, Optional.ofNullable(last), List.of(), Optional.empty(), start.log(), start.workingDirectory(),
                    crashFolder(), clock.instant());
        }
        SessionReport finalReport = report;
        emit(target -> target.exited(this, finalReport));
    }

    private void appendFooter(int exitCode, Instant now) {
        Duration ran = Duration.between(start.startedAt(), now);
        try {
            SessionLogs.appendFooter(start.log(), List.of(
                    "----",
                    "VertexForge Launcher: the editor exited with " + EditorExitCode.describe(exitCode) + " at " + now
                            + " after " + formatDuration(ran) + "."));
        } catch (IOException e) {
            // The editor's own output is complete without it.
        }
    }

    static String formatDuration(Duration duration) {
        long seconds = Math.max(0, duration.toSeconds());
        if (seconds < 60) {
            return seconds + " s";
        }
        long minutes = seconds / 60;
        if (minutes < 60) {
            return minutes + " min " + (seconds % 60) + " s";
        }
        return (minutes / 60) + " h " + (minutes % 60) + " min";
    }

    private static void cancel(SessionScheduler.Cancellable task) {
        if (task != null) {
            task.cancel();
        }
    }

    private void emit(Consumer<Listener> call) {
        try {
            events.execute(() -> call.accept(listener));
        } catch (RejectedExecutionException | IllegalStateException e) {
            // The window is gone (the launcher is exiting).
        }
    }
}
