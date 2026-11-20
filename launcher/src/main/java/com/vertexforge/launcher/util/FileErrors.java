package com.vertexforge.launcher.util;

import java.nio.file.AccessDeniedException;
import java.nio.file.DirectoryNotEmptyException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.FileSystemException;
import java.nio.file.NoSuchFileException;

/** Short reasons for file errors, for the UI. */
public final class FileErrors {

    private FileErrors() {
        // Utility class - prevent instantiation
    }

    /**
     * A short reason for the UI. Java's file exceptions often carry only the path (the Windows reason is in the type),
     * so the common types get words of their own.
     */
    public static String describe(Throwable error) {
        if (error instanceof FileSystemException fileError && fileError.getReason() == null
                && fileError.getFile() != null) {
            String what = switch (fileError) {
                case NoSuchFileException ignored -> "not found";
                case AccessDeniedException ignored -> "access denied";
                case FileAlreadyExistsException ignored -> "already exists";
                case DirectoryNotEmptyException ignored -> "the folder is not empty";
                default -> null;
            };
            if (what != null) {
                return fileError.getFile() + ": " + what;
            }
        }
        if (error.getMessage() != null) {
            return error.getMessage();
        }
        return error.getClass().getSimpleName();
    }
}
