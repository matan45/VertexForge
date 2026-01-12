package com.vertexforge.launcher.util;

import java.io.BufferedReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Utility class for validating VertexForge project files.
 * <p>
 * Provides methods to check project file existence, readability,
 * and extract metadata from .vfproj files.
 * </p>
 */
public final class ProjectValidator {

    /**
     * Expected file extension for VertexForge project files.
     */
    public static final String PROJECT_EXTENSION = ".vfproj";

    /**
     * Pattern to extract projectName from JSON (simple regex, no full JSON parsing).
     */
    private static final Pattern PROJECT_NAME_PATTERN =
            Pattern.compile("\"projectName\"\\s*:\\s*\"([^\"]+)\"");

    private ProjectValidator() {
        // Utility class - prevent instantiation
    }

    /**
     * Validation result containing status and optional error message.
     *
     * @param valid   true if validation passed
     * @param message description of the validation result or error
     */
    public record ValidationResult(boolean valid, String message) {
        /**
         * Creates a successful validation result.
         *
         * @return a successful ValidationResult
         */
        public static ValidationResult success() {
            return new ValidationResult(true, "Valid");
        }

        /**
         * Creates a failed validation result with an error message.
         *
         * @param message the error description
         * @return a failed ValidationResult
         */
        public static ValidationResult failure(String message) {
            return new ValidationResult(false, message);
        }
    }

    /**
     * Performs basic validation: checks if the file exists and is readable.
     *
     * @param projectPath path to the .vfproj file
     * @return validation result
     */
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

    /**
     * Checks if a project file exists and is readable.
     *
     * @param projectPath path to the .vfproj file
     * @return true if the file exists and is readable
     */
    public static boolean isValid(Path projectPath) {
        return validateBasic(projectPath).valid();
    }

    /**
     * Extracts the project name from a .vfproj file.
     * <p>
     * Uses simple regex matching to avoid requiring a JSON library dependency.
     * Falls back to the parent directory name if extraction fails.
     * </p>
     *
     * @param projectPath path to the .vfproj file
     * @return the project name, or the directory name as fallback
     */
    public static String extractProjectName(Path projectPath) {
        return tryExtractProjectName(projectPath)
                .orElseGet(() -> getFallbackName(projectPath));
    }

    /**
     * Attempts to extract the project name from the file.
     *
     * @param projectPath path to the .vfproj file
     * @return Optional containing the project name if found
     */
    public static Optional<String> tryExtractProjectName(Path projectPath) {
        if (!isValid(projectPath)) {
            return Optional.empty();
        }

        try (BufferedReader reader = Files.newBufferedReader(projectPath)) {
            StringBuilder content = new StringBuilder();
            String line;
            // Read enough to find the projectName field (typically in first few lines)
            int lineCount = 0;
            while ((line = reader.readLine()) != null && lineCount < 20) {
                content.append(line);
                lineCount++;

                // Early exit if we find what we need
                if (line.contains("projectName")) {
                    break;
                }
            }

            Matcher matcher = PROJECT_NAME_PATTERN.matcher(content);
            if (matcher.find()) {
                String name = matcher.group(1);
                if (!name.isBlank()) {
                    return Optional.of(name);
                }
            }
        } catch (IOException e) {
            // Fall through to return empty
        }

        return Optional.empty();
    }

    /**
     * Gets a fallback name based on the project directory.
     *
     * @param projectPath path to the .vfproj file
     * @return the parent directory name, or "Unknown Project"
     */
    public static String getFallbackName(Path projectPath) {
        if (projectPath == null) {
            return "Unknown Project";
        }

        Path parent = projectPath.getParent();
        if (parent != null && parent.getFileName() != null) {
            return parent.getFileName().toString();
        }

        // Last resort: use file name without extension
        String fileName = projectPath.getFileName().toString();
        if (fileName.endsWith(PROJECT_EXTENSION)) {
            return fileName.substring(0, fileName.length() - PROJECT_EXTENSION.length());
        }

        return fileName;
    }

    /**
     * Validates the project file and extracts metadata in one operation.
     * <p>
     * This is useful when both validation and name extraction are needed,
     * avoiding redundant file reads.
     * </p>
     *
     * @param projectPath path to the .vfproj file
     * @return Optional containing the project name if valid, empty if invalid
     */
    public static Optional<String> validateAndExtractName(Path projectPath) {
        ValidationResult result = validateBasic(projectPath);
        if (!result.valid()) {
            return Optional.empty();
        }

        return Optional.of(extractProjectName(projectPath));
    }
}
