package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BooleanSupplier;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * The Recycle Bin's dry run against the real shell, and everything around it that needs no Windows (VK-1695). No
 * test here recycles anything: a dry run's sink stops every delete, and each test proves its folder is untouched.
 * {@link WindowsRecycleBinRealTest} is the one that really recycles, on request.
 */
class WindowsRecycleBinTest {

    @TempDir
    Path temp;

    /** A factory that counts the threads it makes and remembers them. */
    private static final class CountingThreads implements ThreadFactory {
        final AtomicInteger made = new AtomicInteger();
        final List<Thread> threads = new ArrayList<>();

        @Override
        public synchronized Thread newThread(Runnable task) {
            made.incrementAndGet();
            Thread thread = WindowsRecycleBin.RECYCLE_THREADS.newThread(task);
            threads.add(thread);
            return thread;
        }
    }

    /** A thread that never runs the operation, so the caller's wait always runs out. */
    private static final ThreadFactory NEVER_RUNS = task -> Thread.ofPlatform().daemon(true).unstarted(() -> {
    });

    private static WindowsRecycleBin bin(ThreadFactory threads) {
        return new WindowsRecycleBin(WindowsRecycleBin.CHECK_TIMEOUT, WindowsRecycleBin.RECYCLE_TIMEOUT, threads);
    }

    /** A project-like folder: a file, a subfolder with a file, an empty subfolder. */
    private Path projectFolder(String name) throws IOException {
        Path folder = Files.createDirectories(temp.resolve(name));
        Files.writeString(folder.resolve("Game.vfproj"), "{\"projectName\":\"" + name + "\"}");
        Files.createDirectories(folder.resolve("assets").resolve("scenes"));
        Files.write(folder.resolve("assets").resolve("scenes").resolve("Main.vfScene"), new byte[] {1, 2, 3, 0, 4});
        Files.createDirectories(folder.resolve("empty"));
        return folder;
    }

    /** Every name under {@code folder} with its kind, size and content. */
    private static Map<String, String> snapshot(Path folder) throws IOException {
        Map<String, String> entries = new TreeMap<>();
        try (Stream<Path> walk = Files.walk(folder)) {
            for (Path path : walk.toList()) {
                String name = folder.relativize(path).toString();
                if (Files.isDirectory(path)) {
                    entries.put(name, "dir");
                } else {
                    byte[] content = Files.readAllBytes(path);
                    entries.put(name, "file " + Files.size(path) + " " + Base64.getEncoder().encodeToString(content));
                }
            }
        }
        return entries;
    }

