package com.vertexforge.launcher.project;

import java.util.Objects;
import java.util.Optional;

/**
 * What the launcher reads from a {@code .vfproj} (VK-1685). Only the name is required; the versions are kept as the
 * raw text the file holds, for the stories that compare them (VK-1686, VK-1690's {@link VersionCompatibility}).
 *
 * @param projectName        the file's {@code projectName}; never blank
 * @param schemaVersion      the file's {@code schemaVersion} text, or {@code null} when it is missing or not a string
 * @param engineVersion      the file's {@code engineVersion} text, or {@code null} when it is missing or not a string
 * @param structureProblem   the editor's message for the first member it would refuse ({@link EditorProjectRules}),
 *                           or {@code null} when it would accept them all (VK-1686)
 * @param workingDirectory   the file's {@code workingDirectory} text as written (relative to the {@code .vfproj}'s
 *                           folder, or absolute), or {@code null} when it is missing or not a string (VK-1690)
 * @param assetFormatVersion the file's {@code assetFormatVersion} (VK-1690): the member's text when it is a string;
 *                           {@code null} when it is absent or JSON {@code null} (a project no engine has stamped yet);
 *                           for any other JSON type, that value's JSON text (such as {@code 1} or
 *                           {@code {"major":1}}), which is never a version, so it reads as malformed rather than as
 *                           absent
 */
public record ProjectFile(String projectName, String schemaVersion, String engineVersion, String structureProblem,
                          String workingDirectory, String assetFormatVersion) {

    public ProjectFile {
        Objects.requireNonNull(projectName, "projectName");
        if (projectName.isBlank()) {
            throw new IllegalArgumentException("projectName must not be blank");
        }
    }

    /** A project file whose members the editor accepts, without a working directory or an asset format. */
    public ProjectFile(String projectName, String schemaVersion, String engineVersion) {
        this(projectName, schemaVersion, engineVersion, null, null, null);
    }

    /** A project file without a working directory or an asset format. */
    public ProjectFile(String projectName, String schemaVersion, String engineVersion, String structureProblem) {
        this(projectName, schemaVersion, engineVersion, structureProblem, null, null);
    }

    /** The schema version as the editor reads it; empty when it is missing or not {@code MAJOR.MINOR}. */
    public Optional<SchemaVersion> schema() {
        return SchemaVersion.parse(schemaVersion);
    }
}
