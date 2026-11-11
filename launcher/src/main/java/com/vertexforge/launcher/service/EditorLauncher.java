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

    public CompletableFuture<LaunchResult> launchAsync(Path editorPath, Path projectPath) {
        if (!Files.isExecutable(editorPath)) {
            return CompletableFuture.completedFuture(
                    LaunchResult.fail("Editor not found: " + editorPath));
        }
        if (!Files.exists(projectPath)) {
            return CompletableFuture.completedFuture(
                    LaunchResult.fail("Project file not found: " + projectPath));
        }

        return CompletableFuture.supplyAsync(() -> {
            try {
                ProcessBuilder pb = new ProcessBuilder(
                        editorPath.toString(),
                        projectPath.toString()
                );
                pb.directory(editorPath.getParent().toFile());
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
