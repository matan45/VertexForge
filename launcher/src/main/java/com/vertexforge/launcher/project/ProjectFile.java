package com.vertexforge.launcher.project;

import java.util.Objects;
import java.util.Optional;

/**
 * What the launcher reads from a {@code .vfproj} (VK-1685). Only the name is required; the versions are kept as the
 * raw text the file holds, for the stories that compare them (VK-1686, VK-1690).
 *
 * @param projectName      the file's {@code projectName}; never blank
 * @param schemaVersion    the file's {@code schemaVersion} text, or {@code null} when it is missing or not a string
 * @param engineVersion    the file's {@code engineVersion} text, or {@code null} when it is missing or not a string
 * @param structureProblem the editor's message for the first member it would refuse ({@link EditorProjectRules}), or
 *                         {@code null} when it would accept them all (VK-1686)
 */
public record ProjectFile(String projectName, String schemaVersion, String engineVersion, String structureProblem) {

    public ProjectFile {
        Objects.requireNonNull(projectName, "projectName");
        if (projectName.isBlank()) {
            throw new IllegalArgumentException("projectName must not be blank");
        }
    }

    /** A project file whose members the editor accepts. */
    public ProjectFile(String projectName, String schemaVersion, String engineVersion) {
        this(projectName, schemaVersion, engineVersion, null);
    }

    /** The schema version as the editor reads it; empty when it is missing or not {@code MAJOR.MINOR}. */
    public Optional<SchemaVersion> schema() {
        return SchemaVersion.parse(schemaVersion);
    }
}
