package com.vertexforge.launcher.model;

import java.nio.file.Path;
import java.time.Instant;
import java.util.Objects;

/**
 * Immutable record representing metadata for a VertexForge project.
 * <p>
 * Tracks project location and access history for the launcher's recent projects list.
 * The project name is extracted from the .vfproj file when available.
 * </p>
 *
 * @param name       display name of the project (from .vfproj or directory name as fallback)
 * @param path       absolute path to the .vfproj file
 * @param lastOpened timestamp when the project was last opened in the launcher (UTC)
 */
public record ProjectInfo(
        String name,
        Path path,
        Instant lastOpened
) {
    /**
     * Compact constructor with validation.
     */
    public ProjectInfo {
        Objects.requireNonNull(name, "Project name cannot be null");
        Objects.requireNonNull(path, "Project path cannot be null");
        Objects.requireNonNull(lastOpened, "Last opened timestamp cannot be null");

        if (name.isBlank()) {
            throw new IllegalArgumentException("Project name cannot be blank");
        }
        if (!path.isAbsolute()) {
            throw new IllegalArgumentException("Project path must be absolute");
        }
    }

    /**
     * Creates a new ProjectInfo with an updated last opened timestamp.
     *
     * @param newLastOpened the new timestamp
     * @return a new ProjectInfo instance with the updated timestamp
     */
    public ProjectInfo withLastOpened(Instant newLastOpened) {
        return new ProjectInfo(this.name, this.path, newLastOpened);
    }

    /**
     * Creates a new ProjectInfo with an updated name.
     *
     * @param newName the new display name
     * @return a new ProjectInfo instance with the updated name
     */
    public ProjectInfo withName(String newName) {
        return new ProjectInfo(newName, this.path, this.lastOpened);
    }

    /**
     * Returns the project directory (parent of the .vfproj file).
     *
     * @return the directory containing the project
     */
    public Path projectDirectory() {
        return path.getParent();
    }

    /**
     * Factory method to create a ProjectInfo with the current timestamp.
     *
     * @param name the project name
     * @param path the absolute path to the .vfproj file
     * @return a new ProjectInfo instance
     */
    public static ProjectInfo create(String name, Path path) {
        return new ProjectInfo(name, path, Instant.now());
    }
}
