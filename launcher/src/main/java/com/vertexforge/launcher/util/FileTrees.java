package com.vertexforge.launcher.util;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.stream.Stream;

/**
 * Folder-tree helpers shared by everything that builds a folder of its own and must be able to remove it again
 * (VK-1679, VK-1690): a project creation, a project copy.
 */
public final class FileTrees {

    /** Every delete is retried for about a second: an antivirus scan of a file just written can hold it that long. */
    private static final int DELETE_ATTEMPTS = 5;
    private static final Duration DELETE_FIRST_DELAY = Duration.ofMillis(50);
    private static final Duration DELETE_MAX_DELAY = Duration.ofMillis(500);

    private FileTrees() {
        // Utility class - prevent instantiation
    }

    /**
     * Deletes {@code path} and, when it is a real folder, everything in it first. Never follows a link or junction:
     * a link is deleted itself, and what it points to is left alone. Whatever could not be deleted is added to
     * {@code failures}; the rest is still deleted.
     */
    public static void deleteTree(Path path, List<IOException> failures) {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(failures, "failures");
        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        } catch (NoSuchFileException e) {
            return;
        } catch (IOException e) {
            failures.add(e);
            return;
        }

        // A junction reads as a folder that is also "other" (a reparse point): it is deleted itself, not entered. A
        // OneDrive placeholder folder is a reparse point too, but one whose real path is its own: that one is entered.
        if (attributes.isDirectory() && (!attributes.isOther() || isOwnFolder(path))) {
            List<Path> children = List.of();
            try (Stream<Path> listing = Files.list(path)) {
                children = listing.toList();
            } catch (IOException e) {
                failures.add(e);
            }
            for (Path child : children) {
                deleteTree(child, failures);
            }
        }

        try {
            TransientIo.retry(() -> Files.deleteIfExists(path), DELETE_ATTEMPTS, DELETE_FIRST_DELAY, DELETE_MAX_DELAY,
                    "deleting " + path);
        } catch (IOException e) {
            failures.add(e);
        }
    }

    /**
     * Whether {@code folder} really lives where it is listed, rather than redirecting elsewhere (a junction). A folder
     * that cannot be resolved counts as redirecting.
     */
    public static boolean isOwnFolder(Path folder) {
        Objects.requireNonNull(folder, "folder");
        Path parent = folder.getParent();
        Path name = folder.getFileName();
        if (parent == null || name == null) {
            return false;
        }
        try {
            return folder.toRealPath().equals(parent.toRealPath().resolve(name));
        } catch (IOException e) {
            return false;
        }
    }
}
