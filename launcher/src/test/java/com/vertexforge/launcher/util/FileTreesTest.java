package com.vertexforge.launcher.util;

import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link FileTrees}, moved out of ProjectCreator (VK-1690); ProjectCreatorTest covers its use there. */
class FileTreesTest {

    @TempDir
    Path tempDir;

    @Test
    void deletesAFolderWithEverythingInIt() throws IOException {
        Path root = Files.createDirectories(tempDir.resolve("root/a/b"));
        Files.writeString(root.resolve("deep.txt"), "x");
        Files.writeString(tempDir.resolve("root/top.txt"), "y");
        Files.createDirectories(tempDir.resolve("root/empty"));
        List<IOException> failures = new ArrayList<>();

        FileTrees.deleteTree(tempDir.resolve("root"), failures);

        assertEquals(List.of(), failures);
        assertFalse(Files.exists(tempDir.resolve("root"), LinkOption.NOFOLLOW_LINKS));
    }

    @Test
    void aMissingPathIsNoFailure() {
        List<IOException> failures = new ArrayList<>();

        FileTrees.deleteTree(tempDir.resolve("nothing-here"), failures);

        assertEquals(List.of(), failures);
    }

    @Test
    void aJunctionInsideIsDeletedButWhatItLeadsToIsKept() throws IOException {
        Path elsewhere = Files.createDirectory(tempDir.resolve("elsewhere"));
        Files.writeString(elsewhere.resolve("keep.txt"), "theirs");
        Path root = Files.createDirectory(tempDir.resolve("root"));
        Path junction = TestLinks.junction(root.resolve("link"), elsewhere);
        try {
            assertFalse(FileTrees.isOwnFolder(junction), "a junction leads elsewhere");
            List<IOException> failures = new ArrayList<>();

            FileTrees.deleteTree(root, failures);

            assertEquals(List.of(), failures);
            assertFalse(Files.exists(root, LinkOption.NOFOLLOW_LINKS));
            assertEquals(Set.of("keep.txt"), TestSupport.fileNames(elsewhere));
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    @Test
    void aSymbolicLinkInsideIsDeletedButWhatItLeadsToIsKept() throws IOException {
        Path elsewhere = Files.writeString(tempDir.resolve("elsewhere.txt"), "theirs");
        Path root = Files.createDirectory(tempDir.resolve("root"));
        TestLinks.symlink(root.resolve("link.txt"), elsewhere);
        List<IOException> failures = new ArrayList<>();

        FileTrees.deleteTree(root, failures);

        assertEquals(List.of(), failures);
        assertFalse(Files.exists(root, LinkOption.NOFOLLOW_LINKS));
        assertEquals("theirs", Files.readString(elsewhere));
    }

    @Test
    void aRealFolderIsItsOwnAndARootIsNot() throws IOException {
        Path folder = Files.createDirectory(tempDir.resolve("folder"));

        assertTrue(FileTrees.isOwnFolder(folder));
        assertFalse(FileTrees.isOwnFolder(folder.getRoot()), "a root has no parent to live in");
        assertFalse(FileTrees.isOwnFolder(tempDir.resolve("missing")), "what cannot be resolved counts as elsewhere");
    }
}
