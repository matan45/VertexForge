package com.vertexforge.launcher.project;

import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.io.TempDir;

import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;

/** The editor-lock probe (VK-1686) inside this process; another editor's lock is in the child-process test. */
class EditorLockProbeTest {

    @TempDir
    Path tempDir;

    /** No waits between retries. */
    private final EditorLockProbe probe = new EditorLockProbe(3, Duration.ZERO, duration -> {
    });

    private Path projectFolder(String name) {
        return TestSupport.writeProject(tempDir.resolve(name), name).getParent();
    }

    /** A lock file as an editor that has closed the project leaves it. */
    private static Path leftLockFile(Path folder) throws Exception {
        Path lock = EditorLockProbe.lockFile(folder);
        Files.createDirectories(lock.getParent());
        return Files.writeString(lock, "{\"pid\":4242}\n");
    }

    @Test
    void aFolderNoEditorEverOpenedIsFreeAndGetsNoStateFolder() {
        Path folder = projectFolder("Fresh");
        Map<String, String> before = TestSupport.treeSnapshot(tempDir);

        assertEquals(EditorLockState.FREE, probe.probe(folder));

        assertEquals(before, TestSupport.treeSnapshot(tempDir));
        assertFalse(Files.exists(folder.resolve(EditorLockProbe.STATE_FOLDER)));
    }

    @Test
    void aStateFolderWithoutALockFileIsFree() throws Exception {
        Path folder = projectFolder("Folder");
        Files.createDirectories(folder.resolve(EditorLockProbe.STATE_FOLDER));

        assertEquals(EditorLockState.FREE, probe.probe(folder));
        assertFalse(Files.exists(EditorLockProbe.lockFile(folder)));
    }

    @Test
    void aLockFileNobodyHoldsIsFreeAndStaysAsItWas() throws Exception {
        Path folder = projectFolder("Closed");
        Path lock = leftLockFile(folder);
        byte[] bytes = Files.readAllBytes(lock);
        Map<String, String> before = TestSupport.treeSnapshot(tempDir);

        assertEquals(EditorLockState.FREE, probe.probe(folder));

        assertEquals(before, TestSupport.treeSnapshot(tempDir));
        assertEquals(new String(bytes), new String(Files.readAllBytes(lock)));
    }

    @Test
    void aLockPathThatIsAFolderCannotBeChecked() throws Exception {
        Path folder = projectFolder("Odd");
        Files.createDirectories(EditorLockProbe.lockFile(folder));

        assertInstanceOf(EditorLockState.Unknown.class, probe.probe(folder));
    }

    @Test
    void aLockThisProcessHoldsIsNeverTakenForAnotherEditor() throws Exception {
        Path folder = projectFolder("Mine");
        Path lock = leftLockFile(folder);

        try (FileChannel channel = FileChannel.open(lock, StandardOpenOption.READ, StandardOpenOption.WRITE);
             FileLock ignored = channel.lock(EditorLockProbe.LOCK_OFFSET, 1, false)) {
            // Java file locks belong to the JVM: the probe cannot tell, so it says so instead of "held".
            assertInstanceOf(EditorLockState.Unknown.class, probe.probe(folder));
        }
        assertEquals(EditorLockState.FREE, probe.probe(folder));
    }

    @Test
    @Timeout(value = 60, unit = TimeUnit.SECONDS)
    void probesOfOneFolderFromSeveralThreadsNeverGetInEachOthersWay() throws Exception {
        // Two project files of one folder are checked at the same time, so their probes share the lock file.
        Path folder = projectFolder("Shared");
        leftLockFile(folder);
        ExecutorService pool = Executors.newFixedThreadPool(2);
        try {
            List<Callable<List<EditorLockState>>> tasks = new ArrayList<>();
            for (int t = 0; t < 2; t++) {
                tasks.add(() -> {
                    List<EditorLockState> states = new ArrayList<>();
                    for (int i = 0; i < 200; i++) {
                        states.add(probe.probe(folder));
                    }
                    return states;
                });
            }
            for (Future<List<EditorLockState>> result : pool.invokeAll(tasks)) {
                for (EditorLockState state : result.get()) {
                    assertEquals(EditorLockState.FREE, state);
                }
            }
        } finally {
            pool.shutdownNow();
        }
    }
}
