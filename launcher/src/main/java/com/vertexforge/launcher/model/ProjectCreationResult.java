package com.vertexforge.launcher.model;

import java.nio.file.Path;

/**
 * Result object for project creation operations.
 * <p>
 * Provides clear success/failure semantics with an optional error message
 * for failures and the project path for successful creations.
 * </p>
 *
 * @param success      true if the project was created successfully
 * @param projectPath  path to the created .vfproj file (null if failed)
 * @param errorMessage description of the error (null if success)
 */
public record ProjectCreationResult(
        boolean success,
        Path projectPath,
        String errorMessage
) {
    /**
     * Creates a successful result with the project path.
     *
     * @param projectPath path to the created .vfproj file
     * @return a success result
     */
    public static ProjectCreationResult success(Path projectPath) {
        return new ProjectCreationResult(true, projectPath, null);
    }

    /**
     * Creates a failure result with an error message.
     *
     * @param errorMessage description of what went wrong
     * @return a failure result
     */
    public static ProjectCreationResult failure(String errorMessage) {
        return new ProjectCreationResult(false, null, errorMessage);
    }
}
