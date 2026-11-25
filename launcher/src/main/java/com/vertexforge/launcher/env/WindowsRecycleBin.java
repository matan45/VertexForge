package com.vertexforge.launcher.env;

import java.nio.file.Path;
import java.time.Duration;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.BooleanSupplier;

/**
 * The Windows Recycle Bin through the shell's {@code IFileOperation} ({@link ShellFileOperation}), with the safety
 * rules of {@link ShellProgressSink} and {@link ShellResults} (VK-1695).
 * <p>
 * Every check and recycle runs on a NEW platform daemon thread named {@value #THREAD_NAME}: COM makes that thread a
 * single-threaded apartment for the length of the operation, which a pooled thread (the caller's executor, a virtual
 * thread's carrier) must never be left with. The caller waits for an answer, up to {@link #CHECK_TIMEOUT} for a check
 * and {@link #RECYCLE_TIMEOUT} for a recycle; after that it is told so, and the thread finishes on its own.
 * <p>
 * Paths the shell would not take as they are, or that could name another folder than they seem to, are refused
 * before any native call or thread: relative, longer than {@value #MAX_PATH_CHARS} characters ({@code MAX_PATH} - 1,
 * the length the shell's copy engine is built for), {@code \\?\} or {@code \\.\} prefixed, not normalized
 * ({@code .} or {@code ..}: {@code Project\..} is the folder that holds the project), or a drive root.
 */
final class WindowsRecycleBin implements RecycleBin {

    static final String THREAD_NAME = "vf-recycle";
    static final Duration CHECK_TIMEOUT = Duration.ofMinutes(5);
    static final Duration RECYCLE_TIMEOUT = Duration.ofMinutes(10);
    /** {@code MAX_PATH} (260) less the terminating NUL. */
    static final int MAX_PATH_CHARS = 259;

    static final String NO_ANSWER_IN_TIME = "Windows did not answer in time";

    /** A new platform daemon thread per operation; never a virtual one (see the class comment). */
    static final ThreadFactory RECYCLE_THREADS = Thread.ofPlatform().name(THREAD_NAME).daemon(true).factory();

    private final Duration checkTimeout;
    private final Duration recycleTimeout;
    private final ThreadFactory threads;

    WindowsRecycleBin() {
        this(CHECK_TIMEOUT, RECYCLE_TIMEOUT, RECYCLE_THREADS);
    }

    /**
     * @param checkTimeout   how long {@link #check} waits for Windows
     * @param recycleTimeout how long {@link #recycle} waits for Windows
     * @param threads        makes the thread each operation runs on; tests replace it to count threads or to hold
     *                       an operation back
     */
    WindowsRecycleBin(Duration checkTimeout, Duration recycleTimeout, ThreadFactory threads) {
        this.checkTimeout = Objects.requireNonNull(checkTimeout, "checkTimeout");
        this.recycleTimeout = Objects.requireNonNull(recycleTimeout, "recycleTimeout");
        this.threads = Objects.requireNonNull(threads, "threads");
    }

    /**
     * An answer with what the operation saw, for tests and diagnostics. The observation is empty when no operation
     * ran or none finished: a refused path, a timeout, a failure to start.
     */
    record Attempt<T>(T answer, Optional<ShellResults.Observation> observation) {
        Attempt {
            Objects.requireNonNull(answer, "answer");
            Objects.requireNonNull(observation, "observation");
        }

        static <T> Attempt<T> without(T answer) {
            return new Attempt<>(answer, Optional.empty());
        }
    }

    /** Whether the COM and shell functions were found. Whether {@code IFileOperation} works shows per operation. */
    @Override
    public boolean available() {
        return ShellFileOperation.available();
    }

    @Override
    public RecycleCheck check(Path folder, BooleanSupplier cancelRequested) {
        return checkAttempt(folder, cancelRequested).answer();
    }

    @Override
    public RecycleResult recycle(Path folder) {
        return recycleAttempt(folder).answer();
    }

    /** {@link #check}, with what the operation saw. Never throws. */
    Attempt<RecycleCheck> checkAttempt(Path folder, BooleanSupplier cancelRequested) {
        try {
            String refusal = refusal(folder);
            if (refusal != null) {
                return Attempt.without(new RecycleCheck.Unknown(refusal));
            }
            if (!available()) {
                return Attempt.without(new RecycleCheck.Unknown(UNAVAILABLE));
            }
            BooleanSupplier cancel = cancelRequested == null ? () -> false : cancelRequested;
            Run run = runOnRecycleThread(
                    () -> ShellFileOperation.run(folder, ShellFileOperation.Mode.CHECK, cancel), checkTimeout, cancel);
            return switch (run) {
                case Run.Done done -> new Attempt<>(
                        ShellResults.classifyCheck(done.observation(), cancelled(cancel)),
                        Optional.of(done.observation()));
                // The dry run goes on without a listener; its sink still stops it before anything is deleted.
                case Run.Cancelled cancelled -> Attempt.without(new RecycleCheck.Cancelled());
                case Run.TimedOut timedOut -> Attempt.without(new RecycleCheck.Unknown(NO_ANSWER_IN_TIME));
                case Run.Crashed crashed -> Attempt.without(new RecycleCheck.Unknown(
                        "Windows could not be asked about the Recycle Bin (" + describe(crashed.failure()) + ")"));
            };
        } catch (Throwable t) {
            return Attempt.without(new RecycleCheck.Unknown(
                    "Windows could not be asked about the Recycle Bin (" + describe(t) + ")"));
        }
    }

