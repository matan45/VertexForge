package com.vertexforge.launcher.model;

import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;

/**
 * The recent-projects list as an immutable value (VK-1678). Every operation returns a new list and never touches the
 * file system, so a project whose drive is offline is kept like any other.
 * <p>
 * The store applies these operations to the list it has just re-read under its lock. An operation on an entry that
 * another launcher removed in the meantime does nothing, so it never brings that entry back.
 */
public final class RecentProjects {

    /** Oldest first; an entry without a timestamp counts as the oldest. */
    private static final Comparator<RecentEntry> OLDEST_FIRST =
            Comparator.comparing(RecentEntry::lastOpened, Comparator.nullsFirst(Comparator.naturalOrder()));

    private final List<RecentEntry> entries;

    public RecentProjects(List<RecentEntry> entries) {
        this.entries = List.copyOf(entries);
    }

    public static RecentProjects empty() {
        return new RecentProjects(List.of());
    }

    /** The entries in file order. */
    public List<RecentEntry> entries() {
        return entries;
    }

    public int size() {
        return entries.size();
    }

    public boolean isEmpty() {
        return entries.isEmpty();
    }

    public Optional<RecentEntry> find(UUID id) {
        return entries.stream().filter(entry -> entry.id().equals(id)).findFirst();
    }

    /** Most recently opened first; entries without a timestamp last. Ties keep their file order. */
    public List<RecentEntry> displayOrder() {
        return entries.stream()
                .sorted(Comparator.comparing(RecentEntry::lastOpened,
                        Comparator.nullsLast(Comparator.<Instant>reverseOrder())))
                .toList();
    }

    /** The result of {@link #add}: the new list and the id of the added or updated entry. */
    public record Added(RecentProjects projects, UUID id) {
    }

    /**
     * Adds a project, or, when an entry already has this path (paths compare as the file system does, so without
     * regard to letter case on Windows), updates that entry's name and time and keeps its id and other members.
     */
    public Added add(Path path, String name, Instant now, UUID newId) {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(now, "now");
        Objects.requireNonNull(newId, "newId");
        List<RecentEntry> next = new ArrayList<>(entries);
        for (int i = 0; i < next.size(); i++) {
            RecentEntry existing = next.get(i);
            if (path.equals(existing.path())) {
                RecentEntry updated = existing.withName(name).withLastOpened(now);
                next.set(i, updated);
                return new Added(new RecentProjects(next), updated.id());
            }
        }
        next.add(RecentEntry.create(newId, path, name, now));
        return new Added(new RecentProjects(next), newId);
    }

    /** Records that the entry was opened at {@code now}; does nothing when the id is gone. */
    public RecentProjects touch(UUID id, Instant now) {
        Objects.requireNonNull(now, "now");
        List<RecentEntry> next = new ArrayList<>(entries);
        for (int i = 0; i < next.size(); i++) {
            if (next.get(i).id().equals(id)) {
                next.set(i, next.get(i).withLastOpened(now));
                return new RecentProjects(next);
            }
        }
        return this;
    }

    /** Removes the entry; does nothing when the id is gone. Never touches the project on disk. */
    public RecentProjects remove(UUID id) {
        List<RecentEntry> next = new ArrayList<>(entries);
        return next.removeIf(entry -> entry.id().equals(id)) ? new RecentProjects(next) : this;
    }

    /**
     * Drops the oldest entries until at most {@code limit} remain. A pinned entry and {@code keepId} (the entry just
     * added) are never dropped, so the list may stay above the limit when only those are left.
     */
    public RecentProjects trim(int limit, UUID keepId) {
        if (limit < 1) {
            throw new IllegalArgumentException("limit must be at least 1");
        }
        if (entries.size() <= limit) {
            return this;
        }
        List<RecentEntry> next = new ArrayList<>(entries);
        while (next.size() > limit) {
            Optional<RecentEntry> oldest = next.stream()
                    .filter(entry -> !entry.pinned() && !entry.id().equals(keepId))
                    .min(OLDEST_FIRST);
            if (oldest.isEmpty()) {
                break;
            }
            UUID victim = oldest.get().id();
            next.removeIf(entry -> entry.id().equals(victim));
        }
        return new RecentProjects(next);
    }
}
