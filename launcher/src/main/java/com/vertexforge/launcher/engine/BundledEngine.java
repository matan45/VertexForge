package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.Optional;

/**
 * Where the engine that ships with a packaged launcher lives (VK-1681). {@code tools/package_release.lua} lays the
 * release folder out as
 * <pre>
 * VertexForge/
 *   VertexForge Launcher/VertexForge Launcher.exe   the jpackage app image (its exe is jpackage.app-path)
 *   bin/Editor/Release/x64/Editor.exe               + engine.json
 * </pre>
 * so the bundled editor is found from the launcher's own exe, whatever folder the launcher was started from.
 */
public final class BundledEngine {

    /** The editor below the release folder's root. */
    static final Path EDITOR_IN_PACKAGE = Path.of("bin", "Editor", "Release", "x64", "Editor.exe");

    private BundledEngine() {
        // Utility class - prevent instantiation
    }

    /**
     * Where the bundled editor would be for a launcher started from {@code launcherExecutable}; empty when the path
     * is too short to have a release folder above the app image. Never touches the file system.
     */
    public static Optional<Path> expectedExecutable(Path launcherExecutable) {
        Path imageRoot = launcherExecutable.toAbsolutePath().normalize().getParent();
        Path packageRoot = imageRoot == null ? null : imageRoot.getParent();
        if (packageRoot == null) {
            return Optional.empty();
        }
        return Optional.of(packageRoot.resolve(EDITOR_IN_PACKAGE));
    }
}
