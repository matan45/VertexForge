package com.vertexforge.launcher.template;

import com.vertexforge.launcher.engine.EngineContent;

import java.nio.file.Path;
import java.util.Optional;
import java.util.function.Function;

/**
 * The options a template may offer (VK-1689), each copying one engine content folder that {@code engine.json} names
 * into the project. A template lists them by id; an id this launcher does not know is ignored.
 */
public enum TemplateOptionKind {
    /** {@code engine.json} {@code examplesDir}: the example scripts the MCP primer quotes. */
    EXAMPLES("examples", "examplesDir", EngineContent::examplesDir),
    /** {@code engine.json} {@code vfxLibraryDir}: the ready-made effects. */
    VFX_LIBRARY("vfxLibrary", "vfxLibraryDir", EngineContent::vfxLibraryDir);

    private final String id;
    private final String manifestField;
    private final Function<EngineContent, Optional<Path>> source;

    TemplateOptionKind(String id, String manifestField, Function<EngineContent, Optional<Path>> source) {
        this.id = id;
        this.manifestField = manifestField;
        this.source = source;
    }

    /** The id a {@code template.json} option uses. */
    public String id() {
        return id;
    }

    /** The {@code engine.json} member that names the folder. */
    public String manifestField() {
        return manifestField;
    }

    /** The engine folder to copy, when the engine's manifest names one. */
    public Optional<Path> sourceIn(EngineContent engine) {
        return source.apply(engine);
    }

    public static Optional<TemplateOptionKind> forId(String id) {
        for (TemplateOptionKind kind : values()) {
            if (kind.id.equals(id)) {
                return Optional.of(kind);
            }
        }
        return Optional.empty();
    }
}
