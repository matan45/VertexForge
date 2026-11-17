package com.vertexforge.launcher.project;

import java.util.Objects;

/**
 * What the launcher reads from a {@code .vfproj} (VK-1685). Only the name is required; the versions are kept as the
 * raw text the file holds, for the stories that compare them (VK-1686, VK-1690).
 *
 * @param projectName   the file's {@code projectName}; never blank
 * @param schemaVersion the file's {@code schemaVersion} text, or {@code null} when it is missing or not a string
 * @param engineVersion the file's {@code engineVersion} text, or {@code null} when it is missing or not a string
 */
public record ProjectFile(String projectName, String schemaVersion, String engineVersion) {

    public ProjectFile {
        Objects.requireNonNull(projectName, "projectName");
        if (projectName.isBlank()) {
            throw new IllegalArgumentException("projectName must not be blank");
        }
    }
}
