package com.vertexforge.launcher.repository;

import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.repository.RecentProjectsCodec.Decoded;
import com.vertexforge.launcher.store.AtomicJsonStore;
import com.vertexforge.launcher.store.StoreSnapshot;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;

import java.nio.file.Path;
import java.time.Clock;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;
import java.util.function.UnaryOperator;

/**
 * The recent-projects list in {@code ~/.vertexforge/recent-projects.json} (VK-1678).
 * <p>
 * Loading never drops a project and never touches the projects themselves. Every change re-reads the file under the
 * store's lock and applies itself to what it read, so launchers running at the same time never lose each other's
 * changes. A change that cannot be saved throws {@link StoreWriteException}, and the file stays as it was.
 */
public class RecentProjectsRepository {

    /** The store's file name inside the launcher's settings folder. */
    public static final String FILE_NAME = "recent-projects.json";

    private final AtomicJsonStore store;
    private final Supplier<UUID> ids;
    private final Clock clock;
    private final LauncherLog log;

    public RecentProjectsRepository(AtomicJsonStore store, Supplier<UUID> ids, Clock clock, LauncherLog log) {
        this.store = Objects.requireNonNull(store, "store");
        this.ids = Objects.requireNonNull(ids, "ids");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.log = Objects.requireNonNull(log, "log");
    }

    /** The store for the recent-projects file at {@code file}, logging what it recovers from to {@code log}. */
    public static AtomicJsonStore openStore(Path file, Clock clock, LauncherLog log) {
        return new AtomicJsonStore(file, RecentProjectsCodec.FORMAT, clock, AtomicJsonStore.DEFAULT_LOCK_TIMEOUT,
                log::warn);
    }

    public Path file() {
        return store.file();
    }

    /** Reads the list. A corrupt file is moved aside (see {@link AtomicJsonStore#read()}). Never throws. */
    public RecentProjectsView load() {
        StoreSnapshot snapshot = store.read();
        switch (snapshot.status()) {
            case NEWER_READ_ONLY -> log.warn("The recent projects file " + store.file()
                    + " was written by a newer version of the launcher; it is read-only here");
            case UNREADABLE -> log.warn("Could not read the recent projects file " + store.file() + ": "
                    + snapshot.detail());
            default -> {
                // Quarantines and unmovable files are reported by the store itself.
            }
        }
        return view(snapshot, RecentProjectsCodec.decode(snapshot.root()));
    }

    /**
     * Adds a project (or, when the path is already listed, refreshes that entry's name and time), then trims the
     * list to {@code limit}, never dropping the new entry or a pinned one.
     */
    public RecentProjectsView add(Path path, String name, int limit) throws StoreWriteException {
        return add(path, name, limit, Optional.empty());
    }

    /**
     * As {@link #add(Path, String, int)}, and, when {@code engineId} is given, binds the entry to that engine in the
     * same write (a project just created from that engine's templates, VK-1689). Without one the entry's binding is
     * left as it is.
     */
    public RecentProjectsView add(Path path, String name, int limit, Optional<UUID> engineId)
            throws StoreWriteException {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(engineId, "engineId");
        UUID newId = ids.get();
        return change("add " + path, projects -> {
            RecentProjects.Added added = projects.add(path, name, clock.instant(), newId);
            RecentProjects bound = engineId
                    .map(engine -> added.projects().bindEngine(added.id(), engine.toString()))
                    .orElse(added.projects());
            return bound.trim(limit, added.id());
        });
    }

    /**
     * Adds a project that already exists on disk (VK-1685) under its real path, or, when that path is listed, only
     * refreshes the listed entry's name; then trims the list to {@code limit}, never dropping that entry or a pinned
     * one.
     *
     * @param path the project file's real path
     * @param name the project file's {@code projectName}
     */
    public AddedProject addExisting(Path path, String name, int limit) throws StoreWriteException {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(name, "name");
        UUID newId = ids.get();
        AtomicReference<RecentProjects.Added> added = new AtomicReference<>();
        RecentProjectsView view = change("add existing " + path, projects -> {
            RecentProjects.Added result = projects.addExisting(path, name, clock.instant(), newId);
            added.set(result);
            return result.projects().trim(limit, result.id());
        });
        UUID id = added.get().id();
        RecentEntry entry = view.find(id)
                .orElseThrow(() -> new IllegalStateException("The added project " + id + " is not in the list"));
        return new AddedProject(view, entry, added.get().existed());
    }

