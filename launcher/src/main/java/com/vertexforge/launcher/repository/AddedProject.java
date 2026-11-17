package com.vertexforge.launcher.repository;

import com.vertexforge.launcher.model.RecentEntry;

import java.util.Objects;

/**
 * A project added with "Add Existing…", a drop or the command line (VK-1685).
 *
 * @param view    the list as written
 * @param entry   the project's entry in {@code view}
 * @param existed whether the project was listed already (only its name was refreshed then)
 */
public record AddedProject(RecentProjectsView view, RecentEntry entry, boolean existed) {
    public AddedProject {
        Objects.requireNonNull(view, "view");
        Objects.requireNonNull(entry, "entry");
    }
}
