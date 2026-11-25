package com.vertexforge.launcher.service;

import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.store.StoreWriteException;

import java.nio.file.Path;
import java.util.Objects;

/**
 * What {@link ProjectHub#createProject} did (VK-1678). A project can be created and still not be listed (the list is
 * read-only or could not be saved); {@code listError} then says why, so the project is never silently lost.
 *
 * @param result      the creation itself
 * @param projectPath the canonical path of the new {@code .vfproj}, or {@code null} when creation failed
 * @param view        the list after adding the project, or {@code null} when it was not added
 * @param listError   why the project was not added to the list, or {@code null}
 */
public record CreateOutcome(
        ProjectCreationResult result,
        Path projectPath,
        RecentProjectsView view,
        StoreWriteException listError
) {
    public CreateOutcome {
        Objects.requireNonNull(result, "result");
    }

    static CreateOutcome failed(ProjectCreationResult result) {
        return new CreateOutcome(result, null, null, null);
    }

    static CreateOutcome listed(ProjectCreationResult result, Path projectPath, RecentProjectsView view) {
        return new CreateOutcome(result, projectPath, Objects.requireNonNull(view, "view"), null);
    }

    static CreateOutcome notListed(ProjectCreationResult result, Path projectPath, StoreWriteException listError) {
        return new CreateOutcome(result, projectPath, null, Objects.requireNonNull(listError, "listError"));
    }

    public boolean created() {
        return result.success();
    }

    public boolean listed() {
        return view != null;
    }
}
