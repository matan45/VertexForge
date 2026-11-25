package com.vertexforge.launcher.repository;

import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.model.RecentProjects.Relink;

import java.util.Objects;
import java.util.Optional;

/**
 * A "Locate…" (VK-1686): the list as written and what happened to the entry.
 *
 * @param view     the list as written
 * @param outcome  what the relink did
 * @param conflict the entry that already lists the picked file, for {@link Relink#ALREADY_LISTED}
 */
public record RelinkedProject(RecentProjectsView view, Relink outcome, Optional<RecentEntry> conflict) {
    public RelinkedProject {
        Objects.requireNonNull(view, "view");
        Objects.requireNonNull(outcome, "outcome");
        conflict = conflict == null ? Optional.empty() : conflict;
    }
}
