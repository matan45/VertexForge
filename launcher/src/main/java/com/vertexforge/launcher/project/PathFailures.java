package com.vertexforge.launcher.project;

import com.vertexforge.launcher.util.Volumes;

import java.io.IOException;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileSystemException;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;

/**
 * The words for a file-system call that failed on a project path, shared by the status probe ({@code PathProbe},
 * VK-1678) and the inspector of picked projects (VK-1685), so a row and a refusal say the same thing.
 */
public final class PathFailures {

    /** Shown when Windows refuses access. */
    public static final String ACCESS_DENIED = "Access denied";

    private PathFailures() {
        // Utility class - prevent instantiation
    }

    /** See {@link Volumes#isAbsentDriveLetter}. */
    public static boolean isAbsentDrive(Path path) {
        return Volumes.isAbsentDriveLetter(path);
    }

    /** "Drive E:\ is not available". */
    public static String absentDrive(Path path) {
        return "Drive " + path.getRoot() + " is not available";
    }

    /**
     * Why a call on {@code path} failed: {@code notFound} or the absent drive for a missing file, "Access denied", the
     * absent drive or Windows' own reason for a volume that did not answer, else the exception's reason.
     */
    public static String describe(Path path, IOException error, String notFound) {
        if (error instanceof NoSuchFileException) {
            return isAbsentDrive(path) ? absentDrive(path) : notFound;
        }
        if (error instanceof AccessDeniedException) {
            return ACCESS_DENIED;
        }
        if (error instanceof FileSystemException && isAbsentDrive(path)) {
            return absentDrive(path);
        }
        return reasonOf(error);
    }

    /**
     * The exception's reason: a {@link FileSystemException}'s own reason (Java's message often holds only the path),
     * else its message, else its type.
     */
    public static String reasonOf(IOException error) {
        if (error instanceof FileSystemException fileSystemException && fileSystemException.getReason() != null) {
            return fileSystemException.getReason();
        }
        return error.getMessage() != null ? error.getMessage() : error.getClass().getSimpleName();
    }
}
