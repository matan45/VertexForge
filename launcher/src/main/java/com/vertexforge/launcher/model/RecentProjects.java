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

    /**
     * The result of {@link #add} and {@link #addExisting}: the new list, the id of the added or updated entry, and
     * whether that entry was listed already.
     */
    public record Added(RecentProjects projects, UUID id, boolean existed) {
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
                return new Added(new RecentProjects(next), updated.id(), true);
            }
        }
        next.add(RecentEntry.create(newId, path, name, now));
        return new Added(new RecentProjects(next), newId, false);
    }

    /**
     * Adds a project that already exists on disk (VK-1685), or, when an entry already has this path (compared as in
     * {@link #add}), refreshes only that entry's name from the project file. Its id, stored path, time, pin, engine
     * binding and other members stay, so adding a listed project never moves it or changes how it opens.
     *
     * @param path the project file's real path
     * @param name the project file's {@code projectName}
     */
    public Added addExisting(Path path, String name, Instant now, UUID newId) {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(now, "now");
        Objects.requireNonNull(newId, "newId");
        List<RecentEntry> next = new ArrayList<>(entries);
        for (int i = 0; i < next.size(); i++) {
            RecentEntry existing = next.get(i);
            if (path.equals(existing.path())) {
                RecentEntry renamed = existing.withName(name);
                if (renamed == existing) {
                    return new Added(this, existing.id(), true);
                }
                next.set(i, renamed);
                return new Added(new RecentProjects(next), existing.id(), true);
            }
        }
        next.add(RecentEntry.create(newId, path, name, now));
        return new Added(new RecentProjects(next), newId, false);
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

    /**
     * Binds the entry to an engine install ({@code null} unbinds it, VK-1681); does nothing when the id is gone or
     * the entry already has this binding.
     */
    public RecentProjects bindEngine(UUID id, String engineId) {
        List<RecentEntry> next = new ArrayList<>(entries);
        for (int i = 0; i < next.size(); i++) {
            RecentEntry entry = next.get(i);
            if (entry.id().equals(id)) {
                RecentEntry bound = entry.withEngineId(engineId);
                if (bound == entry) {
                    return this;
                }
                next.set(i, bound);
                return new RecentProjects(next);
            }
        }
        return this;
    }

    /** Sets the entry's "Open with MCP" (VK-1683); does nothing when the id is gone or nothing changes. */
    public RecentProjects setMcp(UUID id, McpSetting setting) {
        List<RecentEntry> next = new ArrayList<>(entries);
        for (int i = 0; i < next.size(); i++) {
            RecentEntry entry = next.get(i);
            if (entry.id().equals(id)) {
                RecentEntry changed = entry.withMcp(setting);
                if (changed == entry) {
                    return this;
                }
                next.set(i, changed);
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

    /** The entry's position in file order, or -1 when the id is not listed. */
    public int indexOf(UUID id) {
        for (int i = 0; i < entries.size(); i++) {
            if (entries.get(i).id().equals(id)) {
                return i;
            }
        }
        return -1;
    }

    /**
     * Puts a removed entry back (Undo, VK-1686) at {@code index} in file order, or at the end when the list has
     * become shorter. Does nothing when its id is listed again, or its path is (another launcher, or Add Existing,
     * listed the project again meanwhile). The list is not trimmed: the next add does that.
     *
     * @param entry the entry as it was removed, with its resolved id (its stored JSON may have none)
     */
    public RecentProjects restore(RecentEntry entry, int index) {
        Objects.requireNonNull(entry, "entry");
        for (RecentEntry listed : entries) {
            if (listed.id().equals(entry.id()) || (entry.path() != null && entry.path().equals(listed.path()))) {
                return this;
            }
        }
        List<RecentEntry> next = new ArrayList<>(entries);
        next.add(Math.clamp(index, 0, next.size()), entry);
        return new RecentProjects(next);
    }

    /** What {@link #relink} did. */
    public enum Relink {
        /** The entry now points at the new file. */
        RELINKED,
        /** The entry already pointed at that file; nothing changed (or only its name). */
        UNCHANGED,
        /** The entry is no longer listed (another launcher removed it). */
        GONE,
        /** Another entry already lists that file; nothing changed. */
        ALREADY_LISTED
    }

    /**
     * The result of {@link #relink}.
     *
     * @param conflict the entry that already lists the file, for {@link Relink#ALREADY_LISTED}; else {@code null}
     */
    public record Relinked(RecentProjects projects, Relink outcome, RecentEntry conflict) {
    }

    /**
     * Points an entry at the project file it was moved to (Locate…, VK-1686) and refreshes its name from that file.
     * The entry keeps its id, pin, last-opened time, engine binding, MCP setting and unknown members. Paths compare
     * as in {@link #add}.
     *
     * @param path the project file's real path
     * @param name the project file's {@code projectName}
     */
    public Relinked relink(UUID id, Path path, String name) {
        Objects.requireNonNull(id, "id");
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(name, "name");
        int index = indexOf(id);
        if (index < 0) {
            return new Relinked(this, Relink.GONE, null);
        }
        for (RecentEntry listed : entries) {
            if (!listed.id().equals(id) && path.equals(listed.path())) {
                return new Relinked(this, Relink.ALREADY_LISTED, listed);
            }
        }
        RecentEntry current = entries.get(index);
        boolean samePath = path.equals(current.path());
        RecentEntry relinked = (samePath ? current : current.withPath(path)).withName(name);
        if (relinked == current) {
            return new Relinked(this, Relink.UNCHANGED, null);
        }
        List<RecentEntry> next = new ArrayList<>(entries);
        next.set(index, relinked);
        return new Relinked(new RecentProjects(next), samePath ? Relink.UNCHANGED : Relink.RELINKED, null);
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
