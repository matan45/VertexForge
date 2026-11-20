package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.LockHolderMain;
import com.vertexforge.launcher.scriptlib.LegacyLibraryHashes;
import com.vertexforge.launcher.scriptlib.UpdateJournal;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.PreviewOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.RecoveryOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.UpdateOutcome;
import com.vertexforge.launcher.template.LibraryManifest;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * AC (VK-1691): "an editor that has the project open stops the update". Another PROCESS holds the editor's lock as the
 * editor does ({@link LockHolderMain}); the preview, the update and the recovery all refuse, and write nothing: no
 * backup, no {@code .partial}, no journal, no staging folder.
 */
@EnabledOnOs(OS.WINDOWS)
class ScriptLibraryUpdateLockTest {

    @TempDir
    Path tempDir;

    @Test
    @Timeout(value = 120, unit = TimeUnit.SECONDS)
    void nothingIsWrittenWhileAnEditorHasTheProjectOpen() throws Exception {
        Path root = TestSupport.real(tempDir);
        Path engineLib = root.resolve("engine/lib");
        write(engineLib.resolve("engine/Log.mt"), "public class Log {\n    // 1.1.0\n}\n");
        Path folder = root.resolve("Game");
        Path projectFile = TestSupport.writeProject(folder, "Game");
        Path scripts = folder.resolve("scripts");
        Path lib = scripts.resolve("lib");
        write(lib.resolve("engine/Log.mt"), "public class Log {\n}\n");
        Files.write(lib.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson("1.0.0",
                Map.of("engine/Log.mt", LibraryManifest.hash("public class Log {\n}\n"
                        .getBytes(StandardCharsets.UTF_8)))));
        Path lockFile = EditorLockProbe.lockFile(folder);
        ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(projectFile, ".", engineLib, "1.1.0");

        // The preview before the editor opens: this is what the user confirmed.
        String fingerprint = assertInstanceOf(PreviewOutcome.Ready.class,
                updater(new CopyOnWriteArrayList<>()).preview(request, JobMonitor.NONE)).preview().fingerprint();

        Process holder = startJava(LockHolderMain.class, lockFile.toString());
        try {
            BufferedReader output = new BufferedReader(new InputStreamReader(holder.getInputStream(),
                    StandardCharsets.UTF_8));
            String line = CompletableFuture.supplyAsync(() -> readLine(output)).get(60, TimeUnit.SECONDS);
            assertTrue(line != null && line.startsWith(LockHolderMain.LOCKED), "the holder said: " + line);
            Map<String, String> before = TestSupport.treeSnapshot(folder);
            List<Duration> waits = new CopyOnWriteArrayList<>();
            ScriptLibraryUpdater updater = updater(waits);

            PreviewOutcome preview = updater.preview(request, JobMonitor.NONE);
            PreviewOutcome.Refused refusedPreview = assertInstanceOf(PreviewOutcome.Refused.class, preview,
                    preview::toString);
            assertTrue(refusedPreview.reason().contains("open in the editor (process " + holder.pid() + ")"),
                    refusedPreview.reason());

            UpdateOutcome update = updater.update(request, fingerprint, false, JobMonitor.NONE);
            UpdateOutcome.Refused refusedUpdate = assertInstanceOf(UpdateOutcome.Refused.class, update,
                    update::toString);
            assertTrue(refusedUpdate.reason().contains("open in the editor (process " + holder.pid() + ")"),
                    refusedUpdate.reason());
            assertEquals(List.of(Duration.ofMillis(5), Duration.ofMillis(5), Duration.ofMillis(5),
                    Duration.ofMillis(5)), waits, "the probe and the hold each retry a held lock twice");

            assertEquals(before, TestSupport.treeSnapshot(folder), "no backup, no .partial, no journal, nothing");

            // With an interrupted update's journal, the recovery refuses too and leaves everything as it is.
            Files.write(scripts.resolve(UpdateJournal.FILE_NAME), new UpdateJournal(1, "1a2b3c4d", "lib",
                    "lib.new-1a2b3c4d", "lib.old-1a2b3c4d", true, "0".repeat(64), null, "1.1.0", "t").toJson());
            Map<String, String> withJournal = TestSupport.treeSnapshot(folder);

            RecoveryOutcome recovery = updater.recover(projectFile, ".");
            assertInstanceOf(RecoveryOutcome.Refused.class, recovery, recovery::toString);
            assertEquals(withJournal, TestSupport.treeSnapshot(folder));
            Files.delete(scripts.resolve(UpdateJournal.FILE_NAME));
        } finally {
            holder.getOutputStream().close();
            assertTrue(holder.waitFor(30, TimeUnit.SECONDS), "the holder did not exit");
        }
        assertEquals(0, holder.exitValue());

        // The editor closed the project: the update goes ahead.
        UpdateOutcome after = updater(new CopyOnWriteArrayList<>()).update(request, fingerprint, false,
                JobMonitor.NONE);
        UpdateOutcome.Done done = assertInstanceOf(UpdateOutcome.Done.class, after, after::toString);
        assertTrue(Files.isRegularFile(done.backup()));
        assertFalse(Files.exists(scripts.resolve(UpdateJournal.FILE_NAME)));
    }

