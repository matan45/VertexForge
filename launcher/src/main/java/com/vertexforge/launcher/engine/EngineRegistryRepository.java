package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.engine.EngineRegistryCodec.Decoded;
import com.vertexforge.launcher.store.AtomicJsonStore;
import com.vertexforge.launcher.store.StoreSnapshot;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;

import java.nio.file.Path;
import java.time.Clock;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;
import java.util.function.UnaryOperator;

/**
 * The engine installs in {@code ~/.vertexforge/engines.json} (VK-1681), kept with the same atomic store as the recent
 * projects (VK-1678): loading never drops an engine, and every change re-reads the file under the store's lock and
 * applies itself to what it read, so launchers running at the same time never lose each other's changes. A change
 * that cannot be saved throws {@link StoreWriteException}, and the file stays as it was. Nothing here ever looks at
 * an engine's own files.
 */
public class EngineRegistryRepository {

    /** The store's file name inside the launcher's settings folder. */
    public static final String FILE_NAME = "engines.json";

    private final AtomicJsonStore store;
    private final Supplier<UUID> ids;
    private final LauncherLog log;

    public EngineRegistryRepository(AtomicJsonStore store, Supplier<UUID> ids, LauncherLog log) {
        this.store = Objects.requireNonNull(store, "store");
        this.ids = Objects.requireNonNull(ids, "ids");
        this.log = Objects.requireNonNull(log, "log");
    }

    /** The store for the engines file at {@code file}, logging what it recovers from to {@code log}. */
    public static AtomicJsonStore openStore(Path file, Clock clock, LauncherLog log) {
        return new AtomicJsonStore(file, EngineRegistryCodec.FORMAT, clock, AtomicJsonStore.DEFAULT_LOCK_TIMEOUT,
                log::warn);
    }

    public Path file() {
        return store.file();
    }

    /** Reads the list. A corrupt file is moved aside (see {@link AtomicJsonStore#read()}). Never throws. */
    public EngineRegistryView load() {
        StoreSnapshot snapshot = store.read();
        switch (snapshot.status()) {
            case NEWER_READ_ONLY -> log.warn("The engines file " + store.file()
                    + " was written by a newer version of the launcher; it is read-only here");
            case UNREADABLE -> log.warn("Could not read the engines file " + store.file() + ": " + snapshot.detail());
            default -> {
                // Quarantines and unmovable files are reported by the store itself.
            }
        }
        return view(snapshot, EngineRegistryCodec.decode(snapshot.root()));
    }

    /**
     * What {@link #add} did.
     *
     * @param view    the list as written
     * @param engine  the entry of the added editor (the existing one when it was registered already)
     * @param existed whether the editor was registered already. The list is then unchanged, and a file this launcher
     *                wrote is not rewritten; a hand-written one may still be normalized (a missing
     *                {@code defaultEngineId} or {@code version}, an id in another form), as for the recent projects
     */
    public record Added(EngineRegistryView view, EngineInstall engine, boolean existed) {
    }

    /** Registers the editor at {@code realExecutable}, or finds the entry that already has it. */
    public Added add(Path realExecutable) throws StoreWriteException {
        Objects.requireNonNull(realExecutable, "realExecutable");
        // Drawn outside the update: the store runs the change once, under its lock.
        UUID newId = ids.get();
        AtomicReference<EngineRegistry.Added> result = new AtomicReference<>();
        EngineRegistryView view = change("add " + realExecutable, registry -> {
            EngineRegistry.Added added = registry.add(realExecutable, newId);
            result.set(added);
            return added.registry();
        });
        return new Added(view, result.get().engine(), result.get().existed());
    }

    /** Unregisters the engine; its files are never touched. Clears the default when it was this engine. */
    public EngineRegistryView remove(UUID id) throws StoreWriteException {
        Objects.requireNonNull(id, "id");
        return change("remove engine " + id, registry -> registry.remove(id));
    }

    /** Makes {@code id} the default engine; {@code null} clears the default. */
    public EngineRegistryView setDefault(UUID id) throws StoreWriteException {
        return change("set the default engine to " + id, registry -> registry.withDefault(id));
    }

    private EngineRegistryView change(String action, UnaryOperator<EngineRegistry> operation)
            throws StoreWriteException {
        AtomicInteger skipped = new AtomicInteger();
        StoreSnapshot snapshot;
        try {
            snapshot = store.update(root -> {
                Decoded current = EngineRegistryCodec.decode(root);
                skipped.set(current.skippedElements());
                EngineRegistry next = operation.apply(current.registry());
                return EngineRegistryCodec.encode(next, root);
            });
        } catch (StoreWriteException e) {
            log.warn("Failed to save the engines list (" + action + "): " + e.getMessage());
            throw e;
        }
        if (skipped.get() > 0) {
            log.warn(skippedMessage(skipped.get()) + "; they were not written back");
        }
        return view(snapshot, EngineRegistryCodec.decode(snapshot.root()));
    }

    private EngineRegistryView view(StoreSnapshot snapshot, Decoded decoded) {
        if (decoded.skippedElements() > 0) {
            log.warn(skippedMessage(decoded.skippedElements()));
        }
        return new EngineRegistryView(decoded.registry().engines(), decoded.registry().defaultEngineId(),
                snapshot.status(), snapshot.quarantinedTo(), snapshot.detail(), decoded.skippedElements());
    }

    private String skippedMessage(int count) {
        return "The engines file " + store.file() + " held " + count
                + (count == 1 ? " element that is not an engine" : " elements that are not engines");
    }
}
