package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.Objects;

/** What a file picked with "Add Engine…" turned out to be (VK-1681). */
public sealed interface PickedEngine {

    /**
     * An engine that can be registered.
     *
     * @param realExecutable the editor's real path ({@code toRealPath}), the form the registry stores
     * @param health         its check, always OK
     */
    record Usable(Path realExecutable, EngineHealth health) implements PickedEngine {
        public Usable {
            Objects.requireNonNull(realExecutable, "realExecutable");
            Objects.requireNonNull(health, "health");
            if (!health.isOk()) {
                throw new IllegalArgumentException("only an OK engine is usable");
            }
        }
    }

    /** A file that is not a usable engine; {@code reason} says why, for the user. */
    record Refused(String reason) implements PickedEngine {
        public Refused {
            Objects.requireNonNull(reason, "reason");
        }
    }
}
