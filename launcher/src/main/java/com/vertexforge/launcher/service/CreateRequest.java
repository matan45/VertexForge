package com.vertexforge.launcher.service;

import java.nio.file.Path;
import java.util.Objects;

/**
 * A project to create (VK-1679).
 *
 * @param name     exactly as typed: never trimmed, so that a stray space is rejected rather than silently dropped
 * @param location the folder the project folder goes into; it must be absolute
 */
public record CreateRequest(String name, Path location) {
    public CreateRequest {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(location, "location");
    }
}
