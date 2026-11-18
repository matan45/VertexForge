package com.vertexforge.launcher.service;

import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.InvalidProjectFileException;
import com.vertexforge.launcher.project.PathFailures;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.ProjectFileReader;
import com.vertexforge.launcher.project.ProjectFiles;

import java.io.IOException;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;

/**
 * Looks at one listed project file (VK-1678): whether it is there, then (VK-1686) what it says. A probe blocks,
 * possibly for many seconds on an unreachable network share, so it only ever runs on {@link ProjectHealthService}'s
 * threads. It never throws, and it never creates or changes anything.
 * <p>
 * The folder's editor lock is not looked at here: {@link ProjectHealthService} probes it after a successful read,
 * with a timeout of its own, so a slow lock check can never make a readable project Unreadable.
 */
@FunctionalInterface
public interface PathProbe {

    /** What the file holds; a {@link ProjectFileCheck.Readable} carries {@link EditorLockState#SKIPPED}. */
    ProjectFileCheck probe(Path projectFile);

    /** The real file system. */
    static PathProbe system() {
        return PathProbe::probeFileSystem;
    }

    /**
     * The extension (in any letter case, VK-1685), then the attributes, then a read of the whole file.
     * {@code Files.exists} is not used: it reports a file it may not read as absent.
     */
    private static ProjectFileCheck probeFileSystem(Path file) {
        try {
            if (!ProjectFiles.isProjectFile(file)) {
                return new ProjectFileCheck.Invalid("Not a " + ProjectFiles.EXTENSION + " file");
            }

            BasicFileAttributes attributes;
            try {
                attributes = Files.readAttributes(file, BasicFileAttributes.class);
            } catch (NoSuchFileException e) {
                return missing(file);
            } catch (AccessDeniedException e) {
                return new ProjectFileCheck.Unreadable(PathFailures.ACCESS_DENIED);
            } catch (FileSystemException e) {
                return PathFailures.isAbsentDrive(file) ? missingDrive(file)
                        : new ProjectFileCheck.Unreadable(PathFailures.reasonOf(e));
            }
            if (!attributes.isRegularFile()) {
                return new ProjectFileCheck.Invalid("Not a file");
            }

            ProjectFile project;
            try {
                project = ProjectFileReader.read(file);
            } catch (InvalidProjectFileException e) {
                return new ProjectFileCheck.Invalid(e.getMessage());
            } catch (NoSuchFileException e) {
                return missing(file);
            } catch (AccessDeniedException e) {
                return new ProjectFileCheck.Unreadable(PathFailures.ACCESS_DENIED);
            }

            return new ProjectFileCheck.Readable(project, EditorLockState.SKIPPED);
        } catch (IOException e) {
            return new ProjectFileCheck.Unreadable(PathFailures.reasonOf(e));
        } catch (RuntimeException e) {
            return new ProjectFileCheck.Unreadable("Could not check the project file: " + e);
        }
    }

    private static ProjectFileCheck missing(Path file) {
        return PathFailures.isAbsentDrive(file) ? missingDrive(file)
                : new ProjectFileCheck.Missing("The project file was not found");
    }

    private static ProjectFileCheck missingDrive(Path file) {
        return new ProjectFileCheck.Missing(PathFailures.absentDrive(file));
    }
}
