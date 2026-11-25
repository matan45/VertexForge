package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.store.StoreStatus;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;

/**
 * The registered engines plus the state of {@code engines.json} they came from (VK-1681).
 *
 * @param engines         in file order
 * @param defaultEngineId the chosen default, which may name an engine that is no longer registered
 * @param status          what the last read or write of the file found
 * @param quarantinedTo   where a corrupt file was moved, when that just happened
 * @param detail          the reason behind a non-OK status, or an empty string
 * @param skippedElements list elements that were not entries at all and were not kept
 */
public record EngineRegistryView(
        List<EngineInstall> engines,
        Optional<UUID> defaultEngineId,
        StoreStatus status,
        Optional<Path> quarantinedTo,
        String detail,
        int skippedElements
) {
    public EngineRegistryView {
        engines = List.copyOf(engines);
        defaultEngineId = defaultEngineId == null ? Optional.empty() : defaultEngineId;
        Objects.requireNonNull(status, "status");
        quarantinedTo = quarantinedTo == null ? Optional.empty() : quarantinedTo;
        detail = detail == null ? "" : detail;
    }

    /** Whether the list must not be changed: a newer, corrupt-and-unmovable or unreadable file. */
    public boolean readOnly() {
        return status.readOnly();
    }

    public Optional<EngineInstall> find(UUID id) {
        return engines.stream().filter(engine -> engine.id().equals(id)).findFirst();
    }

    /** The install of this editor (paths compare as the file system does). */
    public Optional<EngineInstall> findByPath(Path executable) {
        return engines.stream().filter(engine -> executable.equals(engine.executable())).findFirst();
    }
}
