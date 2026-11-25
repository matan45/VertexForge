package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectFileCheck;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/** The real file system: the project file's probe, and (through the health check) its folder's editor lock. */
class PathProbeTest {

    @TempDir
    Path tempDir;

    private final PathProbe probe = PathProbe.system();

    /** The real probes, without the waits between lock retries. */
    private final ProjectHealthService health = new ProjectHealthService(probe,
            new EditorLockProbe(3, Duration.ZERO, duration -> {
            })::probe, Duration.ofSeconds(10));

    @AfterEach
    void tearDown() {
        health.close();
    }

    private ProjectFileCheck.Readable readable(Path project) {
        return assertInstanceOf(ProjectFileCheck.Readable.class, probe.probe(project));
    }

    @Test
    void aProjectTheEditorAcceptsIsReadAndItsLockIsLeftToTheHealthCheck() {
        Path project = TestSupport.writeProject(tempDir.resolve("Game"), "Game");

        ProjectFileCheck.Readable readable = readable(project);

        assertEquals("Game", readable.file().projectName());
        assertNull(readable.file().structureProblem());
        assertEquals(EditorLockState.SKIPPED, readable.lock());
    }

    @Test
    void theExtensionMayBeInAnyLetterCase() throws IOException {
        Path project = Files.writeString(tempDir.resolve("Game.VFPROJ"),
                TestSupport.projectJson("Game", "1.0", "VertexForge 1.0.0"));

        readable(project);
    }

    @Test
    void aDeletedProjectFileIsMissing() {
        ProjectFileCheck check = probe.probe(tempDir.resolve("gone").resolve("Gone.vfproj"));

        assertEquals(new ProjectFileCheck.Missing("The project file was not found"), check);
    }

    @Test
    void aRenamedProjectFolderIsMissing() throws IOException {
        Path project = TestSupport.writeProject(tempDir.resolve("Game"), "Game");
        Files.move(project.getParent(), tempDir.resolve("Game Renamed"));

        assertInstanceOf(ProjectFileCheck.Missing.class, probe.probe(project));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aProjectOnAnAbsentDriveIsMissingAndSaysWhichDrive() {
        Optional<Path> absentRoot = TestSupport.absentDriveRoot();
        assumeTrue(absentRoot.isPresent(), "every drive letter from D to Z is in use");

        ProjectFileCheck check = probe.probe(absentRoot.get().resolve("Projects").resolve("Game.vfproj"));

        ProjectFileCheck.Missing missing = assertInstanceOf(ProjectFileCheck.Missing.class, check);
        assertTrue(missing.reason().contains(absentRoot.get().toString()), missing.reason());
    }

    @Test
    void aFolderNamedLikeAProjectIsInvalid() throws IOException {
        Path folder = Files.createDirectories(tempDir.resolve("Folder.vfproj"));

        assertEquals(new ProjectFileCheck.Invalid("Not a file"), probe.probe(folder));
    }

    @Test
    void aFileWithAnotherExtensionIsInvalid() throws IOException {
        Path other = Files.writeString(tempDir.resolve("Game.txt"), "{}");

        assertEquals(new ProjectFileCheck.Invalid("Not a .vfproj file"), probe.probe(other));
    }

    @Test
    void aFileThatIsNotJsonIsInvalidWithTheReadersReason() throws IOException {
        Path project = Files.writeString(tempDir.resolve("Broken.vfproj"), "{\"projectName\": ");

        ProjectFileCheck.Invalid invalid = assertInstanceOf(ProjectFileCheck.Invalid.class, probe.probe(project));
        assertTrue(invalid.reason().startsWith("Not valid JSON"), invalid.reason());
    }

    @Test
    void aFileTheEditorWouldRefuseIsStillReadAndSaysWhy() throws IOException {
        Path project = Files.writeString(tempDir.resolve("Bare.vfproj"), "{\"projectName\": \"Bare\"}");

        assertEquals("Invalid project file: missing or invalid 'schemaVersion' field",
                readable(project).file().structureProblem());
    }

    @Test
    void theHealthCheckFindsTheLockOfAFolderNoEditorHasOpenFree() throws Exception {
        Path project = TestSupport.writeProject(tempDir.resolve("Game"), "Game");

        ProjectFileCheck check = health.check(project, true).get(10, TimeUnit.SECONDS);

        assertInstanceOf(EditorLockState.Free.class, assertInstanceOf(ProjectFileCheck.Readable.class, check).lock());
    }

    /** VK-1686 gotcha: the probe must never create or modify files inside a project. */
    @Test
    void aHealthCheckCreatesAndChangesNothingInTheProject() throws Exception {
        Path project = TestSupport.writeProject(tempDir.resolve("Plain"), "Plain");
        Map<String, String> before = TestSupport.treeSnapshot(tempDir);

        health.check(project, true).get(10, TimeUnit.SECONDS);
        health.check(project, false).get(10, TimeUnit.SECONDS);

        assertEquals(before, TestSupport.treeSnapshot(tempDir));
        assertFalse(Files.exists(project.resolveSibling(EditorLockProbe.STATE_FOLDER)));
    }

    @Test
    void aHealthCheckOfAFolderAnEditorOnceOpenedChangesNothingEither() throws Exception {
        Path project = TestSupport.writeProject(tempDir.resolve("Opened"), "Opened");
        // As an editor leaves it after it closed: the lock file stays, with the pid of the last holder.
        Path lock = EditorLockProbe.lockFile(project.getParent());
        Files.createDirectories(lock.getParent());
        Files.writeString(lock, "{\"pid\":4242}\n");
        Map<String, String> before = TestSupport.treeSnapshot(tempDir);

        ProjectFileCheck check = health.check(project, true).get(10, TimeUnit.SECONDS);

        assertInstanceOf(EditorLockState.Free.class, assertInstanceOf(ProjectFileCheck.Readable.class, check).lock());
        assertEquals(before, TestSupport.treeSnapshot(tempDir));
    }
}
