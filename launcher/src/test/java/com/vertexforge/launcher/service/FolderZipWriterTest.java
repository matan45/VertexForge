package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class FolderZipWriterTest {

    /** A whole second, which the zip's extended timestamp keeps exactly. */
    private static final FileTime TIME = FileTime.from(Instant.parse("2025-05-06T07:08:09Z"));

    @TempDir
    Path tempDir;

    private static Path write(Path file, byte[] content) throws IOException {
        Files.createDirectories(file.getParent());
        Files.write(file, content);
        Files.setLastModifiedTime(file, TIME);
        return file;
    }

    private static byte[] randomBytes(int size, long seed) {
        byte[] bytes = new byte[size];
        new Random(seed).nextBytes(bytes);
        return bytes;
    }

    private FolderTree tree(Path root) throws IOException {
        return FolderTree.scan(root, relative -> false, JobMonitor.NONE);
    }

    /** {@code Game/} with a project file, an empty folder, a non-ASCII name and a file of three chunks and a bit. */
    private Path game() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("Game"));
        write(root.resolve("Game.vfproj"), "{\"projectName\": \"Game\"}\n".getBytes(StandardCharsets.UTF_8));
        write(root.resolve("assets/scripts/Main.mt"), "class Main {}\n".getBytes(StandardCharsets.UTF_8));
        write(root.resolve("assets/textures/big.bin"), randomBytes(3 * FolderZipWriter.CHUNK_BYTES + 100, 1690));
        write(root.resolve("assets/Ünïcødé 日本.txt"), "unicode".getBytes(StandardCharsets.UTF_8));
        Files.createDirectories(root.resolve("assets/empty"));
        return root;
    }

    private static List<String> names(Path zip) throws IOException {
        try (ZipFile file = new ZipFile(zip.toFile())) {
            return file.stream().map(ZipEntry::getName).toList();
        }
    }

    @Test
    void everyEntryLiesUnderTheRootFolderAndEveryFolderHasItsOwnEntry() throws IOException {
        Path root = game();
        Path zip = tempDir.resolve("backup.zip");

        FolderZipWriter.Written written = FolderZipWriter.write(tree(root), "Game", zip, JobMonitor.NONE);

        List<String> expected = List.of("Game/", "Game/assets/", "Game/assets/empty/", "Game/assets/scripts/",
                "Game/assets/textures/", "Game/Game.vfproj", "Game/assets/scripts/Main.mt",
                "Game/assets/textures/big.bin", "Game/assets/Ünïcødé 日本.txt");
        assertEquals(expected, names(zip));
        assertEquals(expected, written.entries().stream().map(FolderZipWriter.Entry::name).toList());
        assertEquals(zip, written.zip());
        assertEquals(tree(root).totalBytes(), written.contentBytes());
        try (ZipFile file = new ZipFile(zip.toFile())) {
            for (FolderTree.FileEntry source : tree(root).files()) {
                ZipEntry entry = file.getEntry("Game/" + source.relative());
                assertEquals(ZipEntry.DEFLATED, entry.getMethod(), source.relative());
                try (InputStream in = file.getInputStream(entry)) {
                    assertArrayEquals(Files.readAllBytes(source.path()), in.readAllBytes(), source.relative());
                }
            }
            assertTrue(file.getEntry("Game/assets/empty/").isDirectory());
        }
    }

    @Test
    void namesAreUtf8WithTheUtf8FlagSet() throws IOException {
        Path zip = tempDir.resolve("backup.zip");

        FolderZipWriter.write(tree(game()), "Game", zip, JobMonitor.NONE);

        byte[] bytes = Files.readAllBytes(zip);
        ByteBuffer header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(0x04034b50, header.getInt(0), "a local file header comes first");
        assertTrue((header.getShort(6) & 0x0800) != 0, "general purpose flag bit 11: names are UTF-8");
        assertTrue(names(zip).contains("Game/assets/Ünïcødé 日本.txt"));
    }

    @Test
    void filesKeepTheirModificationTime() throws IOException {
        Path zip = tempDir.resolve("backup.zip");

        FolderZipWriter.write(tree(game()), "Game", zip, JobMonitor.NONE);

        try (ZipFile file = new ZipFile(zip.toFile())) {
            for (String name : List.of("Game/Game.vfproj", "Game/assets/textures/big.bin")) {
                assertEquals(TIME.toInstant(), file.getEntry(name).getLastModifiedTime().toInstant(), name);
            }
        }
    }

    @Test
    void whatWasWrittenVerifies() throws IOException {
        Path zip = tempDir.resolve("backup.zip");
        FolderZipWriter.Written written = FolderZipWriter.write(tree(game()), "Game", zip, JobMonitor.NONE);
        List<long[]> reports = new ArrayList<>();

        FolderZipWriter.verify(written, new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                assertEquals(Phase.VERIFYING, phase);
                reports.add(new long[]{done, total});
            }
        });

        assertFalse(reports.isEmpty());
        assertEquals(written.contentBytes(), reports.getLast()[0], "every content byte was read back");
        for (long[] report : reports) {
            assertTrue(report[0] <= report[1], "done never passes the total");
        }
    }

    @Test
    void writingReportsEveryChunkByContentBytes() throws IOException {
        Path root = game();
        List<long[]> reports = new ArrayList<>();

        FolderZipWriter.write(tree(root), "Game", tempDir.resolve("backup.zip"), new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                assertEquals(Phase.WRITING, phase);
                reports.add(new long[]{done, total});
            }
        });

        long total = tree(root).totalBytes();
        // The big file takes four chunks, every other file one.
        assertEquals(4 + 3, reports.size());
        assertEquals(total, reports.getLast()[0]);
        for (long[] report : reports) {
            assertEquals(total, report[1]);
        }
    }

    @Test
    void aCancelAfterTheFirstChunkOfAFileDeletesTheArchive() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("Game"));
        write(root.resolve("big.bin"), randomBytes(2 * FolderZipWriter.CHUNK_BYTES + 1, 7));
        Path zip = tempDir.resolve("backup.zip");
        List<Long> reports = new ArrayList<>();

        assertThrows(JobCancelledException.class, () -> FolderZipWriter.write(tree(root), "Game", zip,
                new JobMonitor() {
                    @Override
                    public void progress(Phase phase, long done, long total) {
                        reports.add(done);
                    }

                    @Override
                    public boolean cancelRequested() {
                        return !reports.isEmpty();
                    }
                }));

        assertEquals(List.of((long) FolderZipWriter.CHUNK_BYTES), reports, "stopped in the middle of the file");
        assertFalse(Files.exists(zip));
        assertEquals(Set.of("Game"), TestSupport.fileNames(tempDir));
    }

    @Test
    void aFailureReportedThroughProgressDeletesTheArchive() throws IOException {
        Path zip = tempDir.resolve("backup.zip");
        Path root = game();

        IOException failure = assertThrows(IOException.class, () -> FolderZipWriter.write(tree(root), "Game", zip,
                new JobMonitor() {
                    @Override
                    public void progress(Phase phase, long done, long total) throws IOException {
                        if (done > FolderZipWriter.CHUNK_BYTES) {
                            throw new IOException("injected failure");
                        }
                    }
                }));

        assertEquals("injected failure", failure.getMessage());
        assertFalse(Files.exists(zip));
    }

    @Test
    void anExistingTargetIsNeverOverwrittenOrDeleted() throws IOException {
        Path zip = Files.writeString(tempDir.resolve("backup.zip"), "someone else's");
        Path root = game();

        assertThrows(FileAlreadyExistsException.class, () -> FolderZipWriter.write(tree(root), "Game", zip,
                JobMonitor.NONE));

        assertEquals("someone else's", Files.readString(zip));
    }

    @Test
    void aDamagedArchiveFailsVerification() throws IOException {
        Path root = Files.createDirectory(tempDir.resolve("Game"));
        // Random bytes do not compress, so the middle of the archive is the middle of this file's data.
        write(root.resolve("data.bin"), randomBytes(200_000, 99));
        Path zip = tempDir.resolve("backup.zip");
        FolderZipWriter.Written written = FolderZipWriter.write(tree(root), "Game", zip, JobMonitor.NONE);
        FolderZipWriter.verify(written, JobMonitor.NONE);

        byte[] bytes = Files.readAllBytes(zip);
        bytes[bytes.length / 2] ^= 0x5A;
        Files.write(zip, bytes);

        IOException failure = assertThrows(IOException.class, () -> FolderZipWriter.verify(written, JobMonitor.NONE));
        assertTrue(failure.getMessage().startsWith("The archive " + zip + " did not verify: "), failure.getMessage());
    }

    @Test
    void anArchiveMissingAnEntryFailsVerification() throws IOException {
        Path zip = tempDir.resolve("backup.zip");
        FolderZipWriter.Written written = FolderZipWriter.write(tree(game()), "Game", zip, JobMonitor.NONE);
        List<FolderZipWriter.Entry> more = new ArrayList<>(written.entries());
        more.add(new FolderZipWriter.Entry("Game/missing.txt", 1, 0, false));

        IOException failure = assertThrows(IOException.class, () -> FolderZipWriter.verify(
                new FolderZipWriter.Written(zip, more, written.contentBytes()), JobMonitor.NONE));

        assertTrue(failure.getMessage().contains("it holds " + written.entries().size() + " entries instead of "
                + more.size()), failure.getMessage());
    }

    @Test
    void verificationCanBeCancelled() throws IOException {
        Path zip = tempDir.resolve("backup.zip");
        FolderZipWriter.Written written = FolderZipWriter.write(tree(game()), "Game", zip, JobMonitor.NONE);
        List<Long> reports = new ArrayList<>();

        assertThrows(JobCancelledException.class, () -> FolderZipWriter.verify(written, new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                reports.add(done);
            }

            @Override
            public boolean cancelRequested() {
                return !reports.isEmpty();
            }
        }));
        assertEquals(1, reports.size());
        assertTrue(Files.exists(zip), "verify only reads; the caller decides what to delete");
    }

    @Test
    void aFailureReportedThroughProgressWhileVerifyingIsPassedOnAsItIs() throws IOException {
        Path zip = tempDir.resolve("backup.zip");
        FolderZipWriter.Written written = FolderZipWriter.write(tree(game()), "Game", zip, JobMonitor.NONE);

        IOException failure = assertThrows(IOException.class, () -> FolderZipWriter.verify(written,
                new JobMonitor() {
                    @Override
                    public void progress(Phase phase, long done, long total) throws IOException {
                        throw new IOException("injected failure");
                    }
                }));

        assertEquals("injected failure", failure.getMessage(), "not a damaged archive");
    }

    @Test
    void theRequiredSpaceIsNeverLessThanTheArchive() throws IOException {
        Path root = game();
        Path zip = tempDir.resolve("backup.zip");
        FolderTree tree = tree(root);

        FolderZipWriter.write(tree, "Game", zip, JobMonitor.NONE);

        assertTrue(FolderZipWriter.requiredSpace(tree, "Game") >= Files.size(zip));
    }

    @Test
    void theHeaderEstimateAloneCoversManySmallFiles() throws IOException {
        // Without the margin, so that the per-entry estimate itself is what is checked.
        Path root = Files.createDirectory(tempDir.resolve("Game"));
        for (int i = 0; i < 300; i++) {
            write(root.resolve("folder" + (i % 10) + "/file-" + i + "-ünï.txt"), ("x" + i).getBytes(
                    StandardCharsets.UTF_8));
        }
        Path zip = tempDir.resolve("backup.zip");
        FolderTree tree = tree(root);

        FolderZipWriter.write(tree, "Game", zip, JobMonitor.NONE);

        long margin = Math.max(FolderZipWriter.MIN_MARGIN_BYTES, tree.totalBytes() / 100);
        assertTrue(FolderZipWriter.requiredSpace(tree, "Game") - margin >= Files.size(zip),
                "estimate " + (FolderZipWriter.requiredSpace(tree, "Game") - margin) + " < " + Files.size(zip));
    }

    @Test
    void theRootEntryMustBeASingleName() throws IOException {
        FolderTree tree = tree(game());

        for (String bad : List.of("", "a/b", "a\\b")) {
            assertThrows(IllegalArgumentException.class, () -> FolderZipWriter.requiredSpace(tree, bad), bad);
            assertThrows(IllegalArgumentException.class, () -> FolderZipWriter.write(tree, bad,
                    tempDir.resolve("never.zip"), JobMonitor.NONE), bad);
        }
        assertFalse(Files.exists(tempDir.resolve("never.zip")));
    }
}
