package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;

/**
 * One engine the launcher can offer, with its health as just checked (VK-1681).
 *
 * @param id         the registered install's id; empty for the override and for a bundled engine that could not be
 *                   saved (a read-only registry), neither of which a project can be bound to
 * @param executable the editor, or {@code null} when the stored path or the override's value is not a valid
 *                   absolute path
 * @param rawPath    the path as stored or set, for display
 * @param health     the check's result
 * @param bundled    whether this is the engine that ships with the running launcher
 * @param isDefault  whether the user chose it as the default engine
 * @param override   whether it comes from {@code VERTEXFORGE_EDITOR_PATH}
 */
public record EngineOption(
        Optional<UUID> id,
        Path executable,
        String rawPath,
        EngineHealth health,
        boolean bundled,
        boolean isDefault,
        boolean override
) {
    /** Shown for an engine without a readable {@code engine.json}. */
    public static final String UNKNOWN_VERSION = "Unknown version";

    public EngineOption {
        id = id == null ? Optional.empty() : id;
        Objects.requireNonNull(health, "health");
        rawPath = rawPath == null ? "" : rawPath;
    }

    public boolean isOk() {
        return health.isOk();
    }

    /** {@code 1.0.0 Development}, or {@link #UNKNOWN_VERSION}. */
    public String versionText() {
        return health.manifest() == null ? UNKNOWN_VERSION : health.manifest().describe();
    }

    /** {@code 1.0.0 Development (bundled, default)}: the version plus what makes this engine special. */
    public String label() {
        StringBuilder tags = new StringBuilder();
        if (override) {
            tags.append(EditorOverride.ENV_VAR);
        }
        if (bundled) {
            tags.append(tags.isEmpty() ? "" : ", ").append("bundled");
        }
        if (isDefault) {
            tags.append(tags.isEmpty() ? "" : ", ").append("default");
        }
        return tags.isEmpty() ? versionText() : versionText() + " (" + tags + ")";
    }

    /** The path to show: the editor's, else what was stored or set. */
    public String pathText() {
        return executable != null ? executable.toString() : rawPath;
    }
}
