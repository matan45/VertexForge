package com.vertexforge.launcher.model;

import java.nio.file.Path;
import java.util.Objects;

/**
 * What a project creation did (VK-1679): the project was created, it failed, or it was cancelled. A failed or
 * cancelled creation has written nothing outside its own staging folder; when even that could not be removed, the
 * message says where it is.
 *
 * @param status       what happened
 * @param projectPath  the new {@code .vfproj}, or {@code null} unless {@code status} is {@code CREATED}
 * @param errorMessage why it failed, a note about what was left behind after a cancel, or {@code null}
 */
public record ProjectCreationResult(
        Status status,
        Path projectPath,
        String errorMessage
) {

    public enum Status {
        CREATED,
        FAILED,
        CANCELLED
    }

    public ProjectCreationResult {
        Objects.requireNonNull(status, "status");
    }

    public static ProjectCreationResult success(Path projectPath) {
        return new ProjectCreationResult(Status.CREATED, Objects.requireNonNull(projectPath, "projectPath"), null);
    }

    public static ProjectCreationResult failure(String errorMessage) {
        return new ProjectCreationResult(Status.FAILED, null, errorMessage);
    }

    /** @param note what the cancel left behind, or {@code null} when it left nothing */
    public static ProjectCreationResult cancelled(String note) {
        return new ProjectCreationResult(Status.CANCELLED, null, note);
    }

    /** The project exists and is complete. */
    public boolean success() {
        return status == Status.CREATED;
    }

    public boolean cancelled() {
        return status == Status.CANCELLED;
    }
}
