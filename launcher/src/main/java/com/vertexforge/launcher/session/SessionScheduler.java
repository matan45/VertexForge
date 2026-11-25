package com.vertexforge.launcher.session;

import java.time.Duration;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

/**
 * The one thread every editor session's work runs on (VK-1683): status polls, the ready timeout and the exit
 * handling. One thread runs them one at a time, in order, so a session needs no locks, and the read after the exit
 * always comes after any poll already started. Tests replace it with a manual one.
 */
public interface SessionScheduler extends AutoCloseable {

    /** A scheduled task that can still be cancelled. */
    interface Cancellable {
        void cancel();
    }

    /** Runs {@code task} on the session thread after {@code delay}; ignored once the scheduler is closed. */
    Cancellable schedule(Duration delay, Runnable task);

    /** Runs {@code task} on the session thread as soon as possible; ignored once the scheduler is closed. */
    void execute(Runnable task);

    /** Stops the thread; editors keep running, unsupervised. */
    @Override
    void close();

    /** A daemon "vf-session" thread, so a running editor never keeps the launcher's process alive. */
    static SessionScheduler daemon() {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "vf-session");
            thread.setDaemon(true);
            return thread;
        });
        return new SessionScheduler() {
            @Override
            public Cancellable schedule(Duration delay, Runnable task) {
                try {
                    ScheduledFuture<?> future = executor.schedule(task, delay.toNanos(), TimeUnit.NANOSECONDS);
                    return () -> future.cancel(false);
                } catch (RejectedExecutionException e) {
                    return () -> {
                    };
                }
            }

            @Override
            public void execute(Runnable task) {
                try {
                    executor.execute(task);
                } catch (RejectedExecutionException e) {
                    // Closed: the launcher is exiting.
                }
            }

            @Override
            public void close() {
                executor.shutdownNow();
            }
        };
    }
}
