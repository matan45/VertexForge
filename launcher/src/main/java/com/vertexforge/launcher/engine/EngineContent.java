package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.regex.Pattern;

/**
 * What an engine install offers for creating a project (VK-1689): its version and the content folders its
 * {@code engine.json} names. Pure: the folders are only resolved here, never looked at; the template scan reads them
 * in the background.
 *
 * @param engineId      the registered install a new project is bound to; empty for the override and for a bundled
 *                      engine that could not be saved, which no project can be bound to
 * @param engineVersion the engine's {@code MAJOR.MINOR.PATCH}
 * @param templatesDir  the project templates folder
 * @param scriptLibDir  the mType script library folder
 * @param examplesDir   the example scripts folder, when the manifest names one
 * @param vfxLibraryDir the VFX library folder, when the manifest names one
 * @param label         the engine as the launcher shows it ({@link EngineOption#label()})
 * @param path          the editor's path, for display
 * @param override      whether it comes from {@code VERTEXFORGE_EDITOR_PATH}
 * @param assetFormatVersion the binary asset format the engine writes ({@code MAJOR.MINOR.PATCH}), which a new
 *                      {@code .vfproj} records (VK-1690); {@code null} when the manifest does not say, or says
 *                      something that is not such a version
 */
public record EngineContent(
        Optional<UUID> engineId,
        String engineVersion,
        Path templatesDir,
        Path scriptLibDir,
        Optional<Path> examplesDir,
        Optional<Path> vfxLibraryDir,
        String label,
        String path,
        boolean override,
        String assetFormatVersion
) {
    /** The product name the editor writes before its version ({@code Version::displayName}, Config.hpp). */
    public static final String PRODUCT_NAME = "VertexForge";

    /** {@code MAJOR.MINOR.PATCH} without leading zeros, as {@code tools/engine_version.lua} writes it. */
    private static final Pattern VERSION_PATTERN = Pattern.compile("(0|[1-9]\\d*)(\\.(0|[1-9]\\d*)){2}");

    /**
     * @throws IllegalArgumentException when {@code assetFormatVersion} is given but is not a {@code MAJOR.MINOR.PATCH}
     *                                  version ({@link #of} leaves such a manifest value out instead)
     */
    public EngineContent {
        engineId = engineId == null ? Optional.empty() : engineId;
        Objects.requireNonNull(engineVersion, "engineVersion");
        Objects.requireNonNull(templatesDir, "templatesDir");
        Objects.requireNonNull(scriptLibDir, "scriptLibDir");
        examplesDir = examplesDir == null ? Optional.empty() : examplesDir;
        vfxLibraryDir = vfxLibraryDir == null ? Optional.empty() : vfxLibraryDir;
        label = label == null ? engineVersion : label;
        path = path == null ? "" : path;
        if (assetFormatVersion != null && !isVersion(assetFormatVersion)) {
            throw new IllegalArgumentException("assetFormatVersion \"" + assetFormatVersion
                    + "\" is not a MAJOR.MINOR.PATCH version");
        }
    }

    /** Content whose asset format is not known: a new project then records none. */
    public EngineContent(Optional<UUID> engineId, String engineVersion, Path templatesDir, Path scriptLibDir,
                         Optional<Path> examplesDir, Optional<Path> vfxLibraryDir, String label, String path,
                         boolean override) {
        this(engineId, engineVersion, templatesDir, scriptLibDir, examplesDir, vfxLibraryDir, label, path, override,
                null);
    }

    /**
     * {@code VertexForge 1.0.0}: the {@code engineVersion} a new {@code .vfproj} gets, exactly what this engine's editor
     * writes on every save ({@code Version::displayName}).
     */
    public String displayVersion() {
        return PRODUCT_NAME + " " + engineVersion;
    }

    /**
     * The content of an engine that works and whose {@code engine.json} names a templates folder and a script library;
     * empty for any other engine ({@link #unavailableReason} says why).
     */
    public static Optional<EngineContent> of(EngineOption option) {
        Objects.requireNonNull(option, "option");
        if (!option.isOk() || option.executable() == null || option.health().manifest() == null) {
            return Optional.empty();
        }
        EngineManifest manifest = option.health().manifest();
        Path dir = option.executable().getParent();
        if (dir == null) {
            return Optional.empty();
        }
        Optional<Path> templates = manifest.templatesDirIn(dir);
        Optional<Path> library = manifest.scriptLibDirIn(dir);
        if (templates.isEmpty() || library.isEmpty()) {
            return Optional.empty();
        }
        // The manifest reads assetFormatVersion leniently (any string); only a real version is carried on.
        String assetFormatVersion = manifest.assetFormatVersion();
        return Optional.of(new EngineContent(option.id(), manifest.engineVersion(), templates.get(), library.get(),
                manifest.examplesDirIn(dir), manifest.vfxLibraryDirIn(dir), option.label(), option.pathText(),
                option.override(), assetFormatVersion != null && isVersion(assetFormatVersion)
                        ? assetFormatVersion : null));
    }

    private static boolean isVersion(String text) {
        return VERSION_PATTERN.matcher(text).matches();
    }

    /** Why {@link #of} gives nothing for {@code option}; empty when it gives content. */
    public static Optional<String> unavailableReason(EngineOption option) {
        Objects.requireNonNull(option, "option");
        if (!option.isOk()) {
            return Optional.of("it is " + option.health().label().toLowerCase(java.util.Locale.ROOT) + ": "
                    + option.health().reason());
        }
        if (option.health().manifest() == null) {
            return Optional.of("it has no " + EngineManifest.FILE_NAME + " beside its editor");
        }
        if (of(option).isEmpty()) {
            return Optional.of("its " + EngineManifest.FILE_NAME + " names no templates folder or script library"
                    + " (rebuild the Editor to update it)");
        }
        return Optional.empty();
    }
}