    @Test
    @Timeout(value = 120, unit = TimeUnit.SECONDS)
    void anUpdateIsRefusedWhileAnotherLauncherHoldsTheProject() throws Exception {
        Path root = TestSupport.real(tempDir);
        Path engineLib = root.resolve("engine/lib");
        write(engineLib.resolve("engine/Log.mt"), "public class Log {\n    // 1.1.0\n}\n");
        Path folder = root.resolve("Game");
        Path projectFile = TestSupport.writeProject(folder, "Game");
        Path lib = folder.resolve("scripts/lib");
        write(lib.resolve("engine/Log.mt"), "public class Log {\n}\n");
        ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(projectFile, ".", engineLib, "1.1.0");
        String fingerprint = assertInstanceOf(PreviewOutcome.Ready.class,
                updater(new CopyOnWriteArrayList<>()).preview(request, JobMonitor.NONE)).preview().fingerprint();

        Process other = startJava(com.vertexforge.launcher.project.HoldMain.class, folder.toString());
        try {
            BufferedReader output = new BufferedReader(new InputStreamReader(other.getInputStream(),
                    StandardCharsets.UTF_8));
            String line = CompletableFuture.supplyAsync(() -> readLine(output)).get(60, TimeUnit.SECONDS);
            assertEquals(com.vertexforge.launcher.project.HoldMain.HOLDING, line);
            Map<String, String> before = TestSupport.treeSnapshot(folder);

            UpdateOutcome update = updater(new CopyOnWriteArrayList<>()).update(request, fingerprint, false,
                    JobMonitor.NONE);

            UpdateOutcome.Refused refused = assertInstanceOf(UpdateOutcome.Refused.class, update, update::toString);
            assertEquals("Another VertexForge launcher is changing this project right now. Try again once it is "
                    + "done.", refused.reason());
            assertEquals(before, TestSupport.treeSnapshot(folder));
        } finally {
            other.getOutputStream().close();
            assertTrue(other.waitFor(30, TimeUnit.SECONDS), "the other launcher did not exit");
        }
        assertEquals(0, other.exitValue());
    }

    private static ScriptLibraryUpdater updater(List<Duration> waits) {
        EditorLockProbe locks = new EditorLockProbe(2, Duration.ofMillis(5), duration -> {
            waits.add(duration);
            Thread.sleep(duration);
        });
        int[] ids = {0};
        return new ScriptLibraryUpdater(TestSupport.fixedClock(), target -> Long.MAX_VALUE, locks,
                DirectoryMover.SYSTEM, () -> new UUID(0x1a2b3c4d00000000L + ++ids[0], 0L),
                LegacyLibraryHashes::bundled, Duration.ZERO, ScriptLibraryUpdater.Checkpoint.NONE);
    }

    private static void write(Path file, String content) throws IOException {
        Files.createDirectories(file.getParent());
        Files.writeString(file, content, StandardCharsets.UTF_8);
    }

    private static String readLine(BufferedReader reader) {
        try {
            return reader.readLine();
        } catch (IOException e) {
            return "<output closed: " + e + ">";
        }
    }

    /** A child JVM with the test classes and the launcher's own classes, running {@code main}. */
    private static Process startJava(Class<?> main, String... args) throws IOException, URISyntaxException {
        String classPath = Path.of(main.getProtectionDomain().getCodeSource().getLocation().toURI())
                + java.io.File.pathSeparator
                + Path.of(EditorLockProbe.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        Path java = Path.of(System.getProperty("java.home"), "bin", "java.exe");
        List<String> command = new ArrayList<>(List.of(java.toString(),
                // Keeps Surefire's backstop: nothing in the child can reach the real ~/.vertexforge.
                "-Duser.home=" + System.getProperty("user.home"), "-cp", classPath, main.getName()));
        command.addAll(List.of(args));
        return new ProcessBuilder(command).redirectErrorStream(true).start();
    }
}
