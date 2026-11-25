package com.vertexforge.launcher.repository;

import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.store.StoreStatus;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;

/**
 * What the launcher shows: the recent projects plus the state of the file they came from (VK-1678).
 *
 * @param entries         most recently opened first
 * @param status          what the last read or write of the file found
 * @param quarantinedTo   where a corrupt file was moved, when that just happened
 * @param detail          the reason behind a non-OK status, or an empty string
 * @param skippedElements list elements that were not entries at all (e.g. {@code null}) and were not kept
 */
public record RecentProjectsView(
        List<RecentEntry> entries,
        StoreStatus status,
        Optional<Path> quarantinedTo,
        String detail,
        int skippedElements
) {
    public RecentProjectsView {
        entries = List.copyOf(entries);
        Objects.requireNonNull(status, "status");
        quarantinedTo = quarantinedTo == null ? Optional.empty() : quarantinedTo;
        detail = detail == null ? "" : detail;
    }

    /** Whether the list must not be changed: a newer, corrupt-and-unmovable or unreadable file. */
    public boolean readOnly() {
        return status.readOnly();
    }

    public boolean isEmpty() {
        return entries.isEmpty();
    }

    public Optional<RecentEntry> find(UUID id) {
        return entries.stream().filter(entry -> entry.id().equals(id)).findFirst();
    }

    /** The entry with this path (paths compare as the file system does). */
    public Optional<RecentEntry> findByPath(Path path) {
        return entries.stream().filter(entry -> path.equals(entry.path())).findFirst();
    }
}