    /**
     * As {@link #add(Path, String, int, Optional)}, returning the entry: a copy of a project made by "Open a copy"
     * (VK-1690), listed under its real path and bound to the engine it opens with, in one write.
     *
     * @param path the copy's project file, as its real path
     */
    public AddedProject addBound(Path path, String name, int limit, Optional<UUID> engineId)
            throws StoreWriteException {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(engineId, "engineId");
        UUID newId = ids.get();
        AtomicReference<RecentProjects.Added> added = new AtomicReference<>();
        RecentProjectsView view = change("add " + path, projects -> {
            RecentProjects.Added result = projects.add(path, name, clock.instant(), newId);
            added.set(result);
            RecentProjects bound = engineId
                    .map(engine -> result.projects().bindEngine(result.id(), engine.toString()))
                    .orElse(result.projects());
            return bound.trim(limit, result.id());
        });
        UUID id = added.get().id();
        RecentEntry entry = view.find(id)
                .orElseThrow(() -> new IllegalStateException("The added project " + id + " is not in the list"));
        return new AddedProject(view, entry, added.get().existed());
    }

    /**
     * As {@link #addBound}, with the binding copied as an entry stores it: a duplicate made by "Duplicate" (VK-1695)
     * opens with the engine its original opens with. {@code rawEngineId} is another entry's
     * {@link RecentEntry#engineId()}, kept verbatim, even when it is not a valid id (the engine resolver then asks, as
     * it does for the original); {@code null} or blank leaves the entry unbound, following the default engine.
     *
     * @param path the duplicate's project file, as its real path
     */
    public AddedProject addBoundRaw(Path path, String name, int limit, String rawEngineId)
            throws StoreWriteException {
        Objects.requireNonNull(path, "path");
        UUID newId = ids.get();
        AtomicReference<RecentProjects.Added> added = new AtomicReference<>();
        RecentProjectsView view = change("add " + path, projects -> {
            RecentProjects.Added result = projects.add(path, name, clock.instant(), newId);
            added.set(result);
            RecentProjects bound = rawEngineId == null || rawEngineId.isBlank()
                    ? result.projects()
                    : result.projects().bindEngine(result.id(), rawEngineId);
            return bound.trim(limit, result.id());
        });
        UUID id = added.get().id();
        RecentEntry entry = view.find(id)
                .orElseThrow(() -> new IllegalStateException("The added project " + id + " is not in the list"));
        return new AddedProject(view, entry, added.get().existed());
    }

    /** Records that the project was just opened. Does nothing when another launcher removed it meanwhile. */
    public RecentProjectsView markOpened(UUID id) throws StoreWriteException {
        return markOpened(id, Optional.empty());
    }

    /**
     * Records that the project was just opened and, when {@code bindEngine} is given, binds it to that engine
     * (VK-1681), in one write. Does nothing when another launcher removed it meanwhile.
     */
    public RecentProjectsView markOpened(UUID id, Optional<UUID> bindEngine) throws StoreWriteException {
        Objects.requireNonNull(bindEngine, "bindEngine");
        return change("mark " + id + " as opened", projects -> {
            RecentProjects touched = projects.touch(id, clock.instant());
            return bindEngine.map(engine -> touched.bindEngine(id, engine.toString())).orElse(touched);
        });
    }

    /** Binds the project to an engine ({@code null}: it follows the default engine again), VK-1681. */
    public RecentProjectsView bindEngine(UUID id, UUID engineId) throws StoreWriteException {
        return change("bind " + id + " to engine " + engineId,
                projects -> projects.bindEngine(id, engineId == null ? null : engineId.toString()));
    }

    /** Sets the project's "Open with MCP" (VK-1683). Does nothing when another launcher removed it meanwhile. */
    public RecentProjectsView setMcp(UUID id, McpSetting setting) throws StoreWriteException {
        Objects.requireNonNull(setting, "setting");
        return change("set MCP of " + id, projects -> projects.setMcp(id, setting));
    }

