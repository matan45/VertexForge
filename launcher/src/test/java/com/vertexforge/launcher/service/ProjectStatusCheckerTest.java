package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.engine.EngineCatalog;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.ProjectStatus.Kind;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import com.vertexforge.launcher.service.EditorLauncherTest.RecordingStarter;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectStatusCheckerTest {

    private static final Duration TIMEOUT = Duration.ofMillis(200);

    @TempDir
    Path tempDir;

    /** Never released during a test: stands for a network share that does not answer. */
    private final CountDownLatch shareAnswers = new CountDownLatch(1);
    private final AtomicInteger probeCalls = new AtomicInteger();
    private ProjectStatusChecker checker;

    /** A probe that blocks like a file-system call on an unreachable share. */
    private ProjectStatus blockingProbe(Path path) {
        probeCalls.incrementAndGet();
        try {
            shareAnswers.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        return ProjectStatus.ok();
    }

    @AfterEach
    void tearDown() {
        shareAnswers.countDown();
        if (checker != null) {
            checker.close();
        }
    }

    @Test
    void twentyUnreachableEntriesListInstantlyAndEndUnreadable() throws Exception {
        checker = new ProjectStatusChecker(this::blockingProbe, TIMEOUT);
        Path root = TestSupport.real(tempDir);
        CapturedLog log = TestSupport.log(root.resolve("logs"));
        RecentProjectsRepository repository = new RecentProjectsRepository(
                RecentProjectsRepository.openStore(root.resolve("config/recent-projects.json"), TestSupport.fixedClock(),
                        log.log), UUID::randomUUID, TestSupport.fixedClock(), log.log);
        for (int i = 0; i < 20; i++) {
            repository.add(root.resolve("share").resolve("P" + i).resolve("P" + i + ".vfproj"), "P" + i, 50);
        }
        ProjectHub hub = new ProjectHub(repository, new ProjectCreator(TestSupport.fixedClock(), log.log),
                EngineCatalog.create(TestSupport.environment(root.resolve("config"), TestSupport.fixedClock(),
                        Map.of(), new RecordingStarter(FakeProcess.running(), null)), log.log),
                new EditorLauncher(new RecordingStarter(FakeProcess.running(), null)),
                checker, 50, TestSupport.fixedClock());

        // The window lists the projects without touching any of them...
        RecentProjectsView view = assertTimeoutPreemptively(Duration.ofSeconds(1), hub::listProjects);
        assertEquals(20, view.entries().size());
        assertEquals(0, probeCalls.get(), "listing must never probe a project");

        // ...and starting the checks returns at once, however long the share takes.
        List<CompletableFuture<ProjectStatus>> statuses = assertTimeoutPreemptively(Duration.ofSeconds(1),
                () -> view.entries().stream().map(hub::checkStatus).toList());

        CompletableFuture.allOf(statuses.toArray(CompletableFuture[]::new)).get(10, TimeUnit.SECONDS);
        for (CompletableFuture<ProjectStatus> status : statuses) {
            assertEquals(Kind.UNREADABLE, status.join().kind());
            assertTrue(status.join().reason().contains("No response after 200 ms"), status.join().reason());
        }
        assertEquals(20, probeCalls.get());
    }

    @Test
    void aPathStillBeingCheckedIsNotProbedAgain() throws Exception {
        checker = new ProjectStatusChecker(this::blockingProbe, TIMEOUT);
        Path project = tempDir.resolve("Stuck.vfproj");

        ProjectStatus first = checker.check(project).get(10, TimeUnit.SECONDS);
        ProjectStatus second = checker.check(project).get(10, TimeUnit.SECONDS);

        assertEquals(Kind.UNREADABLE, first.kind());
        assertEquals(Kind.UNREADABLE, second.kind());
        assertEquals(1, probeCalls.get(), "a Refresh while the share is down must reuse the stuck probe");
        assertEquals(1, checker.inFlightCount());
    }

    @Test
    void aProbeThatAnswersLeavesNothingBehindSoTheNextCheckProbesAgain() throws Exception {
        AtomicInteger calls = new AtomicInteger();
        checker = new ProjectStatusChecker(path -> {
            calls.incrementAndGet();
            return ProjectStatus.missing("gone");
        }, Duration.ofSeconds(5));
        Path project = tempDir.resolve("Gone.vfproj");

        assertEquals(Kind.MISSING, checker.check(project).get(10, TimeUnit.SECONDS).kind());
        assertEquals(Kind.MISSING, checker.check(project).get(10, TimeUnit.SECONDS).kind());

        assertEquals(2, calls.get());
        assertEquals(0, checker.inFlightCount());
    }

    @Test
    void aLateAnswerDoesNotChangeATimedOutResult() throws Exception {
        checker = new ProjectStatusChecker(this::blockingProbe, TIMEOUT);
        CompletableFuture<ProjectStatus> status = checker.check(tempDir.resolve("Slow.vfproj"));
        assertEquals(Kind.UNREADABLE, status.get(10, TimeUnit.SECONDS).kind());

        shareAnswers.countDown();

        assertEquals(Kind.UNREADABLE, status.join().kind());
    }

    @Test
    void anEntryWithoutAUsablePathIsUnreadableAtOnce() throws Exception {
        checker = new ProjectStatusChecker(this::blockingProbe, TIMEOUT);
        RecentEntry relative = RecentEntry.fromJson(UUID.randomUUID(),
                com.google.gson.JsonParser.parseString("{\"name\": \"R\", \"path\": \"relative/R.vfproj\"}")
                        .getAsJsonObject());

        ProjectStatus status = checker.check(relative).get(1, TimeUnit.SECONDS);

        assertEquals(Kind.UNREADABLE, status.kind());
        assertEquals(0, probeCalls.get());
    }

    @Test
    void aProbeThatThrowsIsUnreadable() throws Exception {
        checker = new ProjectStatusChecker(path -> {
            throw new IllegalStateException("boom");
        }, Duration.ofSeconds(5));

        ProjectStatus status = checker.check(tempDir.resolve("Bad.vfproj")).get(10, TimeUnit.SECONDS);

        assertEquals(Kind.UNREADABLE, status.kind());
        assertTrue(status.reason().contains("boom"), status.reason());
    }
}
