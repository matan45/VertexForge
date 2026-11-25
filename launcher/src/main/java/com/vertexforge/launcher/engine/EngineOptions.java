package com.vertexforge.launcher.engine;

import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;

/**
 * Every engine the launcher knows right now, each with its health (VK-1681): what the UI shows and what
 * {@link EngineResolver} chooses from.
 *
 * @param registry the registry they were read from (its state decides whether it may be changed)
 * @param override the {@code VERTEXFORGE_EDITOR_PATH} override, when the variable is set
 * @param engines  the registered engines in file order, followed by the bundled engine when it could not be saved
 */
public record EngineOptions(EngineRegistryView registry, Optional<EngineOption> override, List<EngineOption> engines) {

    public EngineOptions {
        Objects.requireNonNull(registry, "registry");
        override = override == null ? Optional.empty() : override;
        engines = List.copyOf(engines);
    }

    /** The registered engine with this id (as stored in a project's {@code engineId}); empty when it is unknown. */
    public Optional<EngineOption> find(String engineId) {
        return EngineRegistryCodec.uuid(engineId).flatMap(this::find);
    }

    public Optional<EngineOption> find(UUID id) {
        return engines.stream().filter(engine -> engine.id().equals(Optional.of(id))).findFirst();
    }

    /** The engine that ships with the running launcher, when there is one. */
    public Optional<EngineOption> bundled() {
        return engines.stream().filter(EngineOption::bundled).findFirst();
    }

    /** The engines that can start the editor (the override excluded). */
    public List<EngineOption> usable() {
        return engines.stream().filter(EngineOption::isOk).toList();
    }

    /** Whether Open can start an editor without asking for one to be added. */
    public boolean anyUsable() {
        return override.map(EngineOption::isOk).orElse(false) || !usable().isEmpty();
    }

    /** The override, when it is set and works. */
    public Optional<EngineOption> activeOverride() {
        return override.filter(EngineOption::isOk);
    }
}
