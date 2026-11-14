package com.vertexforge.launcher.util;

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
import java.util.function.Function;

/**
 * Runs blocking checks in the background, each with its own timeout (VK-1678; shared with the engine checks of
 * VK-1681), so that a file on an unreachable network share never freezes the window.
 * <ul>
 *   <li>Checks run on a cached pool of daemon platform threads. A file-system call on a dead share cannot be
 *       interrupted; its thread simply stays busy until Windows gives up. Virtual threads would not help: blocking
 *       file-system calls pin their carrier thread.</li>
 *   <li>There is at most one check per key at a time. Checking a key again while it is still being checked (a
 *       Refresh while a share is down) reuses that check, so stuck threads never pile up.</li>
 *   <li>Every caller gets its own copy of the result with its own timeout; after the timeout the result is
 *       {@code timedOut} and a late answer is ignored.</li>
 * </ul>
 *
 * @param <K> what is checked; never {@code null}
 * @param <V> the answer
 */
public final class SingleFlightChecks<K, V> implements AutoCloseable {

    private final Duration timeout;
    private final Function<K, V> work;
    private final V noAnswer;
    private final Function<RuntimeException, V> failed;
    private final V timedOut;
    private final V shuttingDown;
    private final ExecutorService pool;
    private final ConcurrentHashMap<K, CompletableFuture<V>> inFlight = new ConcurrentHashMap<>();

    /**
     * @param threadPrefix the name of every pool thread, followed by a number
     * @param timeout      how long each caller waits; must be positive
     * @param work         the blocking check; it may return {@code null} or throw
     * @param noAnswer     the result when {@code work} returns {@code null} or throws an {@link Error}
     * @param failed       the result when {@code work} throws a {@link RuntimeException}
     * @param timedOut     the result a caller gets when the check takes longer than {@code timeout}
     * @param shuttingDown the result when the pool no longer accepts work
     */
    public SingleFlightChecks(String threadPrefix, Duration timeout, Function<K, V> work, V noAnswer,
                              Function<RuntimeException, V> failed, V timedOut, V shuttingDown) {
        Objects.requireNonNull(threadPrefix, "threadPrefix");
        this.timeout = Objects.requireNonNull(timeout, "timeout");
        if (timeout.isNegative() || timeout.isZero()) {
            throw new IllegalArgumentException("timeout must be positive");
        }
        this.work = Objects.requireNonNull(work, "work");
        this.noAnswer = Objects.requireNonNull(noAnswer, "noAnswer");
        this.failed = Objects.requireNonNull(failed, "failed");
        this.timedOut = Objects.requireNonNull(timedOut, "timedOut");
        this.shuttingDown = Objects.requireNonNull(shuttingDown, "shuttingDown");
        this.pool = Executors.newCachedThreadPool(daemonThreads(threadPrefix));
    }

    /** The answer for {@code key}, from a check that is already running for it or from a new one. */
    public CompletableFuture<V> check(K key) {
        Objects.requireNonNull(key, "key");
        CompletableFuture<V> mine = new CompletableFuture<>();
        CompletableFuture<V> running = inFlight.putIfAbsent(key, mine);
        if (running == null) {
            running = mine;
            try {
                pool.execute(() -> run(key, mine));
            } catch (RejectedExecutionException e) {
                finish(key, mine, shuttingDown);
            }
        }
        return running.copy().completeOnTimeout(timedOut, timeout.toMillis(), TimeUnit.MILLISECONDS);
    }

    /** How many keys are being checked right now (for tests). */
    public int inFlightCount() {
        return inFlight.size();
    }

    /** Stops the pool; a check still running keeps its thread until its file-system call returns. */
    @Override
    public void close() {
        pool.shutdownNow();
    }

    /** {@code 3 s} for whole seconds, else {@code 200 ms}. */
    public static String describe(Duration duration) {
        long millis = duration.toMillis();
        return millis % 1000 == 0 ? (millis / 1000) + " s" : millis + " ms";
    }

    private void run(K key, CompletableFuture<V> result) {
        V answer = noAnswer;
        try {
            V value = work.apply(key);
            if (value != null) {
                answer = value;
            }
        } catch (RuntimeException e) {
            answer = failed.apply(e);
        } finally {
            finish(key, result, answer);
        }
    }

    /**
     * Leaves the in-flight map BEFORE completing: completion wakes the waiting callers first, and one of them may
     * check the key again at once; it must then start a new check, not get this finished one. Removing outside any
     * map operation also means a check that answers at once can never update the map from inside one of its own
     * operations.
     */
    private void finish(K key, CompletableFuture<V> result, V answer) {
        inFlight.remove(key, result);
        result.complete(answer);
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
