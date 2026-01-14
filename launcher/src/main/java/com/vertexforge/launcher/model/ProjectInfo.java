package com.vertexforge.launcher.model;

import java.nio.file.Path;
import java.time.Instant;
import java.util.Objects;


public record ProjectInfo(
        String name,
        Path path,
        Instant lastOpened
) {
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

    public ProjectInfo withLastOpened(Instant newLastOpened) {
        return new ProjectInfo(this.name, this.path, newLastOpened);
    }

    public static ProjectInfo create(String name, Path path) {
        return new ProjectInfo(name, path, Instant.now());
    }
}
