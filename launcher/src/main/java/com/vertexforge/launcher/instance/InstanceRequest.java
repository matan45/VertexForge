package com.vertexforge.launcher.instance;

import java.nio.file.Path;
import java.util.Objects;

/**
 * One thing a launcher was asked to do with a project, from its own command line or forwarded by a second launcher
 * (VK-1685).
 *
 * @param action what to do with the project
 * @param path   the project file or folder, absolute and normalized (stored normalized)
 */
public record InstanceRequest(Action action, Path path) {

    /** {@code --add}: put the project in the list. {@code --open} (or a bare path): add it, then launch it. */
    public enum Action {
        ADD,
        OPEN
    }

    public InstanceRequest {
        Objects.requireNonNull(action, "action");
        Objects.requireNonNull(path, "path");
        if (!path.isAbsolute()) {
            throw new IllegalArgumentException("The path of a request must be absolute: " + path);
        }
        path = path.normalize();
    }
}
