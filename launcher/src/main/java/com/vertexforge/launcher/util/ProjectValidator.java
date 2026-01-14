package com.vertexforge.launcher.util;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.regex.Pattern;


public final class ProjectValidator {

    public static final String PROJECT_EXTENSION = ".vfproj";


    private ProjectValidator() {
        // Utility class - prevent instantiation
    }

    public record ValidationResult(boolean valid, String message) {
        public static ValidationResult success() {
            return new ValidationResult(true, "Valid");
        }

        public static ValidationResult failure(String message) {
            return new ValidationResult(false, message);
        }
    }


    public static ValidationResult validateBasic(Path projectPath) {
        if (projectPath == null) {
            return ValidationResult.failure("Project path is null");
        }

        if (!Files.exists(projectPath)) {
            return ValidationResult.failure("Project file does not exist: " + projectPath);
        }

        if (!Files.isRegularFile(projectPath)) {
            return ValidationResult.failure("Path is not a file: " + projectPath);
        }

        if (!Files.isReadable(projectPath)) {
            return ValidationResult.failure("Project file is not readable: " + projectPath);
        }

        String fileName = projectPath.getFileName().toString();
        if (!fileName.endsWith(PROJECT_EXTENSION)) {
            return ValidationResult.failure("File does not have " + PROJECT_EXTENSION + " extension");
        }

        return ValidationResult.success();
    }


    public static boolean isValid(Path projectPath) {
        return validateBasic(projectPath).valid();
    }


}
