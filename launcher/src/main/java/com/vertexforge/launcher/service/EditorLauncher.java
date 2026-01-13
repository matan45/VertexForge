package com.vertexforge.launcher.service;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;

/**
 * Service for launching the VertexForge editor with a project.
 * Handles editor executable discovery and process execution.
 * <p>
 * Implements VK-156 (Engine Editor CLI Integration).
 * </p>
 */
public class EditorLauncher {
    private static final String EDITOR_ENV_VAR = "VERTEXFORGE_EDITOR_PATH";
    private static final String EDITOR_EXE_NAME = "Editor.exe";

    /**
     * Result of an editor launch attempt.
     *
     * @param success      true if the editor was launched successfully
     * @param errorMessage error message if launch failed, null on success
     */
    public record LaunchResult(boolean success, String errorMessage) {
        public static LaunchResult ok() {
            return new LaunchResult(true, null);
        }

        public static LaunchResult fail(String message) {
            return new LaunchResult(false, message);
        }
    }

    /**
     * Attempts to find the editor executable.
     * <p>
     * Search order:
     * 1. VERTEXFORGE_EDITOR_PATH environment variable
     * 2. Relative paths from launcher working directory (bin/Editor/Debug|Release/x64/)
     * </p>
     *
     * @return Optional containing the editor path if found, empty otherwise
     */
    public Optional<Path> findEditorExecutable() {
        // 1. Check environment variable
        String envPath = System.getenv(EDITOR_ENV_VAR);
        if (envPath != null && !envPath.isBlank()) {
            Path path = Path.of(envPath);
            if (Files.isExecutable(path)) {
                return Optional.of(path);
            }
        }

        // 2. Check relative paths from launcher working directory
        Path launcherDir = Path.of(System.getProperty("user.dir"));
        Path[] relativePaths = {
                launcherDir.resolve("../bin/Editor/Debug/x64/" + EDITOR_EXE_NAME),
                launcherDir.resolve("../bin/Editor/Release/x64/" + EDITOR_EXE_NAME),
                launcherDir.resolve("bin/Editor/Debug/x64/" + EDITOR_EXE_NAME),
                launcherDir.resolve("bin/Editor/Release/x64/" + EDITOR_EXE_NAME)
        };

        for (Path p : relativePaths) {
            Path normalized = p.normalize();
            if (Files.isExecutable(normalized)) {
                return Optional.of(normalized);
            }
        }

        return Optional.empty();
    }

    /**
     * Launches the editor with the specified project.
     *
     * @param editorPath  path to the editor executable
     * @param projectPath path to the .vfproj file
     * @return LaunchResult indicating success or failure with error message
     */
    public LaunchResult launch(Path editorPath, Path projectPath) {
        if (!Files.isExecutable(editorPath)) {
            return LaunchResult.fail("Editor not found: " + editorPath);
        }
        if (!Files.exists(projectPath)) {
            return LaunchResult.fail("Project file not found: " + projectPath);
        }

        try {
            ProcessBuilder pb = new ProcessBuilder(
                    editorPath.toString(),
                    projectPath.toString()
            );
            pb.directory(editorPath.getParent().toFile());
            pb.inheritIO();

            Process process = pb.start();

            // Wait briefly to detect immediate crashes
            Thread.sleep(500);
            if (!process.isAlive()) {
                int exitCode = process.exitValue();
                if (exitCode != 0) {
                    return LaunchResult.fail("Editor exited with code: " + exitCode);
                }
            }

            return LaunchResult.ok();
        } catch (IOException e) {
            return LaunchResult.fail("Failed to start editor: " + e.getMessage());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return LaunchResult.fail("Launch interrupted");
        }
    }
}
