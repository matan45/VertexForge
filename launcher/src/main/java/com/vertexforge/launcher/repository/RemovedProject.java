package com.vertexforge.launcher.repository;

import com.vertexforge.launcher.model.RecentEntry;

import java.util.Objects;
import java.util.Optional;

/**
 * A "Remove from List" (VK-1686): the list as written, and what Undo needs to put the entry back.
 *
 * @param view    the list as written
 * @param removal the entry that was removed, or empty when it was no longer listed (another launcher removed it)
 */
public record RemovedProject(RecentProjectsView view, Optional<RemovedProject.Removal> removal) {

    public RemovedProject {
        Objects.requireNonNull(view, "view");
        removal = removal == null ? Optional.empty() : removal;
    }

    /**
     * The removed entry as the list held it when it was removed (read under the store's lock), with its resolved id:
     * its stored JSON may have none (a version 1 entry), so the JSON alone could not give it back its id.
     *
     * @param entry     the whole entry
     * @param fileIndex its position in the file's order (the view shows another order)
     */
    public record Removal(RecentEntry entry, int fileIndex) {
        public Removal {
            Objects.requireNonNull(entry, "entry");
            if (fileIndex < 0) {
                throw new IllegalArgumentException("fileIndex must not be negative");
            }
        }
    }
}
