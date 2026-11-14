package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.ProcessStarter;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

public class EditorLauncher {

    /** How long after the start an exit still counts as a failed launch. */
    public static final Duration DEFAULT_CRASH_WINDOW = Duration.ofMillis(500);

    public record LaunchResult(boolean success, String errorMessage) {
        public static LaunchResult ok() {
            return new LaunchResult(true, null);
        }

        public static LaunchResult fail(String message) {
            return new LaunchResult(false, message);
        }
    }

    private final ProcessStarter processStarter;
    private final Duration crashWindow;

    public EditorLauncher(ProcessStarter processStarter) {
        this(processStarter, DEFAULT_CRASH_WINDOW);
    }

    public EditorLauncher(ProcessStarter processStarter, Duration crashWindow) {
        this.processStarter = Objects.requireNonNull(processStarter, "processStarter");
        this.crashWindow = Objects.requireNonNull(crashWindow, "crashWindow");
    }

    /**
     * Starts the editor on a background thread. The file checks run there too: on a network share that has just
     * gone away they can block for many seconds, which must never happen on the FX thread (VK-1678).
     *
     * @param workingDirectory the folder the editor starts in: the one its {@code engine.json} names, or its own
     *                         folder for an install without one (VK-1681). A dev build loads its shaders relative to
     *                         it.
     */
    public CompletableFuture<LaunchResult> launchAsync(Path editorPath, Path workingDirectory, Path projectPath) {
        return CompletableFuture.supplyAsync(() -> {
            if (!Files.isRegularFile(editorPath) || !Files.isExecutable(editorPath)) {
                return LaunchResult.fail("Editor not found: " + editorPath);
            }
            if (!Files.isDirectory(workingDirectory)) {
                return LaunchResult.fail("Working directory not found: " + workingDirectory);
            }
            if (!Files.exists(projectPath)) {
                return LaunchResult.fail("Project file not found: " + projectPath);
            }
            try {
                ProcessBuilder pb = new ProcessBuilder(
                        editorPath.toString(),
                        projectPath.toString()
                );
                pb.directory(workingDirectory.toFile());
                pb.inheritIO();

                Process process = processStarter.start(pb);

                // Wait briefly to detect immediate crashes
                Thread.sleep(crashWindow.toMillis());
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
        });
    }
}
