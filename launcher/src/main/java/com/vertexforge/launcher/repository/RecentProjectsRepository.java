package com.vertexforge.launcher.repository;

import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.repository.RecentProjectsCodec.Decoded;
import com.vertexforge.launcher.store.AtomicJsonStore;
import com.vertexforge.launcher.store.StoreSnapshot;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;

import java.nio.file.Path;
import java.time.Clock;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
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
        Objects.requireNonNull(path, "path");
        UUID newId = ids.get();
        return change("add " + path, projects -> {
            RecentProjects.Added added = projects.add(path, name, clock.instant(), newId);
            return added.projects().trim(limit, added.id());
        });
    }

    /** Records that the project was just opened. Does nothing when another launcher removed it meanwhile. */
    public RecentProjectsView markOpened(UUID id) throws StoreWriteException {
        return change("mark " + id + " as opened", projects -> projects.touch(id, clock.instant()));
    }

    /** Removes the entry from the list; the project on disk is never touched. */
    public RecentProjectsView remove(UUID id) throws StoreWriteException {
        return change("remove " + id, projects -> projects.remove(id));
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
