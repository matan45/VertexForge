package com.vertexforge.launcher.service;

import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;

import java.nio.file.Path;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Checks project files in the background, each with its own timeout (VK-1678), so that a project on an unreachable
 * network share never freezes the window.
 * <ul>
 *   <li>Probes run on a cached pool of daemon platform threads. A file-system call on a dead share cannot be
 *       interrupted; its thread simply stays busy until Windows gives up. Virtual threads would not help: blocking
 *       file-system calls pin their carrier thread.</li>
 *   <li>There is at most one probe per path at a time. Checking a path again while it is still being probed (a
 *       Refresh while a share is down) reuses that probe, so stuck threads never pile up.</li>
 *   <li>Every caller gets its own copy of the result with its own timeout; after the timeout the status is
 *       {@code UNREADABLE} and a late answer is ignored.</li>
 * </ul>
 */
public final class ProjectStatusChecker implements AutoCloseable {

    public static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(3);

    private final PathProbe probe;
    private final Duration timeout;
    private final ExecutorService pool;
    private final ConcurrentHashMap<Path, CompletableFuture<ProjectStatus>> inFlight = new ConcurrentHashMap<>();

    public ProjectStatusChecker(PathProbe probe, Duration timeout) {
        this.probe = Objects.requireNonNull(probe, "probe");
        this.timeout = Objects.requireNonNull(timeout, "timeout");
        if (timeout.isNegative() || timeout.isZero()) {
            throw new IllegalArgumentException("timeout must be positive");
        }
        this.pool = Executors.newCachedThreadPool(daemonThreads("vf-status-"));
    }

    /** The entry's status; an entry without a usable path is {@code UNREADABLE} at once. */
    public CompletableFuture<ProjectStatus> check(RecentEntry entry) {
        Objects.requireNonNull(entry, "entry");
        return check(entry.path());
    }

    public CompletableFuture<ProjectStatus> check(Path path) {
        if (path == null) {
            return CompletableFuture.completedFuture(
                    ProjectStatus.unreadable("The stored path is not a valid absolute path"));
        }

        CompletableFuture<ProjectStatus> mine = new CompletableFuture<>();
        CompletableFuture<ProjectStatus> running = inFlight.putIfAbsent(path, mine);
        if (running == null) {
            running = mine;
            try {
                pool.execute(() -> runProbe(path, mine));
            } catch (RejectedExecutionException e) {
                finish(path, mine, ProjectStatus.unreadable("The launcher is shutting down"));
            }
        }
        return running.copy().completeOnTimeout(
                ProjectStatus.unreadable("No response after " + describe(timeout)),
                timeout.toMillis(), TimeUnit.MILLISECONDS);
    }

    /** How many paths are being probed right now (for tests). */
    int inFlightCount() {
        return inFlight.size();
    }

    @Override
    public void close() {
        pool.shutdownNow();
    }

    private void runProbe(Path path, CompletableFuture<ProjectStatus> result) {
        ProjectStatus status = ProjectStatus.unreadable("Could not check the project file");
        try {
            ProjectStatus answer = probe.probe(path);
            if (answer != null) {
                status = answer;
            }
        } catch (RuntimeException e) {
            status = ProjectStatus.unreadable("Could not check the project file: " + e);
        } finally {
            finish(path, result, status);
        }
    }

    /**
     * Leaves the in-flight map BEFORE completing: completion wakes the waiting callers first, and one of them may
     * check the path again at once; it must then start a new probe, not get this finished one. Removing outside any
     * map operation also means a probe that answers at once can never update the map from inside one of its own
     * operations.
     */
    private void finish(Path path, CompletableFuture<ProjectStatus> result, ProjectStatus status) {
        inFlight.remove(path, result);
        result.complete(status);
    }

    private static String describe(Duration duration) {
        long millis = duration.toMillis();
        return millis % 1000 == 0 ? (millis / 1000) + " s" : millis + " ms";
    }

    private static ThreadFactory daemonThreads(String prefix) {
        AtomicInteger count = new AtomicInteger();
        return runnable -> {
            Thread thread = new Thread(runnable, prefix + count.incrementAndGet());
            thread.setDaemon(true);
            return thread;
        };
    }
}
