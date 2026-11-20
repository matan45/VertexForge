package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.util.TestLinks;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.NotDirectoryException;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class FolderTreeTest {

    private static final Predicate<String> NOTHING = relative -> false;

    @TempDir
    Path tempDir;

    private static Path write(Path file, String content) throws IOException {
        Files.createDirectories(file.getParent());
        return Files.writeString(file, content);
    }

    private static List<String> relatives(FolderTree tree) {
        return tree.files().stream().map(FolderTree.FileEntry::relative).toList();
    }

    @Test
    void listsEveryFolderAndFileSortedWithEmptyFoldersKept() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        write(root.resolve("b.txt"), "bb");
        write(root.resolve("a/x.txt"), "x");
        write(root.resolve("a/deeper/y.txt"), "yyy");
        Files.createDirectories(root.resolve("a/empty"));
        Files.createDirectories(root.resolve("z-empty"));

        FolderTree tree = FolderTree.scan(root, NOTHING, JobMonitor.NONE);

        assertEquals(root, tree.root());
        assertEquals(List.of("a", "a/deeper", "a/empty", "z-empty"), tree.folders());
        assertEquals(List.of("a/deeper/y.txt", "a/x.txt", "b.txt"), relatives(tree));
        assertEquals(6, tree.totalBytes());
        assertEquals("a/deeper/y.txt".length(), tree.deepestRelativeLength());
        assertEquals(List.of(), tree.notes());
    }

    @Test
    void eachFileCarriesItsPathSizeAndModificationTime() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        Path file = write(root.resolve("sub/data.bin"), "12345");
        FileTime time = FileTime.from(Instant.parse("2025-05-06T07:08:09.123Z"));
        Files.setLastModifiedTime(file, time);

        FolderTree.FileEntry entry = FolderTree.scan(root, NOTHING, JobMonitor.NONE).files().getFirst();

        assertEquals(file, entry.path());
        assertEquals("sub/data.bin", entry.relative());
        assertEquals(5, entry.size());
        assertEquals(time, entry.lastModified());
    }

    @Test
    void aFolderCanBeTheDeepestEntry() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        write(root.resolve("a.txt"), "");
        Files.createDirectories(root.resolve("long/folder/name"));

        FolderTree tree = FolderTree.scan(root, NOTHING, JobMonitor.NONE);

        assertEquals("long/folder/name".length(), tree.deepestRelativeLength());
        assertEquals(0, FolderTree.scan(Files.createDirectory(tempDir.resolve("empty")), NOTHING, JobMonitor.NONE)
                .deepestRelativeLength());
    }

    @Test
    void hiddenFilesAreTaken() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        Path hidden = write(root.resolve("secret.txt"), "s");
        if (TestSupport.isWindows()) {
            Files.setAttribute(hidden, "dos:hidden", true);
            assertTrue(Files.isHidden(hidden));
        }
        write(root.resolve(".dotfile"), "d");

        assertEquals(List.of(".dotfile", "secret.txt"), relatives(FolderTree.scan(root, NOTHING, JobMonitor.NONE)));
    }

    @Test
    void exclusionsIgnoreCaseAndAnExcludedFolderIsNeverEntered() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("Game"));
        write(root.resolve("Game.vfproj"), "{}");
        write(root.resolve(".VertexForge/editor.lock"), "{\"pid\":1}");
        write(root.resolve("Assets/Scripts/Compiled/scripts.mtcLib"), "compiled");
        write(root.resolve("Assets/Scripts/CompiledOld/keep.txt"), "not excluded: only the exact folder is");
        write(root.resolve("Assets/Scripts/game/Main.mt"), "source");
        Predicate<String> excluded = ProjectContent.of(root.resolve("Game.vfproj"), "assets").excluded();
        List<String> asked = new ArrayList<>();

        FolderTree tree = FolderTree.scan(root, relative -> {
            asked.add(relative);
            return excluded.test(relative);
        }, JobMonitor.NONE);

        assertEquals(List.of("Assets/Scripts/CompiledOld/keep.txt", "Assets/Scripts/game/Main.mt", "Game.vfproj"),
                relatives(tree));
        assertEquals(List.of("Assets", "Assets/Scripts", "Assets/Scripts/CompiledOld", "Assets/Scripts/game"),
                tree.folders());
        assertTrue(asked.contains(".VertexForge") && asked.contains("Assets/Scripts/Compiled"));
        assertFalse(asked.stream().anyMatch(relative -> relative.startsWith(".VertexForge/")
                || relative.startsWith("Assets/Scripts/Compiled/")), "excluded folders are not entered: " + asked);
        assertEquals(List.of(), tree.notes(), "an exclusion is no surprise, so it gets no note");
    }

    @Test
    void symbolicLinksAreLeftOutWithANote() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        write(root.resolve("real.txt"), "r");
        Path elsewhere = Files.createDirectory(tempDir.resolve("elsewhere"));
        write(elsewhere.resolve("outside.txt"), "o");
        TestLinks.symlink(root.resolve("file-link.txt"), elsewhere.resolve("outside.txt"));
        TestLinks.symlink(root.resolve("folder-link"), elsewhere);

        FolderTree tree = FolderTree.scan(root, NOTHING, JobMonitor.NONE);

        assertEquals(List.of("real.txt"), relatives(tree));
        assertEquals(List.of(), tree.folders());
        assertEquals(List.of("file-link.txt is a link and was left out.", "folder-link is a link and was left out."),
                tree.notes());
        assertEquals(1, tree.totalBytes());
    }

    @Test
    void aJunctionToAnotherFolderIsLeftOutWithANote() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        write(root.resolve("real.txt"), "r");
        Path elsewhere = Files.createDirectory(tempDir.resolve("elsewhere"));
        write(elsewhere.resolve("outside.txt"), "o");
        Path junction = TestLinks.junction(Files.createDirectory(root.resolve("sub")).resolve("junction"), elsewhere);
        try {
            FolderTree tree = FolderTree.scan(root, NOTHING, JobMonitor.NONE);

            assertEquals(List.of("real.txt"), relatives(tree));
            assertEquals(List.of("sub"), tree.folders());
            assertEquals(List.of("sub/junction is a link to another folder and was left out."), tree.notes());
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    @Test
    void reportsEveryEntryWhileScanning() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        write(root.resolve("a/x.txt"), "x");
        write(root.resolve("b.txt"), "b");
        List<long[]> reports = new ArrayList<>();

        FolderTree.scan(root, NOTHING, new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                assertEquals(Phase.SCANNING, phase);
                reports.add(new long[]{done, total});
            }
        });

        assertEquals(3, reports.size(), "a, a/x.txt and b.txt");
        for (int i = 0; i < reports.size(); i++) {
            assertEquals(i + 1, reports.get(i)[0]);
            assertEquals(JobMonitor.UNKNOWN, reports.get(i)[1]);
        }
    }

    @Test
    void aCancelStopsTheScan() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("root"));
        write(root.resolve("a.txt"), "a");
        write(root.resolve("b.txt"), "b");
        List<Long> reports = new ArrayList<>();

        assertThrows(JobCancelledException.class, () -> FolderTree.scan(root, NOTHING, new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                reports.add(done);
            }

            @Override
            public boolean cancelRequested() {
                return !reports.isEmpty();
            }
        }));
        assertEquals(List.of(1L), reports);
    }

    @Test
    void theRootMustBeAnExistingFolder() throws IOException {
        Path file = write(tempDir.resolve("file.txt"), "f");

        assertThrows(NotDirectoryException.class, () -> FolderTree.scan(file, NOTHING, JobMonitor.NONE));
        assertThrows(NoSuchFileException.class, () -> FolderTree.scan(tempDir.resolve("missing"), NOTHING,
                JobMonitor.NONE));
    }
}
