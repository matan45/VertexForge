package com.vertexforge.launcher.project;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.service.PathProbe;
import com.vertexforge.launcher.service.ProjectHealthService;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
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
import java.nio.file.Path;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.OptionalLong;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * AC (VK-1686): "A project whose editor lock is held shows Already open." Another PROCESS holds the lock as the
 * editor does ({@link LockHolderMain}); the probe sees it as held, names its pid, and changes nothing.
 */
@EnabledOnOs(OS.WINDOWS)
class EditorLockProbeChildProcessTest {

    @TempDir
    Path tempDir;

    @Test
    @Timeout(value = 90, unit = TimeUnit.SECONDS)
    void aFolderAnotherEditorHoldsIsAlreadyOpenUntilThatEditorLetsGo() throws Exception {
        Path root = TestSupport.real(tempDir);
        Path project = TestSupport.writeProject(root.resolve("Game"), "Game");
        Path lockFile = EditorLockProbe.lockFile(project.getParent());
        List<Duration> waits = new CopyOnWriteArrayList<>();
        EditorLockProbe probe = new EditorLockProbe(2, Duration.ofMillis(20), duration -> {
            waits.add(duration);
            Thread.sleep(duration);
        });

        Process holder = startHolder(lockFile);
        try {
            BufferedReader output = new BufferedReader(new InputStreamReader(holder.getInputStream(),
                    StandardCharsets.UTF_8));
            String line = CompletableFuture.supplyAsync(() -> readLine(output)).get(60, TimeUnit.SECONDS);
            assertTrue(line != null && line.startsWith(LockHolderMain.LOCKED), "the holder said: " + line);
            Map<String, String> before = TestSupport.treeSnapshot(root);

            EditorLockState state = probe.probe(project.getParent());

            assertEquals(new EditorLockState.Held(OptionalLong.of(holder.pid())), state);
            assertEquals(List.of(Duration.ofMillis(20), Duration.ofMillis(20)), waits, "a held lock is retried");

            ProjectFileCheck check;
            try (ProjectHealthService health = new ProjectHealthService(PathProbe.system(), probe::probe,
                    Duration.ofSeconds(30))) {
                check = health.check(project, true).get(60, TimeUnit.SECONDS);
            }
            ProjectFileCheck.Readable readable = assertInstanceOf(ProjectFileCheck.Readable.class, check);
            assertInstanceOf(EditorLockState.Held.class, readable.lock());

            ProjectStatus status = ProjectHealthService.evaluate(check, null, null,
                    ProjectHealthService.FolderEditor.NONE);
            assertEquals(ProjectStatus.Kind.ALREADY_OPEN, status.kind());
            assertTrue(status.reason().contains("(pid " + holder.pid() + ")"), status.reason());
            ProjectViewModel row = new ProjectViewModel(RecentEntry.create(UUID.randomUUID(), project, "Game",
                    Instant.EPOCH));
            row.setStatus(status);
            assertFalse(row.isOpenable(), "Open is disabled while another editor has the project");

            // The probe never wrote to the lock file, nor created anything.
            assertEquals(before, TestSupport.treeSnapshot(root));
        } finally {
            holder.getOutputStream().close();
            assertTrue(holder.waitFor(30, TimeUnit.SECONDS), "the holder did not exit");
        }
        assertEquals(0, holder.exitValue());

        // The editor closed the project: the lock is free again (the file stays, as the editor leaves it).
        assertInstanceOf(EditorLockState.Free.class, probe.probe(project.getParent()));
    }

    private static String readLine(BufferedReader reader) {
        try {
            return reader.readLine();
        } catch (IOException e) {
            return "<output closed: " + e + ">";
        }
    }

    /** A child JVM with just this test's classes (the holder needs nothing else). */
    private static Process startHolder(Path lockFile) throws IOException, URISyntaxException {
        String classPath = Path.of(LockHolderMain.class.getProtectionDomain().getCodeSource().getLocation().toURI())
                .toString();
        Path java = Path.of(System.getProperty("java.home"), "bin", "java.exe");
        ProcessBuilder builder = new ProcessBuilder(java.toString(),
                // Keeps Surefire's backstop: nothing in the child can reach the real ~/.vertexforge.
                "-Duser.home=" + System.getProperty("user.home"),
                "-cp", classPath, LockHolderMain.class.getName(), lockFile.toString());
        builder.redirectErrorStream(true);
        return builder.start();
    }
}
