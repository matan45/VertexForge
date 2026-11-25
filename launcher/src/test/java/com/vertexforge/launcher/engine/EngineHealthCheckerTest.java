package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.engine.EngineHealth.Kind;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;

class EngineHealthCheckerTest {

    private static final Duration TIMEOUT = Duration.ofMillis(200);

    @TempDir
    Path tempDir;

    /** Never released during a test: stands for a network share that does not answer. */
    private final CountDownLatch shareAnswers = new CountDownLatch(1);
    private final AtomicInteger probeCalls = new AtomicInteger();
    private EngineHealthChecker checker;

    private EngineHealth blockingProbe(Path executable) {
        probeCalls.incrementAndGet();
        try {
            shareAnswers.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        return EngineHealth.ok(null, executable.getParent());
    }

    @AfterEach
    void tearDown() {
        shareAnswers.countDown();
        if (checker != null) {
            checker.close();
        }
    }

    @Test
    void anEngineThatDoesNotAnswerIsMissingAfterTheTimeout() throws Exception {
        checker = new EngineHealthChecker(this::blockingProbe, TIMEOUT);

        CompletableFuture<EngineHealth> health = assertTimeoutPreemptively(Duration.ofSeconds(1),
                () -> checker.check(tempDir.resolve("share/Editor.exe")));

        EngineHealth answer = health.get(10, TimeUnit.SECONDS);
        assertEquals(Kind.MISSING, answer.kind());
        assertTrue(answer.reason().contains("No response after 200 ms"), answer.reason());
    }

    @Test
    void anEngineIsProbedOnceWhileAProbeIsRunning() throws Exception {
        checker = new EngineHealthChecker(this::blockingProbe, TIMEOUT);
        Path editor = tempDir.resolve("share/Editor.exe");

        CompletableFuture<EngineHealth> first = checker.check(editor);
        CompletableFuture<EngineHealth> second = checker.check(editor);
        first.get(10, TimeUnit.SECONDS);
        second.get(10, TimeUnit.SECONDS);

        assertEquals(1, probeCalls.get());
        assertEquals(1, checker.inFlightCount());
    }

    @Test
    void aProbeThatThrowsOrAnswersNothingIsBroken() throws Exception {
        checker = new EngineHealthChecker(executable -> {
            throw new IllegalStateException("boom");
        }, TIMEOUT);
        EngineHealth thrown = checker.check(tempDir.resolve("a/Editor.exe")).get(10, TimeUnit.SECONDS);
        checker.close();

        checker = new EngineHealthChecker(executable -> null, TIMEOUT);
        EngineHealth nothing = checker.check(tempDir.resolve("b/Editor.exe")).get(10, TimeUnit.SECONDS);

        assertEquals(Kind.BROKEN, thrown.kind());
        assertTrue(thrown.reason().contains("boom"), thrown.reason());
        assertEquals(Kind.BROKEN, nothing.kind());
    }

    @Test
    void anEntryWithoutAPathIsBrokenAtOnce() throws Exception {
        checker = new EngineHealthChecker(this::blockingProbe, TIMEOUT);

        assertEquals(Kind.BROKEN, checker.check(null).get(1, TimeUnit.SECONDS).kind());
        assertEquals(0, probeCalls.get());
    }
}
