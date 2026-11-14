package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Optional;

/**
 * The engines file as just read, plus where the running launcher's bundled engine is (VK-1681). Its engines have
 * not been checked yet; {@link EngineCatalog#check} does that.
 *
 * @param registry           the registered engines
 * @param bundledExecutable  the real path of the bundled editor, when the running launcher has one on disk
 */
public record EngineListing(EngineRegistryView registry, Optional<Path> bundledExecutable) {
    public EngineListing {
        Objects.requireNonNull(registry, "registry");
        bundledExecutable = bundledExecutable == null ? Optional.empty() : bundledExecutable;
    }
}
