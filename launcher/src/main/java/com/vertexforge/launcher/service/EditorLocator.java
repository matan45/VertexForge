package com.vertexforge.launcher.service;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;
import java.util.function.UnaryOperator;

/** Finds the editor executable: {@code VERTEXFORGE_EDITOR_PATH} first, then fixed paths below the working folder. */
public class EditorLocator {

    public static final String EDITOR_ENV_VAR = "VERTEXFORGE_EDITOR_PATH";
    static final String EDITOR_EXE_NAME = "Editor.exe";

    private final UnaryOperator<String> env;
    private final Path workingDir;

    public EditorLocator(UnaryOperator<String> env, Path workingDir) {
        this.env = env;
        this.workingDir = workingDir;
    }

    public Optional<Path> findEditorExecutable() {
        // 1. Check environment variable
        String envPath = env.apply(EDITOR_ENV_VAR);
        if (envPath != null && !envPath.isBlank()) {
            Path path = Path.of(envPath);
            if (Files.isExecutable(path)) {
                return Optional.of(path);
            }
        }

        // 2. Check relative paths from launcher working directory
        Path[] relativePaths = {
                workingDir.resolve("../bin/Editor/Release/x64/" + EDITOR_EXE_NAME),
                workingDir.resolve("bin/Editor/Release/x64/" + EDITOR_EXE_NAME)
        };

        for (Path p : relativePaths) {
            Path normalized = p.normalize();
            if (Files.isExecutable(normalized)) {
                return Optional.of(normalized);
            }
        }

        return Optional.empty();
    }
}