    /** Pins the project to the top of the list or unpins it (VK-1694). Does nothing when it was removed meanwhile. */
    public RecentProjectsView setPinned(UUID id, boolean pinned) throws StoreWriteException {
        return change((pinned ? "pin " : "unpin ") + id, projects -> projects.setPinned(id, pinned));
    }

    /**
     * Removes the oldest unpinned entries until at most {@code limit} remain (a lowered cap, VK-1694); pinned entries
     * are always kept, and the projects on disk are never touched.
     */
    public RecentProjectsView trim(int limit) throws StoreWriteException {
        if (limit < 1) {
            throw new IllegalArgumentException("limit must be at least 1");
        }
        return change("trim to " + limit, projects -> projects.trim(limit, null));
    }

    /**
     * Records that the project's "unknown version" warning was shown (VK-1690). Does nothing when another launcher
     * removed it meanwhile.
     */
    public RecentProjectsView setUnknownVersionWarned(UUID id) throws StoreWriteException {
        return change("record the version warning of " + id, projects -> projects.setUnknownVersionWarned(id));
    }

    /**
     * Removes the entry from the list; the project on disk is never touched. The removal holds the entry as the list
     * held it, read under the store's lock, so Undo puts back exactly what was removed (VK-1686).
     */
    public RemovedProject remove(UUID id) throws StoreWriteException {
        AtomicReference<RemovedProject.Removal> removed = new AtomicReference<>();
        RecentProjectsView view = change("remove " + id, projects -> {
            int index = projects.indexOf(id);
            if (index < 0) {
                return projects;
            }
            removed.set(new RemovedProject.Removal(projects.entries().get(index), index));
            return projects.remove(id);
        });
        return new RemovedProject(view, Optional.ofNullable(removed.get()));
    }

    /**
     * Puts a removed entry back where it was (Undo, VK-1686), with its id and every member as they were. Does nothing
     * when its id or its path is listed again meanwhile.
     */
    public RecentProjectsView restore(RemovedProject.Removal removal) throws StoreWriteException {
        Objects.requireNonNull(removal, "removal");
        return change("restore " + removal.entry().id(),
                projects -> projects.restore(removal.entry(), removal.fileIndex()));
    }

    /**
     * Points the entry at the project file it was moved to (Locate…, VK-1686), keeping its id, pin, time, engine
     * binding and MCP setting, and refreshes its name.
     *
     * @param path the project file's real path
     * @param name the project file's {@code projectName}
     */
    public RelinkedProject relink(UUID id, Path path, String name) throws StoreWriteException {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(name, "name");
        AtomicReference<RecentProjects.Relinked> relinked = new AtomicReference<>();
        RecentProjectsView view = change("relink " + id + " to " + path, projects -> {
            RecentProjects.Relinked result = projects.relink(id, path, name);
            relinked.set(result);
            return result.projects();
        });
        RecentProjects.Relinked result = relinked.get();
        return new RelinkedProject(view, result.outcome(), Optional.ofNullable(result.conflict()));
    }

    private RecentProjectsView change(String action, UnaryOperator<RecentProjects> operation)
            throws StoreWriteException {
        AtomicInteger skipped = new AtomicInteger();
        StoreSnapshot snapshot;
        try {
            snapshot = store.update(root -> {
                Decoded current = RecentProjectsCodec.decode(root);
                skipped.set(current.skippedElements());
                RecentProjects next = operation.apply(current.projects());
                return RecentProjectsCodec.encode(next, root);
            });
        } catch (StoreWriteException e) {
            log.warn("Failed to save recent projects (" + action + "): " + e.getMessage());
            throw e;
        }
        if (skipped.get() > 0) {
            log.warn(skippedMessage(skipped.get()) + "; they were not written back");
        }
        return view(snapshot, RecentProjectsCodec.decode(snapshot.root()));
    }

    private RecentProjectsView view(StoreSnapshot snapshot, Decoded decoded) {
        if (decoded.skippedElements() > 0) {
            log.warn(skippedMessage(decoded.skippedElements()));
        }
        return new RecentProjectsView(decoded.projects().displayOrder(), snapshot.status(), snapshot.quarantinedTo(),
                snapshot.detail(), decoded.skippedElements());
    }

    private static String skippedMessage(int count) {
        return "Skipping " + count + " empty " + (count == 1 ? "entry" : "entries") + " in the recent projects file";
    }
}
