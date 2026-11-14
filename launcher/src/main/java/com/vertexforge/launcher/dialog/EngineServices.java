package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EngineOptions;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;

/**
 * What the engine dialogs ask of the main window (VK-1681). Every result completes on the FX thread; the work itself
 * runs in the background, so a dialog never blocks on a slow drive.
 */
public interface EngineServices {

    /** Lists and checks every engine again. */
    CompletableFuture<EngineOptions> refresh();

    /** Checks a picked {@code Editor.exe} or {@code engine.json} and registers it, then lists and checks again. */
    CompletableFuture<AddOutcome> add(Path picked);

    /** Unregisters an engine (its files are never touched), then lists and checks again. */
    CompletableFuture<EngineOptions> remove(UUID id);

    /** Makes an engine the default, then lists and checks again. */
    CompletableFuture<EngineOptions> setDefault(UUID id);

    /** How many listed projects are bound to the engine. FX thread. */
    int projectsUsing(UUID id);

    /**
     * What {@link #add} did.
     *
     * @param options  the engines after the add; {@code null} when it was refused
     * @param engineId the added (or already registered) engine; empty when it was refused
     * @param existed  whether the engine was registered already
     * @param refusal  why the file was not added, or {@code null}
     */
    record AddOutcome(EngineOptions options, Optional<UUID> engineId, boolean existed, String refusal) {
        public AddOutcome {
            engineId = engineId == null ? Optional.empty() : engineId;
        }

        public static AddOutcome refused(String reason) {
            return new AddOutcome(null, Optional.empty(), false, Objects.requireNonNull(reason, "reason"));
        }

        public static AddOutcome added(EngineOptions options, UUID engineId, boolean existed) {
            return new AddOutcome(Objects.requireNonNull(options, "options"),
                    Optional.of(Objects.requireNonNull(engineId, "engineId")), existed, null);
        }

        public boolean isRefused() {
            return refusal != null;
        }
    }
}