    private static String describe(WindowsRecycleBin.Attempt<?> attempt) {
        return attempt.answer() + " / " + attempt.observation().map(ShellResults.Observation::describe).orElse("-");
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void theSystemRecycleBinIsAvailableOnWindows() {
        RecycleBin system = RecycleBin.system();

        assertInstanceOf(WindowsRecycleBin.class, system);
        assertTrue(system.available());
    }

    @Test
    void theUnavailableBinRecyclesNothing() throws IOException {
        Path folder = projectFolder("Unavailable");
        RecycleBin bin = RecycleBin.unavailable();

        assertFalse(bin.available());
        assertEquals(new RecycleCheck.Unknown(RecycleBin.UNAVAILABLE), bin.check(folder, () -> false));
        assertEquals(new RecycleResult.Failed(RecycleBin.UNAVAILABLE), bin.recycle(folder));
        assertTrue(Files.isDirectory(folder));
    }

    /** The shell is asked and reaches the sink; the dry run deletes nothing, not one byte. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aDryRunAsksWindowsAndDeletesNothing() throws IOException {
        Path folder = projectFolder("Checked");
        Map<String, String> before = snapshot(folder);
        CountingThreads threads = new CountingThreads();

        WindowsRecycleBin.Attempt<RecycleCheck> attempt = bin(threads).checkAttempt(folder, () -> false);

        assertTrue(attempt.answer() instanceof RecycleCheck.Recyclable
                || attempt.answer() instanceof RecycleCheck.NotRecyclable, describe(attempt));
        ShellResults.Observation observation = attempt.observation().orElseThrow();
        assertEquals(1, observation.sink().preDeleteCalls(), describe(attempt));
        assertFalse(observation.sink().newlyCreated(), describe(attempt));
        // The operation-level sink, where a cancel is polled, heard the operation start.
        assertTrue(observation.sink().startOperationsCalls() >= 1, describe(attempt));
        assertEquals(before, snapshot(folder));
        // One operation, on its own platform daemon thread.
        assertEquals(1, threads.made.get());
        Thread thread = threads.threads.getFirst();
        assertEquals(WindowsRecycleBin.THREAD_NAME, thread.getName());
        assertTrue(thread.isDaemon());
        assertFalse(thread.isVirtual());
    }

    /** The temp folder is on a fixed drive, which has a Recycle Bin. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aFolderOnTheTempDriveIsRecyclable() throws IOException {
        Path folder = projectFolder("OnFixedDrive");
        assumeTrue(new WindowsDriveTypes().kindOf(folder.getRoot()) == DriveTypes.Kind.LOCAL,
                "the temp folder is not on a fixed drive");
        Map<String, String> before = snapshot(folder);

        RecycleCheck check = RecycleBin.system().check(folder, () -> false);

        assertEquals(new RecycleCheck.Recyclable(), check);
        assertEquals(before, snapshot(folder));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aMissingFolderIsUnknownAndNamesTheStep() {
        Path missing = temp.resolve("does-not-exist");

        WindowsRecycleBin.Attempt<RecycleCheck> attempt = bin(WindowsRecycleBin.RECYCLE_THREADS)
                .checkAttempt(missing, () -> false);

        RecycleCheck.Unknown unknown = assertInstanceOf(RecycleCheck.Unknown.class, attempt.answer(),
                describe(attempt));
        assertTrue(unknown.reason().contains("SHCreateItemFromParsingName"), unknown.reason());
        assertEquals(0, attempt.observation().orElseThrow().sink().preDeleteCalls());
        assertFalse(Files.exists(missing));
    }

    /** Refused before any thread or native call; so is a recycle of the same path. */
    @Test
    void aPathTooLongForTheRecycleBinIsRefusedAtOnce() {
        Path base = temp.toAbsolutePath();
        String name = "x".repeat(WindowsRecycleBin.MAX_PATH_CHARS - base.toString().length());
        Path tooLong = base.resolve(name);
        assertEquals(WindowsRecycleBin.MAX_PATH_CHARS + 1, tooLong.toString().length());
        CountingThreads threads = new CountingThreads();
        WindowsRecycleBin bin = bin(threads);

        RecycleCheck check = assertTimeoutPreemptively(Duration.ofSeconds(2), () -> bin.check(tooLong, () -> false));
        RecycleResult recycle = assertTimeoutPreemptively(Duration.ofSeconds(2), () -> bin.recycle(tooLong));

        assertInstanceOf(RecycleCheck.Unknown.class, check);
        assertInstanceOf(RecycleResult.Failed.class, recycle);
        assertEquals(0, threads.made.get());
    }

    @Test
    void pathsThatCouldNameAnotherFolderAreRefusedWithoutAThread() {
        Path base = temp.toAbsolutePath();
        CountingThreads threads = new CountingThreads();
        WindowsRecycleBin bin = bin(threads);

        for (Path path : new Path[] {
            null,
            Path.of("relative", "Project"),
            base.resolve("Project").resolve(".."),
            base.resolve(".").resolve("Project"),
            base.getRoot()}) {
            assertInstanceOf(RecycleCheck.Unknown.class, bin.check(path, () -> false), String.valueOf(path));
            assertInstanceOf(RecycleResult.Failed.class, bin.recycle(path), String.valueOf(path));
            assertFalse(WindowsRecycleBin.refusal(path) == null, String.valueOf(path));
        }
        assertEquals(0, threads.made.get());
        assertNull(WindowsRecycleBin.refusal(base.resolve("Project")));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void twoDryRunsAtOnceBothAnswer() throws Exception {
        Path first = projectFolder("First");
        Path second = projectFolder("Second");
        Map<String, String> firstBefore = snapshot(first);
        Map<String, String> secondBefore = snapshot(second);
        RecycleBin bin = RecycleBin.system();
        ExecutorService callers = Executors.newFixedThreadPool(2);
        try {
            Future<RecycleCheck> a = callers.submit(() -> bin.check(first, () -> false));
            Future<RecycleCheck> b = callers.submit(() -> bin.check(second, () -> false));

            for (RecycleCheck check : List.of(a.get(2, TimeUnit.MINUTES), b.get(2, TimeUnit.MINUTES))) {
                assertTrue(check instanceof RecycleCheck.Recyclable || check instanceof RecycleCheck.NotRecyclable,
                        String.valueOf(check));
            }
        } finally {
            callers.shutdownNow();
        }
        assertEquals(firstBefore, snapshot(first));
        assertEquals(secondBefore, snapshot(second));
    }

    /** Whichever comes first, the cancel or Windows' answer, nothing is deleted. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aCancelledDryRunDeletesNothing() throws IOException {
        Path folder = projectFolder("Cancelled");
        Map<String, String> before = snapshot(folder);
        BooleanSupplier cancelled = () -> true;

        RecycleCheck check = RecycleBin.system().check(folder, cancelled);
        WindowsRecycleBin.Attempt<RecycleCheck> attempt = bin(WindowsRecycleBin.RECYCLE_THREADS)
                .checkAttempt(folder, cancelled);

        assertTrue(check instanceof RecycleCheck.Cancelled || check instanceof RecycleCheck.Recyclable
                || check instanceof RecycleCheck.NotRecyclable, String.valueOf(check));
        ShellResults.SinkReport sink = attempt.observation().orElseThrow().sink();
        assertTrue(sink.cancelObserved() || sink.preDeleteCalls() == 1, describe(attempt));
        assertEquals(before, snapshot(folder));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aCancelSupplierThatThrowsCountsAsNotCancelled() throws IOException {
        Path folder = projectFolder("BrokenCancel");
        Map<String, String> before = snapshot(folder);

        RecycleCheck check = RecycleBin.system().check(folder, () -> {
            throw new IllegalStateException("broken");
        });

        assertTrue(check instanceof RecycleCheck.Recyclable || check instanceof RecycleCheck.NotRecyclable,
                String.valueOf(check));
        assertEquals(before, snapshot(folder));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aDryRunWindowsDoesNotAnswerInTimeIsUnknown() throws IOException {
        Path folder = projectFolder("Slow");
        WindowsRecycleBin bin = new WindowsRecycleBin(Duration.ofMillis(100), Duration.ofSeconds(1), NEVER_RUNS);

        assertEquals(new RecycleCheck.Unknown(WindowsRecycleBin.NO_ANSWER_IN_TIME), bin.check(folder, () -> false));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aRecycleWindowsDoesNotFinishInTimeSaysWhereToLook() throws IOException {
        Path folder = projectFolder("SlowRecycle");
        WindowsRecycleBin bin = new WindowsRecycleBin(Duration.ofMillis(100), Duration.ofSeconds(1), NEVER_RUNS);

        assertEquals(new RecycleResult.Failed("Windows has not finished moving the folder after 1 second. Look in "
                + "the Recycle Bin and in " + folder.getParent() + " to see where it is."), bin.recycle(folder));
        assertTrue(Files.isDirectory(folder));
        assertEquals("10 minutes", WindowsRecycleBin.describe(WindowsRecycleBin.RECYCLE_TIMEOUT));
        assertEquals("1 minute", WindowsRecycleBin.describe(Duration.ofMinutes(1)));
        assertEquals("30 seconds", WindowsRecycleBin.describe(Duration.ofSeconds(30)));
    }

    /** An interrupted caller stops waiting at once, keeps its interrupt, and is answered as after a timeout. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void anInterruptedWaitKeepsTheInterruptAndAnswersLikeATimeout() throws IOException {
        Path folder = projectFolder("Interrupted");
        WindowsRecycleBin bin = new WindowsRecycleBin(Duration.ofMinutes(5), Duration.ofMinutes(10), NEVER_RUNS);
        try {
            long start = System.nanoTime();
            Thread.currentThread().interrupt();
            RecycleCheck check = bin.check(folder, () -> false);
            assertTrue(Thread.currentThread().isInterrupted());
            assertTrue(System.nanoTime() - start < Duration.ofSeconds(30).toNanos(), "the check waited");
            RecycleResult recycle = bin.recycle(folder);
            assertTrue(Thread.currentThread().isInterrupted());

            assertEquals(new RecycleCheck.Unknown(WindowsRecycleBin.NO_ANSWER_IN_TIME), check);
            assertEquals(new RecycleResult.Failed("Windows has not finished moving the folder after 10 minutes. "
                    + "Look in the Recycle Bin and in " + folder.getParent() + " to see where it is."), recycle);
        } finally {
            Thread.interrupted();
        }
        assertTrue(Files.isDirectory(folder));
    }

    /** A failure on the recycle thread is an answer, never an exception. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aThreadThatCannotBeMadeIsAnAnswer() throws IOException {
        Path folder = projectFolder("NoThread");
        ThreadFactory broken = task -> {
            throw new IllegalStateException("no threads today");
        };
        WindowsRecycleBin bin = bin(broken);

        RecycleCheck check = bin.check(folder, () -> false);
        RecycleResult recycle = bin.recycle(folder);

        assertTrue(assertInstanceOf(RecycleCheck.Unknown.class, check).reason().contains("no threads today"));
        assertTrue(assertInstanceOf(RecycleResult.Failed.class, recycle).message().contains("no threads today"));
        assertTrue(Files.isDirectory(folder));
    }

    @Test
    void theDefaultThreadsArePlatformDaemonsNamedForTheRecycleBin() throws Exception {
        CompletableFuture<Thread> ran = new CompletableFuture<>();
        Thread thread = WindowsRecycleBin.RECYCLE_THREADS.newThread(() -> ran.complete(Thread.currentThread()));

        assertEquals("vf-recycle", thread.getName());
        assertTrue(thread.isDaemon());
        assertFalse(thread.isVirtual());
        thread.start();
        assertEquals(thread, ran.get(10, TimeUnit.SECONDS));
    }

    @Test
    void reasonsAreRequired() {
        assertThrows(NullPointerException.class, () -> new RecycleCheck.NotRecyclable(null));
        assertThrows(NullPointerException.class, () -> new RecycleCheck.Unknown(null));
        assertThrows(NullPointerException.class, () -> new RecycleResult.NotRecyclable(null));
        assertThrows(NullPointerException.class, () -> new RecycleResult.Refused(null, true));
        assertThrows(NullPointerException.class, () -> new RecycleResult.Failed(null));
    }
}
