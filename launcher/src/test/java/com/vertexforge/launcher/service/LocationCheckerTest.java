package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.service.LocationChecker.LocationCheck;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BooleanSupplier;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class LocationCheckerTest {

    @TempDir
    Path tempDir;

    private final CountDownLatch folderAnswers = new CountDownLatch(1);
    private final AtomicInteger folderProbes = new AtomicInteger();
    private final AtomicInteger targetProbes = new AtomicInteger();
    private LocationChecker checker;

    @AfterEach
    void tearDown() {
        folderAnswers.countDown();
        if (checker != null) {
            checker.close();
        }
    }

    /** A folder probe stuck like a dead network share, until {@link #folderAnswers} is released. */
    private Optional<String> stuckFolder(Path location) {
        folderProbes.incrementAndGet();
        try {
            folderAnswers.await(10, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        return Optional.empty();
    }

    private LocationCheck countedTarget(Path location, String name) {
        targetProbes.incrementAndGet();
        return LocationCheck.ok(List.of("warning for " + name));
    }

    @Test
    void anUnreachableFolderNeverBlocksTheCallerOrPilesUpProbes() throws Exception {
        checker = new LocationChecker(this::stuckFolder, this::countedTarget);
        Path share = tempDir.resolve("share");

        // Typing a name while the folder hangs: every keystroke's check returns at once...
        List<CompletableFuture<LocationCheck>> checks = assertTimeoutPreemptively(Duration.ofSeconds(1), () -> List.of(
                checker.check(share, "G"),
                checker.check(share, "Ga"),
                checker.check(share, "Gam"),
                checker.check(share, "Game")));

        // ...and they all wait on ONE folder probe; no project path is probed while the folder is stuck.
        waitFor(() -> folderProbes.get() == 1);
        Thread.sleep(100);
        assertEquals(1, folderProbes.get());
        assertEquals(0, targetProbes.get());
        assertTrue(checks.stream().noneMatch(CompletableFuture::isDone));

        folderAnswers.countDown();
        for (CompletableFuture<LocationCheck> check : checks) {
            assertTrue(check.get(5, TimeUnit.SECONDS).ok());
        }
        assertEquals("warning for Game", checks.getLast().get().warnings().getFirst());
        assertEquals(4, targetProbes.get());
        waitFor(() -> checker.inFlightCount() == 0);
    }

    @Test
    void aFolderProblemIsTheAnswerAndTheProjectPathIsNotProbed() throws Exception {
        checker = new LocationChecker(location -> Optional.of("The folder is gone."), this::countedTarget);

        LocationCheck check = checker.check(tempDir, "Game").get(5, TimeUnit.SECONDS);

        assertFalse(check.ok());
        assertEquals(Optional.of("The folder is gone."), check.error());
        assertEquals(0, targetProbes.get());
    }

    @Test
    void aWrongNameOrARelativeLocationIsAnsweredWithoutAnyProbe() throws Exception {
        checker = new LocationChecker(location -> {
            folderProbes.incrementAndGet();
            return Optional.empty();
        }, this::countedTarget);

        CompletableFuture<LocationCheck> badName = checker.check(tempDir, "CON");
        CompletableFuture<LocationCheck> relative = checker.check(Path.of("relative"), "Game");

        assertTrue(badName.isDone() && relative.isDone());
        assertEquals(ProjectNameRules.checkName("CON").orElseThrow().message(), badName.get().error().orElseThrow());
        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, relative.get().error().orElseThrow());
        assertEquals(0, folderProbes.get());
    }

    @Test
    void aProbeThatThrowsIsAnErrorNotAHang() throws Exception {
        checker = new LocationChecker(location -> {
            throw new IllegalStateException("probe broke");
        }, this::countedTarget);

        LocationCheck check = checker.check(tempDir, "Game").get(5, TimeUnit.SECONDS);

        assertEquals(Optional.of("Could not check the folder: probe broke"), check.error());
        waitFor(() -> checker.inFlightCount() == 0);
    }

    @Test
    void theRealChecksAgreeWithTheCreator() throws Exception {
        checker = new LocationChecker(LocationWarnings.none(), ProjectTemplate.BUNDLED);
        Path folder = TestSupport.real(tempDir);
        Path full = Files.createDirectory(folder.resolve("Full"));
        Files.writeString(full.resolve("x.txt"), "x");
        Files.createDirectory(folder.resolve("Empty"));

        assertEquals(LocationCheck.ok(List.of()), checker.check(folder, "Game").get(5, TimeUnit.SECONDS));
        assertEquals(LocationCheck.ok(List.of()), checker.check(folder, "Empty").get(5, TimeUnit.SECONDS));
        assertTrue(checker.check(folder, "Full").get(5, TimeUnit.SECONDS).error().orElseThrow()
                .contains("isn't empty"));
        assertTrue(checker.check(folder.resolve("missing"), "Game").get(5, TimeUnit.SECONDS).error().orElseThrow()
                .contains("doesn't exist"));
        assertEquals(Set.of("Empty", "Full"), TestSupport.fileNames(folder), "checking never writes");
    }

    @Test
    void anAbsentDriveIsAnsweredNotHung() throws Exception {
        Optional<Path> absentRoot = TestSupport.absentDriveRoot();
        assumeTrue(absentRoot.isPresent(), "every drive letter from D to Z is in use");
        checker = new LocationChecker(LocationWarnings.none(), ProjectTemplate.BUNDLED);

        LocationCheck check = checker.check(absentRoot.get().resolve("Projects"), "Game").get(10, TimeUnit.SECONDS);

        assertFalse(check.ok());
    }

    private static void waitFor(BooleanSupplier condition) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!condition.getAsBoolean()) {
            if (System.nanoTime() > deadline) {
                throw new AssertionError("condition not met within 5 s");
            }
            Thread.sleep(10);
        }
    }
}
