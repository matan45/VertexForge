package com.vertexforge.launcher.service;

import com.vertexforge.launcher.engine.EngineContent;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Set;

/**
 * A project to create (VK-1679), from an engine's template (VK-1689).
 *
 * @param name       exactly as typed: never trimmed, so that a stray space is rejected rather than silently dropped
 * @param location   the folder the project folder goes into; it must be absolute
 * @param engine     the engine whose template and script library the project starts with, and which it is bound to
 * @param templateId the template's id ({@code empty})
 * @param options    the ids of the template options to add ({@code examples}, {@code vfxLibrary})
 */
public record CreateRequest(String name, Path location, EngineContent engine, String templateId,
                            Set<String> options) {
    public CreateRequest {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(location, "location");
        Objects.requireNonNull(engine, "engine");
        Objects.requireNonNull(templateId, "templateId");
        options = Set.copyOf(options);
    }
}
