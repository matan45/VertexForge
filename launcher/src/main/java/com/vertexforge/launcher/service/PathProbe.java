package com.vertexforge.launcher.service;

import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.project.PathFailures;
import com.vertexforge.launcher.project.ProjectFiles;

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
     * The extension (in any letter case, VK-1685), then the attributes, then an actual open for reading.
     * {@code Files.exists} is not used: it reports a file it may not read as absent.
     */
    private static ProjectStatus probeFileSystem(Path file) {
        try {
            if (!ProjectFiles.isProjectFile(file)) {
                return ProjectStatus.unreadable("Not a " + ProjectFiles.EXTENSION + " file");
            }

            BasicFileAttributes attributes;
            try {
                attributes = Files.readAttributes(file, BasicFileAttributes.class);
            } catch (NoSuchFileException e) {
                return missing(file);
            } catch (AccessDeniedException e) {
                return ProjectStatus.unreadable(PathFailures.ACCESS_DENIED);
            } catch (FileSystemException e) {
                return PathFailures.isAbsentDrive(file) ? missingDrive(file)
                        : ProjectStatus.unreadable(PathFailures.reasonOf(e));
            }
            if (!attributes.isRegularFile()) {
                return ProjectStatus.unreadable("Not a file");
            }

            try (SeekableByteChannel ignored = Files.newByteChannel(file, StandardOpenOption.READ)) {
                return ProjectStatus.ok();
            } catch (NoSuchFileException e) {
                return missing(file);
            } catch (AccessDeniedException e) {
                return ProjectStatus.unreadable(PathFailures.ACCESS_DENIED);
            }
        } catch (IOException e) {
            return ProjectStatus.unreadable(PathFailures.reasonOf(e));
        } catch (RuntimeException e) {
            return ProjectStatus.unreadable("Could not check the project file: " + e);
        }
    }

    private static ProjectStatus missing(Path file) {
        return PathFailures.isAbsentDrive(file) ? missingDrive(file)
                : ProjectStatus.missing("The project file was not found");
    }

    private static ProjectStatus missingDrive(Path file) {
        return ProjectStatus.missing(PathFailures.absentDrive(file));
    }
}
