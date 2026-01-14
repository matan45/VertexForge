package com.vertexforge.launcher.model;

import java.nio.file.Path;

public record ProjectCreationResult(
        boolean success,
        Path projectPath,
        String errorMessage
) {

    public static ProjectCreationResult success(Path projectPath) {
        return new ProjectCreationResult(true, projectPath, null);
    }

    public static ProjectCreationResult failure(String errorMessage) {
        return new ProjectCreationResult(false, null, errorMessage);
    }
}