    /** {@link #recycle}, with what the operation saw. Never throws. */
    Attempt<RecycleResult> recycleAttempt(Path folder) {
        try {
            String refusal = refusal(folder);
            if (refusal != null) {
                return Attempt.without(new RecycleResult.Failed(refusal));
            }
            if (!available()) {
                return Attempt.without(new RecycleResult.Failed(UNAVAILABLE));
            }
            Run run = runOnRecycleThread(
                    () -> ShellFileOperation.run(folder, ShellFileOperation.Mode.RECYCLE, () -> false),
                    recycleTimeout, () -> false);
            return switch (run) {
                case Run.Done done -> new Attempt<>(ShellResults.classifyRecycle(done.observation()),
                        Optional.of(done.observation()));
                case Run.Cancelled cancelled -> Attempt.without(new RecycleResult.Failed(
                        "The move was cancelled while Windows was working. Look in the Recycle Bin and in "
                                + folder.getParent() + " to see where it is."));
                case Run.TimedOut timedOut -> Attempt.without(new RecycleResult.Failed(
                        "Windows has not finished moving the folder after " + describe(recycleTimeout)
                                + ". Look in the Recycle Bin and in " + folder.getParent()
                                + " to see where it is."));
                case Run.Crashed crashed -> Attempt.without(new RecycleResult.Failed(
                        "Windows could not move the folder to the Recycle Bin (" + describe(crashed.failure())
                                + ")"));
            };
        } catch (Throwable t) {
            return Attempt.without(new RecycleResult.Failed(
                    "Windows could not move the folder to the Recycle Bin (" + describe(t) + ")"));
        }
    }

    /**
     * Why {@code folder} is refused before asking Windows, {@code null} when it may be asked. See the class comment
     * for each rule.
     */
    static String refusal(Path folder) {
        if (folder == null) {
            return "No folder was given";
        }
        String text = folder.toString();
        if (text.startsWith("\\\\?\\") || text.startsWith("\\\\.\\")) {
            return "The Recycle Bin does not take a path in the \\\\?\\ or \\\\.\\ form: " + text;
        }
        if (!folder.isAbsolute()) {
            return "The path is not absolute: " + text;
        }
        if (text.length() > MAX_PATH_CHARS) {
            return "The path is longer than the " + MAX_PATH_CHARS + " characters the Recycle Bin takes";
        }
        if (!folder.equals(folder.normalize())) {
            return "The path holds . or .. names: " + text;
        }
        if (folder.getParent() == null || folder.getFileName() == null) {
            return "A drive root cannot be moved to the Recycle Bin: " + text;
        }
        return null;
    }

    /** One operation on the recycle thread; {@link ShellFileOperation#run} throws {@link Throwable}. */
    @FunctionalInterface
    private interface Task {
        ShellResults.Observation call() throws Throwable;
    }

    /** How a wait on the recycle thread ended. */
    private sealed interface Run {
        record Done(ShellResults.Observation observation) implements Run {
        }

        record TimedOut() implements Run {
        }

        record Cancelled() implements Run {
        }

        record Crashed(Throwable failure) implements Run {
        }
    }

    /** How often a wait looks at the caller's cancel. */
    static final Duration CANCEL_POLL = Duration.ofMillis(100);

    /**
     * Runs {@code task} on a new thread from {@link #threads} and waits up to {@code timeout}, looking at
     * {@code cancelled} every {@link #CANCEL_POLL}: a cancel ends the wait at once, even while Windows reports no
     * progress (the operation goes on, and its answer is lost). An interrupted wait keeps the interrupt and counts as
     * a timeout.
     */
    private Run runOnRecycleThread(Task task, Duration timeout, BooleanSupplier cancelled) {
        CompletableFuture<ShellResults.Observation> answer = new CompletableFuture<>();
        Thread thread = threads.newThread(() -> {
            try {
                answer.complete(task.call());
            } catch (Throwable t) {
                answer.completeExceptionally(t);
            }
        });
        if (thread == null) {
            return new Run.Crashed(new IllegalStateException("no thread could be made for the operation"));
        }
        thread.start();
        long deadline = System.nanoTime() + timeout.toNanos();
        try {
            while (true) {
                long left = deadline - System.nanoTime();
                if (left <= 0) {
                    return new Run.TimedOut();
                }
                try {
                    return new Run.Done(answer.get(Math.min(left, CANCEL_POLL.toNanos()), TimeUnit.NANOSECONDS));
                } catch (TimeoutException e) {
                    if (cancelled(cancelled)) {
                        return new Run.Cancelled();
                    }
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return new Run.TimedOut();
        } catch (ExecutionException e) {
            return new Run.Crashed(e.getCause() == null ? e : e.getCause());
        }
    }

    /** Whether the caller has cancelled; a supplier that throws has not. */
    private static boolean cancelled(BooleanSupplier cancel) {
        try {
            return cancel.getAsBoolean();
        } catch (Throwable t) {
            return false;
        }
    }

    /** "10 minutes", "1 minute", "30 seconds". */
    static String describe(Duration duration) {
        long minutes = duration.toMinutes();
        if (minutes >= 1) {
            return minutes + (minutes == 1 ? " minute" : " minutes");
        }
        long seconds = duration.toSeconds();
        return seconds + (seconds == 1 ? " second" : " seconds");
    }

    private static String describe(Throwable t) {
        String message = t.getMessage();
        return message == null || message.isBlank()
                ? t.getClass().getSimpleName()
                : t.getClass().getSimpleName() + ": " + message;
    }
}
