package com.vertexforge.launcher.project;

import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link EditorLockProbe#hold} (VK-1691): the shared lock a script-library update keeps on the editor's byte, so that
 * no editor can open the project meanwhile, and how the other checks of this launcher wait for it.
 */
class EditorLockProbeHoldTest {

    @TempDir
    Path tempDir;

    private static EditorLockProbe probe() {
        return new EditorLockProbe(0, Duration.ZERO, duration -> {
        });
    }

    private static EditorLockProbe.Hold holding(EditorLockProbe.HoldResult result) {
        return assertInstanceOf(EditorLockProbe.HoldResult.Holding.class, result, result::toString).hold();
    }

    @Test
    void aHoldCreatesTheStateFolderAndAnEmptyLockFileWhenMissing() throws IOException {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        Path lockFile = EditorLockProbe.lockFile(folder);
        assertFalse(Files.exists(lockFile.getParent()));

        try (EditorLockProbe.Hold ignored = holding(probe().hold(folder))) {
            assertTrue(Files.isRegularFile(lockFile));
            assertEquals(0, Files.size(lockFile), "the editor writes its pid; the hold writes nothing");
        }

        assertTrue(Files.isRegularFile(lockFile), "the file stays, as the editor leaves it");
        assertInstanceOf(EditorLockState.Free.class, probe().probe(folder));
    }

    @Test
    void aHoldUsesALockFileThatIsThereAndKeepsItsContent() throws IOException {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        Path lockFile = EditorLockProbe.lockFile(folder);
        Files.createDirectories(lockFile.getParent());
        Files.writeString(lockFile, "{\"pid\":42}\n");

        try (EditorLockProbe.Hold ignored = holding(probe().hold(folder))) {
            assertEquals("{\"pid\":42}\n", Files.readString(lockFile));
        }
        assertEquals("{\"pid\":42}\n", Files.readString(lockFile));
    }

    @Test
    void aStateFolderThatIsAFileGivesUnknownInsteadOfThrowing() throws IOException {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        Files.writeString(folder.resolve(EditorLockProbe.STATE_FOLDER), "not a folder");

        EditorLockProbe.HoldResult result = probe().hold(folder);

        EditorLockProbe.HoldResult.NotHeld notHeld = assertInstanceOf(EditorLockProbe.HoldResult.NotHeld.class, result,
                result::toString);
        assertInstanceOf(EditorLockState.Unknown.class, notHeld.state());
    }

    /**
     * This JVM already locks the editor's byte through another channel (the same folder under another spelling of its
     * path, which the local queue cannot see): Java throws OverlappingFileLockException, and the hold retries it as a
     * held lock instead of failing at once.
     */
    @Test
    void aByteThisJvmLocksThroughAnotherChannelIsRetriedLikeAHeldLock() throws IOException {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        Path lockFile = EditorLockProbe.lockFile(folder);
        Files.createDirectories(lockFile.getParent());
        Files.writeString(lockFile, "{\"pid\":7}");
        List<Duration> waits = new ArrayList<>();
        EditorLockProbe probe = new EditorLockProbe(2, Duration.ofMillis(3), waits::add);

        try (java.nio.channels.FileChannel other = java.nio.channels.FileChannel.open(lockFile,
                java.nio.file.StandardOpenOption.READ, java.nio.file.StandardOpenOption.WRITE);
             java.nio.channels.FileLock ignored = other.lock(EditorLockProbe.LOCK_OFFSET, 1, true)) {
            EditorLockProbe.HoldResult result = probe.hold(folder);

            EditorLockProbe.HoldResult.NotHeld notHeld = assertInstanceOf(EditorLockProbe.HoldResult.NotHeld.class,
                    result, result::toString);
            assertEquals(new EditorLockState.Held(java.util.OptionalLong.of(7)), notHeld.state());
            assertEquals(List.of(Duration.ofMillis(3), Duration.ofMillis(3)), waits, "retried as a held lock");
        }

        try (EditorLockProbe.Hold ignored = holding(probe.hold(folder))) {
            assertEquals(2, waits.size(), "free again: no more waits");
        }
    }

