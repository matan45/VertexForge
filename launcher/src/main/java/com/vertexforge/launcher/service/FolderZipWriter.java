package com.vertexforge.launcher.service;

import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.TransientIo;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.Channels;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * Writes a {@link FolderTree} into a new zip archive and reads it back to prove it (VK-1690: a project backup; VK-1691
 * backs up a script library with it).
 * <p>
 * Every entry lies under one root folder entry ({@code <rootEntry>/}), and every folder has an entry of its own, so
 * empty folders survive. Names are UTF-8 (Java then sets the zip's UTF-8 flag), files are deflated at
 * {@link Deflater#BEST_SPEED} and keep their modification time (to the second), and Zip64 is used by itself when the
 * archive needs it. The writer reads only the files the tree lists: it never decides about links again.
 */
public final class FolderZipWriter {

    /** Files are read, and progress reported, in chunks of this size. */
    static final int CHUNK_BYTES = 64 * 1024;
    /** The smallest margin {@link #requiredSpace} adds for the deflate stream and the file system. */
    static final long MIN_MARGIN_BYTES = 16L * 1024 * 1024;
    /**
     * An entry's bytes besides its name and content, at most: a central header (46 bytes; the local one is 30) twice,
     * a Zip64 data descriptor (24), and the extra fields Java writes (extended timestamp 9 + 9, Zip64 20 + 28).
     */
    static final long ENTRY_OVERHEAD_BYTES = 2 * 46 + 24 + 9 + 9 + 20 + 28;
    /** The end records: Zip64 end of central directory (56), its locator (20) and the classic end record (22). */
    static final long END_RECORDS_BYTES = 56 + 20 + 22;

    private static final int DELETE_ATTEMPTS = 5;
    private static final Duration DELETE_FIRST_DELAY = Duration.ofMillis(50);
    private static final Duration DELETE_MAX_DELAY = Duration.ofMillis(500);

    /**
     * One entry as it was written.
     *
     * @param name      the entry's full name: {@code Game/assets/x.png}, or {@code Game/assets/} for a folder
     * @param size      the content bytes written (0 for a folder)
     * @param crc       the CRC-32 of the content written (0 for a folder)
     * @param directory whether it is a folder entry
     */
    public record Entry(String name, long size, long crc, boolean directory) {
        public Entry {
            Objects.requireNonNull(name, "name");
        }
    }

    /**
     * A written archive.
     *
     * @param zip          the archive
     * @param entries      its entries in the order written: the root folder, the folders, then the files
     * @param contentBytes the content bytes of all files added up
     */
    public record Written(Path zip, List<Entry> entries, long contentBytes) {
        public Written {
            Objects.requireNonNull(zip, "zip");
            entries = List.copyOf(entries);
        }
    }

    private FolderZipWriter() {
        // Utility class - prevent instantiation
    }

    /**
     * Writes {@code tree} into {@code target}, which must not exist yet: it is created with {@code CREATE_NEW}, so an
     * existing file fails with {@link java.nio.file.FileAlreadyExistsException} and is left as it is. Once created,
     * the target is the writer's: any failure, a cancel included, closes and deletes it before the exception is
     * passed on. The data is forced to the disk before the target is closed.
     *
     * @param rootEntry the name of the folder every entry lies under (a single name, without '/')
     * @param monitor   hears about every chunk ({@link JobMonitor.Phase#WRITING}, content bytes) and may cancel after
     *                  any of them
     * @throws JobCancelledException when the monitor asks to stop
     */
    public static Written write(FolderTree tree, String rootEntry, Path target, JobMonitor monitor)
            throws IOException {
        Objects.requireNonNull(tree, "tree");
        requireRootEntry(rootEntry);
        Objects.requireNonNull(target, "target");
        Objects.requireNonNull(monitor, "monitor");

        FileChannel channel = FileChannel.open(target, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE);
        // From here on the target is ours, and every failure deletes it.
        ZipOutputStream zip = null;
        try {
            // Closing the zip stream closes the channel too.
            zip = new ZipOutputStream(new BufferedOutputStream(Channels.newOutputStream(channel), CHUNK_BYTES),
                    StandardCharsets.UTF_8);
            return writeTo(zip, channel, tree, rootEntry, target, monitor);
        } catch (Throwable failure) {
            // The zip stream first, so that its deflater is released; the channel is closed whatever it does.
            if (zip != null) {
                try {
                    zip.close();
                } catch (IOException | RuntimeException closeFailure) {
                    failure.addSuppressed(closeFailure);
                }
            }
            try {
                channel.close();
            } catch (IOException closeFailure) {
                failure.addSuppressed(closeFailure);
            }
            try {
                TransientIo.retry(() -> Files.deleteIfExists(target), DELETE_ATTEMPTS, DELETE_FIRST_DELAY,
                        DELETE_MAX_DELAY, "deleting " + target);
            } catch (IOException deleteFailure) {
                failure.addSuppressed(deleteFailure);
            }
            throw failure;
        }
    }

    private static Written writeTo(ZipOutputStream zip, FileChannel channel, FolderTree tree, String rootEntry,
                                   Path target, JobMonitor monitor) throws IOException {
        List<Entry> entries = new ArrayList<>();
        String prefix = rootEntry + "/";
        long total = tree.totalBytes();
        long done = 0;
        long contentBytes = 0;
        byte[] buffer = new byte[CHUNK_BYTES];

        zip.setMethod(ZipOutputStream.DEFLATED);
        zip.setLevel(Deflater.BEST_SPEED);

        putFolder(zip, prefix, entries);
        for (String folder : tree.folders()) {
            putFolder(zip, prefix + folder + "/", entries);
        }

        for (FolderTree.FileEntry file : tree.files()) {
            monitor.checkCancelled();
            ZipEntry entry = new ZipEntry(prefix + file.relative());
            entry.setLastModifiedTime(file.lastModified());
            zip.putNextEntry(entry);
            CRC32 crc = new CRC32();
            long size = 0;
            try (InputStream in = Files.newInputStream(file.path())) {
                for (int read = in.read(buffer); read >= 0; read = in.read(buffer)) {
                    if (read == 0) {
                        continue;
                    }
                    zip.write(buffer, 0, read);
                    crc.update(buffer, 0, read);
                    size += read;
                    done += read;
                    // A file that grew since the scan must not make the bar run past its end.
                    total = Math.max(total, done);
                    monitor.progress(JobMonitor.Phase.WRITING, done, total);
                    monitor.checkCancelled();
                }
            }
            zip.closeEntry();
            entries.add(new Entry(entry.getName(), size, crc.getValue(), false));
            contentBytes += size;
        }

        zip.finish();
        zip.flush();
        channel.force(true);
        zip.close();
        return new Written(target, entries, contentBytes);
    }

    private static void putFolder(ZipOutputStream zip, String name, List<Entry> entries) throws IOException {
        ZipEntry entry = new ZipEntry(name);
        entry.setMethod(ZipEntry.STORED);
        entry.setSize(0);
        entry.setCompressedSize(0);
        entry.setCrc(0);
        zip.putNextEntry(entry);
        zip.closeEntry();
        entries.add(new Entry(name, 0, 0, true));
    }

    /**
     * Opens {@code written}'s archive again and checks that it holds exactly the entries written, in order, with
     * their names, sizes and CRCs, then inflates every file entry and recomputes its CRC: a disk that dropped or
     * damaged a byte fails here, not when the archive is needed.
     *
     * @param monitor hears about every chunk ({@link JobMonitor.Phase#VERIFYING}, content bytes) and may cancel
     *                after any of them
     * @throws JobCancelledException when the monitor asks to stop
     * @throws IOException           when the archive differs from what was written or cannot be read
     */
    public static void verify(Written written, JobMonitor monitor) throws IOException {
        Objects.requireNonNull(written, "written");
        Objects.requireNonNull(monitor, "monitor");
        long total = written.contentBytes();
        long done = 0;
        byte[] buffer = new byte[CHUNK_BYTES];

        // Only what reads the archive is reported as "cannot be read back"; the monitor's own failures pass as they are.
        ZipFile zip;
        try {
            zip = new ZipFile(written.zip().toFile(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw unreadable(written, e);
        }
        try (zip) {
            List<? extends ZipEntry> actual = zip.stream().toList();
            if (actual.size() != written.entries().size()) {
                throw damaged(written, "it holds " + actual.size() + " entries instead of "
                        + written.entries().size());
            }
            for (int i = 0; i < actual.size(); i++) {
                monitor.checkCancelled();
                Entry expected = written.entries().get(i);
                ZipEntry entry = actual.get(i);
                if (!entry.getName().equals(expected.name()) || entry.isDirectory() != expected.directory()) {
                    throw damaged(written, "entry " + (i + 1) + " is " + entry.getName() + " instead of "
                            + expected.name());
                }
                if (entry.getSize() != expected.size() || entry.getCrc() != expected.crc()) {
                    throw damaged(written, entry.getName() + " is listed with a different size or checksum");
                }
                if (expected.directory()) {
                    continue;
                }

                CRC32 crc = new CRC32();
                long size = 0;
                try (InputStream in = open(zip, entry, written)) {
                    for (int read = read(in, buffer, written); read >= 0; read = read(in, buffer, written)) {
                        if (read == 0) {
                            continue;
                        }
                        crc.update(buffer, 0, read);
                        size += read;
                        done += read;
                        total = Math.max(total, done);
                        monitor.progress(JobMonitor.Phase.VERIFYING, done, total);
                        monitor.checkCancelled();
                    }
                }
                if (size != expected.size() || crc.getValue() != expected.crc()) {
                    throw damaged(written, entry.getName() + " reads back with different content");
                }
            }
        }
    }

    private static InputStream open(ZipFile zip, ZipEntry entry, Written written) throws IOException {
        try {
            return zip.getInputStream(entry);
        } catch (IOException e) {
            throw unreadable(written, e);
        }
    }

    private static int read(InputStream in, byte[] buffer, Written written) throws IOException {
        try {
            return in.read(buffer);
        } catch (IOException e) {
            throw unreadable(written, e);
        }
    }

    private static IOException unreadable(Written written, IOException cause) {
        IOException unreadable = damaged(written, "it cannot be read back: " + FileErrors.describe(cause));
        unreadable.initCause(cause);
        return unreadable;
    }

    /**
     * An upper bound of the size of {@code tree}'s archive: its content (deflate can grow stored data a little), each
     * entry's names and headers, the end records, and a margin of {@link #MIN_MARGIN_BYTES} or 1% of the content,
     * whichever is larger, for the deflate stream's growth and the file system's own use.
     */
    public static long requiredSpace(FolderTree tree, String rootEntry) {
        Objects.requireNonNull(tree, "tree");
        requireRootEntry(rootEntry);
        String prefix = rootEntry + "/";
        long overhead = END_RECORDS_BYTES + entryBytes(prefix);
        for (String folder : tree.folders()) {
            overhead += entryBytes(prefix + folder + "/");
        }
        for (FolderTree.FileEntry file : tree.files()) {
            overhead += entryBytes(prefix + file.relative());
        }
        long content = tree.totalBytes();
        return content + overhead + Math.max(MIN_MARGIN_BYTES, content / 100);
    }

    private static long entryBytes(String name) {
        // The name is stored twice: in the local header and in the central directory.
        return ENTRY_OVERHEAD_BYTES + 2L * name.getBytes(StandardCharsets.UTF_8).length;
    }

    private static void requireRootEntry(String rootEntry) {
        Objects.requireNonNull(rootEntry, "rootEntry");
        if (rootEntry.isEmpty() || rootEntry.indexOf('/') >= 0 || rootEntry.indexOf('\\') >= 0) {
            throw new IllegalArgumentException("rootEntry must be a single folder name: \"" + rootEntry + "\"");
        }
    }

    private static ArchiveMismatchException damaged(Written written, String reason) {
        return new ArchiveMismatchException("The archive " + written.zip() + " did not verify: " + reason + ".");
    }

    /** The archive read back differs from what was written. */
    private static final class ArchiveMismatchException extends IOException {
        ArchiveMismatchException(String message) {
            super(message);
        }
    }
}
