package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;

/**
 * The registered engine installs as an immutable value (VK-1681). Every operation returns a new registry and never
 * touches the file system. The store applies these operations to what it has just re-read under its lock, so an
 * operation on an engine another launcher removed in the meantime does nothing.
 */
public final class EngineRegistry {

    private final List<EngineInstall> engines;
    private final UUID defaultEngineId;

    /** @param defaultEngineId the chosen default engine, or {@code null} when none was chosen */
    public EngineRegistry(List<EngineInstall> engines, UUID defaultEngineId) {
        this.engines = List.copyOf(engines);
        this.defaultEngineId = defaultEngineId;
    }

    public static EngineRegistry empty() {
        return new EngineRegistry(List.of(), null);
    }

    /** The installs in file order. */
    public List<EngineInstall> engines() {
        return engines;
    }

    /** The engine the user chose as the default; it may name an engine that is no longer registered. */
    public Optional<UUID> defaultEngineId() {
        return Optional.ofNullable(defaultEngineId);
    }

    public Optional<EngineInstall> find(UUID id) {
        return engines.stream().filter(engine -> engine.id().equals(id)).findFirst();
    }

    /** The install of this editor (paths compare as the file system does, so without regard to case on Windows). */
    public Optional<EngineInstall> findByPath(Path executable) {
        return engines.stream().filter(engine -> executable.equals(engine.executable())).findFirst();
    }

    /**
     * The result of {@link #add}.
     *
     * @param existed whether the editor was registered already; the registry is then unchanged
     */
    public record Added(EngineRegistry registry, EngineInstall engine, boolean existed) {
    }

    /** Registers the editor at {@code executable} (its real path), or returns the entry that already has it. */
    public Added add(Path executable, UUID newId) {
        Objects.requireNonNull(executable, "executable");
        Objects.requireNonNull(newId, "newId");
        Optional<EngineInstall> existing = findByPath(executable);
        if (existing.isPresent()) {
            return new Added(this, existing.get(), true);
        }
        EngineInstall created = EngineInstall.create(newId, executable);
        List<EngineInstall> next = new ArrayList<>(engines);
        next.add(created);
        return new Added(new EngineRegistry(next, defaultEngineId), created, false);
    }

    /** Unregisters the engine (never touching its files); it stops being the default. Does nothing when it is gone. */
    public EngineRegistry remove(UUID id) {
        List<EngineInstall> next = new ArrayList<>(engines);
        if (!next.removeIf(engine -> engine.id().equals(id))) {
            return this;
        }
        return new EngineRegistry(next, id.equals(defaultEngineId) ? null : defaultEngineId);
    }

    /**
     * Makes {@code id} the default engine, or clears the default for {@code null}. Does nothing when the engine is
     * not registered (another launcher removed it meanwhile).
     */
    public EngineRegistry withDefault(UUID id) {
        if (id == null) {
            return defaultEngineId == null ? this : new EngineRegistry(engines, null);
        }
        if (find(id).isEmpty() || id.equals(defaultEngineId)) {
            return this;
        }
        return new EngineRegistry(engines, id);
    }
}
