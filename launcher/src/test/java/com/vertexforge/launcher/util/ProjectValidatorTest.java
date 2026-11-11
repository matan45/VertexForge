package com.vertexforge.launcher.util;

import com.vertexforge.launcher.util.ProjectValidator.ValidationResult;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectValidatorTest {

    @TempDir
    Path tempDir;

    @Test
    void nullPathIsInvalid() {
        ValidationResult result = ProjectValidator.validateBasic(null);

        assertFalse(result.valid());
        assertEquals("Project path is null", result.message());
        assertFalse(ProjectValidator.isValid(null));
    }

    @Test
    void missingFileIsInvalid() {
        Path missing = tempDir.resolve("Missing.vfproj");

        ValidationResult result = ProjectValidator.validateBasic(missing);

        assertFalse(result.valid());
        assertEquals("Project file does not exist: " + missing, result.message());
    }

    @Test
    void directoryWithProjectExtensionIsInvalid() throws IOException {
        Path directory = Files.createDirectory(tempDir.resolve("Folder.vfproj"));

        ValidationResult result = ProjectValidator.validateBasic(directory);

        assertFalse(result.valid());
        assertEquals("Path is not a file: " + directory, result.message());
    }

    @Test
    void wrongExtensionIsInvalid() throws IOException {
        Path file = Files.writeString(tempDir.resolve("Game.txt"), "{}");

        ValidationResult result = ProjectValidator.validateBasic(file);

        assertFalse(result.valid());
        assertEquals("File does not have .vfproj extension", result.message());
    }

    @Test
    void readableProjectFileIsValid() throws IOException {
        Path file = Files.writeString(tempDir.resolve("Game.vfproj"), "{}");

        ValidationResult result = ProjectValidator.validateBasic(file);

        assertTrue(result.valid());
        assertEquals("Valid", result.message());
        assertTrue(ProjectValidator.isValid(file));
    }
}
