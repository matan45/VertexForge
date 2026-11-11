package com.vertexforge.launcher.service;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;

class EditorLocatorTest {

    @TempDir
    Path tempDir;

    private static Path fakeEditor(Path file) throws IOException {
        Files.createDirectories(file.getParent());
        Files.writeString(file, "not really an exe");
        file.toFile().setExecutable(true);
        return file;
    }

    private Path workingDir() throws IOException {
        return Files.createDirectories(tempDir.resolve("repo").resolve("launcher"));
    }

    private static EditorLocator locator(Map<String, String> env, Path workingDir) {
        return new EditorLocator(env::get, workingDir);
    }

    @Test
    void theEnvironmentVariableWins() throws IOException {
        Path working = workingDir();
        fakeEditor(working.resolve("../bin/Editor/Release/x64/Editor.exe").normalize());
        Path custom = fakeEditor(tempDir.resolve("custom").resolve("Editor.exe"));

        Optional<Path> found = locator(Map.of(EditorLocator.EDITOR_ENV_VAR, custom.toString()), working)
                .findEditorExecutable();

        assertEquals(Optional.of(custom), found);
    }

    @Test
    void aMissingEnvironmentPathFallsThroughToTheBuildFolders() throws IOException {
        Path working = workingDir();
        Path parentBuild = fakeEditor(working.resolve("../bin/Editor/Release/x64/Editor.exe").normalize());

        Optional<Path> found = locator(
                Map.of(EditorLocator.EDITOR_ENV_VAR, tempDir.resolve("nowhere/Editor.exe").toString()), working)
                .findEditorExecutable();

        assertEquals(Optional.of(parentBuild), found);
    }

    @Test
    void theParentFolderBuildIsPreferredOverTheWorkingFolderBuild() throws IOException {
        Path working = workingDir();
        Path parentBuild = fakeEditor(working.resolve("../bin/Editor/Release/x64/Editor.exe").normalize());
        fakeEditor(working.resolve("bin/Editor/Release/x64/Editor.exe"));

        assertEquals(Optional.of(parentBuild), locator(Map.of(), working).findEditorExecutable());
    }

    @Test
    void theWorkingFolderBuildIsFoundWhenItIsTheOnlyOne() throws IOException {
        Path working = workingDir();
        Path localBuild = fakeEditor(working.resolve("bin/Editor/Release/x64/Editor.exe"));

        assertEquals(Optional.of(localBuild), locator(Map.of(), working).findEditorExecutable());
    }

    @Test
    void nothingFoundIsEmpty() throws IOException {
        assertEquals(Optional.empty(), locator(Map.of(), workingDir()).findEditorExecutable());
    }
}
