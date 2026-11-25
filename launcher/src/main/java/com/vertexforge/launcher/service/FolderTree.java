package com.vertexforge.launcher.service;

import com.vertexforge.launcher.util.FileTrees;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NotDirectoryException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.FileTime;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.function.Predicate;
import java.util.stream.Stream;

/**
 * Every folder and file below a root, as a backup or a copy takes them (VK-1690; VK-1691 backs up a script library
 * with it).
 * <p>
 * The walk follows the template scanner's rules ({@code FolderTemplateScanner}): a symbolic link is left out with a
 * note; a junction (a folder that is also a reparse point) is entered only when its real path is its own, which keeps
 * OneDrive placeholder folders and leaves out junctions to other folders, with a note; OneDrive placeholder files are
 * kept (reading one downloads it); hidden files count like any other.
 *
 * @param root                  the folder that was scanned
 * @param folders               every folder below the root that was taken, '/'-separated and relative to it (empty
 *                              folders too, so that they survive), sorted, so each parent comes before its children
 * @param files                 every file taken, sorted by relative path
 * @param totalBytes            the sizes of {@code files} added up
 * @param deepestRelativeLength the length, in characters, of the longest relative path among the files and folders;
 *                              0 for an empty root
 * @param notes                 what was left out and why, for the user
 */
public record FolderTree(Path root, List<String> folders, List<FileEntry> files, long totalBytes,
                         int deepestRelativeLength, List<String> notes) {

    /**
     * One file.
     *
     * @param path         the file itself
     * @param relative     its path relative to the root, '/'-separated, without a leading slash
     * @param size         its size when it was scanned
     * @param lastModified its modification time when it was scanned
     */
    public record FileEntry(Path path, String relative, long size, FileTime lastModified) {
        public FileEntry {
            Objects.requireNonNull(path, "path");
            Objects.requireNonNull(relative, "relative");
            Objects.requireNonNull(lastModified, "lastModified");
        }
    }

    public FolderTree {
        Objects.requireNonNull(root, "root");
        folders = List.copyOf(folders);
        files = List.copyOf(files);
        notes = List.copyOf(notes);
    }

    /**
     * Scans {@code root}, which must be a folder (it may itself be a link to one). Blocks on the file system, so it
     * only runs on a background thread.
     *
     * @param excluded given the '/'-relative path of each folder and file before anything else is read about it; an
     *                 excluded folder is not entered, and nothing excluded gets a note
     * @param monitor  hears about every entry ({@link JobMonitor.Phase#SCANNING}) and may cancel after any of them
     * @throws JobCancelledException when the monitor asks to stop
     * @throws IOException           when the root or a folder below it cannot be listed, or an entry's attributes
     *                               cannot be read
     */
    public static FolderTree scan(Path root, Predicate<String> excluded, JobMonitor monitor) throws IOException {
        Objects.requireNonNull(root, "root");
        Objects.requireNonNull(excluded, "excluded");
        Objects.requireNonNull(monitor, "monitor");
        BasicFileAttributes rootAttributes = Files.readAttributes(root, BasicFileAttributes.class);
        if (!rootAttributes.isDirectory()) {
            throw new NotDirectoryException(root.toString());
        }

        Walk walk = new Walk(excluded, monitor);
        walk.folder(root, "");
        walk.folders.sort(Comparator.naturalOrder());
        walk.files.sort(Comparator.comparing(FileEntry::relative));
        return new FolderTree(root, walk.folders, walk.files, walk.totalBytes, walk.deepest, walk.notes);
    }

    /** The state of one scan. */
    private static final class Walk {
        private final Predicate<String> excluded;
        private final JobMonitor monitor;
        private final List<String> folders = new ArrayList<>();
        private final List<FileEntry> files = new ArrayList<>();
        private final List<String> notes = new ArrayList<>();
        private long totalBytes;
        private int deepest;
        private long entries;

        Walk(Predicate<String> excluded, JobMonitor monitor) {
            this.excluded = excluded;
            this.monitor = monitor;
        }

        void folder(Path folder, String prefix) throws IOException {
            for (Path child : children(folder)) {
                monitor.checkCancelled();
                String name = child.getFileName().toString();
                String relative = prefix.isEmpty() ? name : prefix + "/" + name;
                if (excluded.test(relative)) {
                    continue;
                }

                BasicFileAttributes attributes = Files.readAttributes(child, BasicFileAttributes.class,
                        LinkOption.NOFOLLOW_LINKS);
                if (attributes.isSymbolicLink()) {
                    notes.add(relative + " is a link and was left out.");
                } else if (attributes.isDirectory()) {
                    // A junction reads as a folder that is also "other": entered only when it does not lead elsewhere.
                    if (!attributes.isOther() || FileTrees.isOwnFolder(child)) {
                        took(relative);
                        folders.add(relative);
                        folder(child, relative);
                    } else {
                        notes.add(relative + " is a link to another folder and was left out.");
                    }
                } else if (attributes.isRegularFile() || (attributes.isOther() && Files.isRegularFile(child))) {
                    took(relative);
                    files.add(new FileEntry(child, relative, attributes.size(), attributes.lastModifiedTime()));
                    totalBytes += attributes.size();
                } else {
                    notes.add(relative + " is not a regular file and was left out.");
                }

                entries++;
                monitor.progress(JobMonitor.Phase.SCANNING, entries, JobMonitor.UNKNOWN);
            }
        }

        private void took(String relative) {
            deepest = Math.max(deepest, relative.length());
        }

        private static List<Path> children(Path folder) throws IOException {
            try (Stream<Path> listing = Files.list(folder)) {
                return listing.sorted(Comparator.comparing(child -> child.getFileName().toString())).toList();
            }
        }
    }
}
