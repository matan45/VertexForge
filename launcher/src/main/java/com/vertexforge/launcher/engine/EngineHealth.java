package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.Objects;

/**
 * Whether an engine install can start the editor right now (VK-1681).
 * <ul>
 *   <li>{@link Kind#OK}: the editor can be started, in {@link #workingDirectory()}.</li>
 *   <li>{@link Kind#MISSING}: the engine's drive or network share cannot be reached right now (or did not answer in
 *       time). It may come back, so nothing about the engine is decided from this.</li>
 *   <li>{@link Kind#BROKEN}: the location is reachable, but the engine cannot be used: it was moved or deleted, its
 *       {@code engine.json} is not valid, or its working directory is gone.</li>
 * </ul>
 *
 * @param kind             the category
 * @param reason           a human-readable explanation, shown as a tooltip; empty for a plain OK
 * @param manifest         the engine's {@code engine.json}, when it has one that could be read; {@code null} otherwise
 * @param workingDirectory the folder the editor starts in; set exactly when the kind is OK
 */
public record EngineHealth(Kind kind, String reason, EngineManifest manifest, Path workingDirectory) {

    public enum Kind {
        OK("OK"),
        MISSING("Missing"),
        BROKEN("Broken");

        private final String label;

        Kind(String label) {
            this.label = label;
        }

        public String label() {
            return label;
        }
    }

    public EngineHealth {
        Objects.requireNonNull(kind, "kind");
        reason = reason == null ? "" : reason;
        if ((kind == Kind.OK) != (workingDirectory != null)) {
            throw new IllegalArgumentException("an OK engine has a working directory, and only an OK engine does");
        }
    }

    /** An engine that can start; {@code manifest} is {@code null} for an install without {@code engine.json}. */
    public static EngineHealth ok(EngineManifest manifest, Path workingDirectory) {
        Objects.requireNonNull(workingDirectory, "workingDirectory");
        String reason = manifest == null
                ? "There is no " + EngineManifest.FILE_NAME + " beside the editor, so its version is unknown; it"
                        + " starts in its own folder"
                : "Starts in " + workingDirectory;
        return new EngineHealth(Kind.OK, reason, manifest, workingDirectory);
    }

    public static EngineHealth missing(String reason) {
        return new EngineHealth(Kind.MISSING, reason, null, null);
    }

    public static EngineHealth broken(String reason) {
        return new EngineHealth(Kind.BROKEN, reason, null, null);
    }

    /** A broken engine whose manifest could still be read, so that the UI can still name its version. */
    public static EngineHealth broken(String reason, EngineManifest manifest) {
        return new EngineHealth(Kind.BROKEN, reason, manifest, null);
    }

    public boolean isOk() {
        return kind == Kind.OK;
    }

    public String label() {
        return kind.label();
    }
}
