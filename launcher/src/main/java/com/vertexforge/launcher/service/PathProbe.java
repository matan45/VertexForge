package com.vertexforge.launcher.service;

import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.util.ProjectValidator;

import java.io.IOException;
import java.nio.channels.SeekableByteChannel;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;

/**
 * Looks at one project file and says whether it can be opened (VK-1678). A probe blocks, possibly for many seconds on
 * an unreachable network share, so it only ever runs on {@link ProjectStatusChecker}'s threads. It never throws.
 */
@FunctionalInterface
public interface PathProbe {

    ProjectStatus probe(Path projectFile);

    /** The real file system. */
    static PathProbe system() {
        return PathProbe::probeFileSystem;
    }

    /**
     * The extension, then the attributes, then an actual open for reading. {@code Files.exists} is not used: it
     * reports a file it may not read as absent.
     */
    private static ProjectStatus probeFileSystem(Path file) {
        try {
            Path fileName = file.getFileName();
            if (fileName == null || !fileName.toString().endsWith(ProjectValidator.PROJECT_EXTENSION)) {
                return ProjectStatus.unreadable("Not a " + ProjectValidator.PROJECT_EXTENSION + " file");
            }

            BasicFileAttributes attributes;
            try {
                attributes = Files.readAttributes(file, BasicFileAttributes.class);
            } catch (NoSuchFileException e) {
                return missing(file);
            } catch (AccessDeniedException e) {
                return ProjectStatus.unreadable("Access denied");
            } catch (FileSystemException e) {
                return driveIsAbsent(file) ? missingDrive(file) : ProjectStatus.unreadable(reasonOf(e));
            }
            if (!attributes.isRegularFile()) {
                return ProjectStatus.unreadable("Not a file");
            }

            try (SeekableByteChannel ignored = Files.newByteChannel(file, StandardOpenOption.READ)) {
                return ProjectStatus.ok();
            } catch (NoSuchFileException e) {
                return missing(file);
            } catch (AccessDeniedException e) {
                return ProjectStatus.unreadable("Access denied");
            }
        } catch (IOException e) {
            return ProjectStatus.unreadable(reasonOf(e));
        } catch (RuntimeException e) {
            return ProjectStatus.unreadable("Could not check the project file: " + e);
        }
    }

    private static ProjectStatus missing(Path file) {
        return driveIsAbsent(file) ? missingDrive(file) : ProjectStatus.missing("The project file was not found");
    }

    private static ProjectStatus missingDrive(Path file) {
        return ProjectStatus.missing("Drive " + file.getRoot() + " is not available");
    }

    /**
     * Whether {@code file} is on a drive letter that does not exist right now (an unplugged USB drive, a removed
     * {@code subst}). Only drive-letter roots are checked: looking at a network share's root could block.
     */
    private static boolean driveIsAbsent(Path file) {
        Path root = file.getRoot();
        if (root == null) {
            return false;
        }
        String text = root.toString();
        boolean driveLetter = text.length() == 3 && Character.isLetter(text.charAt(0)) && text.charAt(1) == ':'
                && (text.charAt(2) == '\\' || text.charAt(2) == '/');
        return driveLetter && !Files.exists(root);
    }

    private static String reasonOf(IOException e) {
        if (e instanceof FileSystemException fileSystemException && fileSystemException.getReason() != null) {
            return fileSystemException.getReason();
        }
        return e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
    }
}
