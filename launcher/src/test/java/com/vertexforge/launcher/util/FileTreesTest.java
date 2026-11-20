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

    /** VK-1691: a library checked out read-only must still go when the update deletes its old copy. */
    @Test
    void aReadOnlyFileAndFolderAreDeletedOnlyWhenAskedToClearTheFlag() throws IOException {
        assumeDosAttributes();
        Path root = Files.createDirectories(tempDir.resolve("root/locked"));
        Path file = Files.writeString(root.resolve("ReadOnly.mt"), "x");
        Files.writeString(tempDir.resolve("root/plain.mt"), "y");
        Files.setAttribute(file, "dos:readonly", true);
        Files.setAttribute(root, "dos:readonly", true);

        List<IOException> failures = new ArrayList<>();
        FileTrees.deleteTree(tempDir.resolve("root"), failures);

        assertFalse(failures.isEmpty(), "deleteTree leaves the user's protection alone");
        assertTrue(Files.exists(file), "the read-only file stays");
        assertFalse(Files.exists(tempDir.resolve("root/plain.mt")), "the rest is still deleted");
        assertTrue(Files.readAttributes(file, java.nio.file.attribute.DosFileAttributes.class).isReadOnly());

        List<IOException> clearing = new ArrayList<>();
        FileTrees.deleteTreeClearingReadOnly(tempDir.resolve("root"), clearing);

        assertEquals(List.of(), clearing);
        assertFalse(Files.exists(tempDir.resolve("root"), LinkOption.NOFOLLOW_LINKS));
    }

    @Test
    void aReadOnlyFolderAloneIsDeletedWhenAskedToClearTheFlag() throws IOException {
        assumeDosAttributes();
        Path folder = Files.createDirectories(tempDir.resolve("empty-locked"));
        Files.setAttribute(folder, "dos:readonly", true);

        List<IOException> failures = new ArrayList<>();
        FileTrees.deleteTreeClearingReadOnly(folder, failures);

        assertEquals(List.of(), failures);
        assertFalse(Files.exists(folder, LinkOption.NOFOLLOW_LINKS));
    }

    @Test
    void clearingTheFlagNeverReachesThroughAJunction() throws IOException {
        assumeDosAttributes();
        Path elsewhere = Files.createDirectory(tempDir.resolve("elsewhere"));
        Path theirs = Files.writeString(elsewhere.resolve("theirs.mt"), "keep");
        Files.setAttribute(theirs, "dos:readonly", true);
        Path root = Files.createDirectory(tempDir.resolve("root"));
        Path junction = TestLinks.junction(root.resolve("link"), elsewhere);
        try {
            List<IOException> failures = new ArrayList<>();
            FileTrees.deleteTreeClearingReadOnly(root, failures);

            assertEquals(List.of(), failures);
            assertFalse(Files.exists(root, LinkOption.NOFOLLOW_LINKS));
            assertTrue(Files.exists(theirs));
            assertTrue(Files.readAttributes(theirs, java.nio.file.attribute.DosFileAttributes.class).isReadOnly(),
                    "what the junction leads to keeps its flag");
        } finally {
            TestLinks.removeLink(junction);
            Files.setAttribute(theirs, "dos:readonly", false);
        }
    }

    private void assumeDosAttributes() {
        org.junit.jupiter.api.Assumptions.assumeTrue(
                Files.getFileAttributeView(tempDir, java.nio.file.attribute.DosFileAttributeView.class) != null,
                "this file system has no DOS read-only flag");
    }

    @Test
    void aRealFolderIsItsOwnAndARootIsNot() throws IOException {
        Path folder = Files.createDirectory(tempDir.resolve("folder"));

        assertTrue(FileTrees.isOwnFolder(folder));
        assertFalse(FileTrees.isOwnFolder(folder.getRoot()), "a root has no parent to live in");
        assertFalse(FileTrees.isOwnFolder(tempDir.resolve("missing")), "what cannot be resolved counts as elsewhere");
    }
}