    @Test
    void closingTwiceIsHarmless() throws IOException {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        EditorLockProbe.Hold hold = holding(probe().hold(folder));

        hold.close();
        hold.close();

        assertInstanceOf(EditorLockState.Free.class, probe().probe(folder));
    }

    @Test
    @Timeout(value = 60, unit = TimeUnit.SECONDS)
    void aProbeOfTheSameFolderOnAnotherThreadWaitsForTheHoldThenFindsItFree() throws Exception {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        ExecutorService other = Executors.newSingleThreadExecutor();
        try {
            Future<EditorLockState> probed;
            try (EditorLockProbe.Hold ignored = holding(probe().hold(folder))) {
                probed = other.submit(() -> probe().probe(folder));

                // While the hold lasts, the other check queues behind it instead of answering Held or Unknown.
                assertThrows(TimeoutException.class, () -> probed.get(500, TimeUnit.MILLISECONDS));
            }

            assertInstanceOf(EditorLockState.Free.class, probed.get(30, TimeUnit.SECONDS));
        } finally {
            other.shutdownNow();
        }
    }

    @Test
    @Timeout(value = 60, unit = TimeUnit.SECONDS)
    void aSecondHoldOnAnotherThreadWaitsForTheFirst() throws Exception {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        ExecutorService other = Executors.newSingleThreadExecutor();
        try {
            Future<Boolean> second;
            try (EditorLockProbe.Hold ignored = holding(probe().hold(folder))) {
                second = other.submit(() -> {
                    // Taken and closed on its own thread, as the hold requires.
                    EditorLockProbe.HoldResult result = probe().hold(folder);
                    if (result instanceof EditorLockProbe.HoldResult.Holding holding) {
                        holding.hold().close();
                        return true;
                    }
                    return false;
                });
                assertThrows(TimeoutException.class, () -> second.get(500, TimeUnit.MILLISECONDS));
            }

            assertTrue(second.get(30, TimeUnit.SECONDS));
        } finally {
            other.shutdownNow();
        }
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    @Timeout(value = 120, unit = TimeUnit.SECONDS)
    void anEditorCannotTakeItsLockWhileTheHoldLastsAndCanAfterwards() throws Exception {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        Path lockFile = EditorLockProbe.lockFile(folder);

        try (EditorLockProbe.Hold ignored = holding(probe().hold(folder))) {
            assertEquals(LockAttemptMain.HELD, attempt(lockFile), "the editor's byte");
            assertEquals(LockAttemptMain.HELD, attempt(lockFile, "launcher"), "the launcher's byte");
            assertEquals(0, Files.size(lockFile), "nothing is written into the lock file");
        }

        assertEquals(LockAttemptMain.FREE, attempt(lockFile));
        assertEquals(LockAttemptMain.FREE, attempt(lockFile, "launcher"));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    @Timeout(value = 120, unit = TimeUnit.SECONDS)
    void aSecondLauncherCannotHoldAFolderAnotherLauncherHolds() throws Exception {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        Process other = startJava(HoldMain.class, folder.toString());
        try {
            java.io.BufferedReader output = new java.io.BufferedReader(new java.io.InputStreamReader(
                    other.getInputStream(), StandardCharsets.UTF_8));
            String line = CompletableFuture.supplyAsync(() -> {
                try {
                    return output.readLine();
                } catch (IOException e) {
                    return "<output closed: " + e + ">";
                }
            }).get(60, TimeUnit.SECONDS);
            assertEquals(HoldMain.HOLDING, line);

            EditorLockProbe.HoldResult result = probe().hold(folder);

            EditorLockProbe.HoldResult.NotHeld notHeld = assertInstanceOf(EditorLockProbe.HoldResult.NotHeld.class,
                    result, result::toString);
            assertEquals(new EditorLockState.Unknown(EditorLockProbe.HELD_BY_LAUNCHER), notHeld.state());
            assertInstanceOf(EditorLockState.Free.class, probe().probe(folder),
                    "a probe asks about editors only: another launcher's shared hold does not count");
            assertEquals(LockAttemptMain.HELD, attempt(EditorLockProbe.lockFile(folder)),
                    "and no editor can open the project meanwhile");
        } finally {
            other.getOutputStream().close();
            assertTrue(other.waitFor(30, TimeUnit.SECONDS), "the other launcher did not exit");
        }
        assertEquals(0, other.exitValue());

        try (EditorLockProbe.Hold ignored = holding(probe().hold(folder))) {
            assertTrue(Files.exists(EditorLockProbe.lockFile(folder)));
        }
    }

    /** VK-1695: a probe cannot see another launcher's hold; probeLauncher can, and so a delete or copy waits. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    @Timeout(value = 120, unit = TimeUnit.SECONDS)
    void probeLauncherSeesAnotherLaunchersHold() throws Exception {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));
        Process other = startJava(HoldMain.class, folder.toString());
        try {
            java.io.BufferedReader output = new java.io.BufferedReader(new java.io.InputStreamReader(
                    other.getInputStream(), StandardCharsets.UTF_8));
            String line = CompletableFuture.supplyAsync(() -> {
                try {
                    return output.readLine();
                } catch (IOException e) {
                    return "<output closed: " + e + ">";
                }
            }).get(60, TimeUnit.SECONDS);
            assertEquals(HoldMain.HOLDING, line);

            assertInstanceOf(EditorLockState.Free.class, probe().probe(folder), "the editor's byte reads as free");
            assertEquals(new EditorLockState.Unknown(EditorLockProbe.HELD_BY_LAUNCHER), probe().probeLauncher(folder));
        } finally {
            other.getOutputStream().close();
            assertTrue(other.waitFor(30, TimeUnit.SECONDS), "the other launcher did not exit");
        }
        assertEquals(EditorLockState.FREE, probe().probeLauncher(folder));
    }

    @Test
    void probeLauncherSeesThisLaunchersOwnHold() throws IOException {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));

        try (EditorLockProbe.Hold ignored = holding(probe().hold(folder))) {
            assertEquals(new EditorLockState.Unknown(EditorLockProbe.HELD_BY_LAUNCHER), probe().probeLauncher(folder));
        }
        assertEquals(EditorLockState.FREE, probe().probeLauncher(folder));
    }

    @Test
    void probeLauncherCreatesNothing() throws IOException {
        Path folder = Files.createDirectories(TestSupport.real(tempDir).resolve("Game"));

        assertEquals(EditorLockState.FREE, probe().probeLauncher(folder));
        assertFalse(Files.exists(folder.resolve(EditorLockProbe.STATE_FOLDER)), "a probe never creates the folder");
    }

    /** What a second process gets when it asks for the editor's (or with "launcher", the launcher's) exclusive byte. */
    static String attempt(Path lockFile, String... which) throws IOException, URISyntaxException,
            InterruptedException, java.util.concurrent.ExecutionException, TimeoutException {
        List<String> args = new ArrayList<>(List.of(lockFile.toString()));
        args.addAll(List.of(which));
        Process child = startJava(LockAttemptMain.class, args.toArray(String[]::new));
        try {
            String output = CompletableFuture.supplyAsync(() -> {
                try {
                    return new String(child.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
                } catch (IOException e) {
                    return "<output closed: " + e + ">";
                }
            }).get(60, TimeUnit.SECONDS);
            assertTrue(child.waitFor(30, TimeUnit.SECONDS), "the child did not exit");
            assertEquals(0, child.exitValue(), output);
            return output.trim();
        } finally {
            child.destroyForcibly();
        }
    }

    /** A child JVM with the test classes and the launcher's own classes, running {@code main}. */
    static Process startJava(Class<?> main, String... args) throws IOException, URISyntaxException {
        String classPath = Path.of(main.getProtectionDomain().getCodeSource().getLocation().toURI())
                + java.io.File.pathSeparator
                + Path.of(EditorLockProbe.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        Path javaExe = Path.of(System.getProperty("java.home"), "bin", TestSupport.isWindows() ? "java.exe" : "java");
        List<String> command = new ArrayList<>(List.of(javaExe.toString(),
                // Keeps Surefire's backstop: nothing in the child can reach the real ~/.vertexforge.
                "-Duser.home=" + System.getProperty("user.home"), "-cp", classPath, main.getName()));
        command.addAll(List.of(args));
        return new ProcessBuilder(command).redirectErrorStream(true).start();
    }
}
