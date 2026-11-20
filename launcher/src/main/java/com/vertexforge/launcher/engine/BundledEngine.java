package com.vertexforge.launcher.engine;

import java.nio.file.Path;
import java.util.Optional;

/**
 * Where the engine that ships with a packaged launcher lives (VK-1681). {@code tools/package_release.lua} makes the
 * release folder the launcher's own jpackage app image (VK-1693), because {@code jpackage --type msi} installs an app
 * image as the install folder and needs its launcher at the root:
 * <pre>
 * VertexForge/                                    dist/VertexForge, or %LOCALAPPDATA%\Programs\VertexForge
 *   VertexForge Launcher.exe                      jpackage.app-path (app/ and runtime/ beside it)
 *   bin/Editor/Release/x64/Editor.exe             + engine.json
 * </pre>
 * so the bundled editor is found from the launcher's own exe, whatever folder the launcher was started from. The
 * earlier layout, with the app image in a {@code VertexForge Launcher/} subfolder, is not looked for.
 */
public final class BundledEngine {

    /** The editor below the release folder's root. */
    static final Path EDITOR_IN_PACKAGE = Path.of("bin", "Editor", "Release", "x64", "Editor.exe");

    private BundledEngine() {
        // Utility class - prevent instantiation
    }

    /**
     * Where the bundled editor would be for a launcher started from {@code launcherExecutable}: below the folder that
     * holds the exe. Empty when the path is a root and names no exe. Never touches the file system.
     */
    public static Optional<Path> expectedExecutable(Path launcherExecutable) {
        Path packageRoot = launcherExecutable.toAbsolutePath().normalize().getParent();
        if (packageRoot == null) {
            return Optional.empty();
        }
        return Optional.of(packageRoot.resolve(EDITOR_IN_PACKAGE));
    }
}
