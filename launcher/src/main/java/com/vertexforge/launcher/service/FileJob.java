package com.vertexforge.launcher.service;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Function;

/**
 * One long file job as the window runs it (VK-1690: a project backup or copy), on a worker thread, with a cancel flag
 * and an outcome that never completes exceptionally. The generic form of {@link CreationJob}, without its hand-over
 * to the store thread.
 * <p>
 * The work itself reports how it ended in its own outcome type ({@code Done}, {@code Cancelled}, {@code Failed}); the
 * job only turns what the work could not report into a failure, through the {@code failure} function: an executor
 * that no longer takes work (the launcher is closing), or an exception that escaped the work.
 *
 * @param <T> the work's outcome
 */
public final class FileJob<T> {

    /** The failure when the executor refuses the work: it has shut down because the launcher is closing. */
    public static final String CLOSING = "The launcher is closing.";
    /** The start of the failure when the work threw instead of returning an outcome. */
    public static final String STOPPED_UNEXPECTEDLY = "The job stopped unexpectedly";

    /** The job's work. Runs on the executor's thread; returns its outcome instead of throwing. */
    @FunctionalInterface
    public interface Work<T> {
        T run(JobMonitor monitor);
    }

    /** Hears how far the work got. Called on the work's thread. */
    @FunctionalInterface
    public interface ProgressListener {
        ProgressListener NONE = (phase, done, total) -> {
        };

        void progress(JobMonitor.Phase phase, long done, long total);
    }

    private final AtomicBoolean cancelRequested = new AtomicBoolean();
    private final CompletableFuture<T> outcome = new CompletableFuture<>();

    private FileJob() {
    }

    /**
     * Starts {@code work} on {@code executor} and returns at once. The outcome is the work's own, except:
     * <ul>
     *     <li>the executor refuses the work: {@code failure.apply(}{@link #CLOSING}{@code )}, completed before this
     *     returns;</li>
     *     <li>the work throws a {@link RuntimeException} {@code e}:
     *     {@code failure.apply(}{@link #STOPPED_UNEXPECTEDLY}{@code + ": " + e)}; it returns {@code null}:
     *     {@code failure.apply(STOPPED_UNEXPECTEDLY + ": it gave no outcome")};</li>
     *     <li>the work throws an {@link Error}: {@code failure.apply(STOPPED_UNEXPECTEDLY + ".")}, after which the
     *     Error goes on to the executor's thread (as {@link CreationJob} does: the window keeps its buttons disabled
     *     until the outcome completes, so it must complete even then).</li>
     * </ul>
     *
     * @param failure makes a failed outcome from its message; it must not throw
     */
    public static <T> FileJob<T> start(Executor executor, Work<T> work, ProgressListener listener,
                                       Function<String, T> failure) {
        Objects.requireNonNull(executor, "executor");
        Objects.requireNonNull(work, "work");
        Objects.requireNonNull(listener, "listener");
        Objects.requireNonNull(failure, "failure");

        FileJob<T> job = new FileJob<>();
        try {
            executor.execute(() -> job.run(work, listener, failure));
        } catch (RejectedExecutionException e) {
            job.outcome.complete(failure.apply(CLOSING));
        }
        return job;
    }

    /**
     * Asks the work to stop. It stops at its next step and removes what it wrote; a work that is already putting its
     * result into place finishes anyway, so the outcome may still be a success.
     */
    public void cancel() {
        cancelRequested.set(true);
    }

    public boolean cancelRequested() {
        return cancelRequested.get();
    }

    /** What happened. Never completes exceptionally. */
    public CompletableFuture<T> outcome() {
        return outcome;
    }

    private void run(Work<T> work, ProgressListener listener, Function<String, T> failure) {
        JobMonitor monitor = new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                listener.progress(phase, done, total);
            }

            @Override
            public boolean cancelRequested() {
                return cancelRequested.get();
            }
        };

        T result = null;
        boolean finished = false;
        try {
            try {
                result = work.run(monitor);
                if (result == null) {
                    result = failure.apply(STOPPED_UNEXPECTEDLY + ": it gave no outcome");
                }
            } catch (RuntimeException e) {
                result = failure.apply(STOPPED_UNEXPECTEDLY + ": " + e);
            }
            finished = true;
        } finally {
            // Even an Error must end the job; it then goes on to the thread's uncaught-exception handler.
            outcome.complete(finished ? result : failure.apply(STOPPED_UNEXPECTEDLY + "."));
        }
    }
}
