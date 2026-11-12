package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.ProjectStatus.Kind;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class PathProbeTest {

    @TempDir
    Path tempDir;

    private final PathProbe probe = PathProbe.system();

    @Test
    void aReadableProjectFileIsOk() throws IOException {
        Path project = Files.writeString(tempDir.resolve("Game.vfproj"), "{}");

        assertEquals(Kind.OK, probe.probe(project).kind());
    }

    @Test
    void aDeletedProjectFileIsMissing() {
        ProjectStatus status = probe.probe(tempDir.resolve("gone").resolve("Gone.vfproj"));

        assertEquals(Kind.MISSING, status.kind());
        assertEquals("The project file was not found", status.reason());
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aProjectOnAnAbsentDriveIsMissingAndSaysWhichDrive() {
        Optional<Path> absentRoot = TestSupport.absentDriveRoot();
        assumeTrue(absentRoot.isPresent(), "every drive letter from D to Z is in use");

        ProjectStatus status = probe.probe(absentRoot.get().resolve("Projects").resolve("Game.vfproj"));

        assertEquals(Kind.MISSING, status.kind());
        assertTrue(status.reason().contains(absentRoot.get().toString()), status.reason());
    }

    @Test
    void aFolderNamedLikeAProjectIsUnreadable() throws IOException {
        Path folder = Files.createDirectories(tempDir.resolve("Folder.vfproj"));

        assertEquals(Kind.UNREADABLE, probe.probe(folder).kind());
    }

    @Test
    void aFileWithAnotherExtensionIsUnreadable() throws IOException {
        Path other = Files.writeString(tempDir.resolve("Game.txt"), "{}");

        assertEquals(Kind.UNREADABLE, probe.probe(other).kind());
    }
}
