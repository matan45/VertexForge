package com.vertexforge.launcher.service;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.vertexforge.launcher.model.ProjectCreationResult;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Service for creating new VertexForge projects.
 * <p>
 * Handles all file system operations: creating directories,
 * generating configuration files, and extracting resources.
 * </p>
 */
public class ProjectCreator {

    private static final String LIZ_ZIP_RESOURCE = "/templates/liz.zip";
    private static final String SCENE_TEMPLATE_RESOURCE = "/templates/Main.vfScene";
    private static final String ENGINE_VERSION = "VertexForge 1.0.0";

    private final Gson gson;

    /**
     * Creates a new ProjectCreator instance.
     */
    public ProjectCreator() {
        this.gson = new GsonBuilder()
                .setPrettyPrinting()
                .create();
    }

    /**
     * Creates a new project with the specified name at the given location.
     * <p>
     * Creates the following structure:
     * <pre>
     * {projectName}/
     * ├── {projectName}.vfproj
     * ├── scenes/
     * │   └── Main.vfScene
     * ├── assets/
     * └── scripts/
     *     ├── scripts.mtproj
     *     ├── game/
     *     ├── compiled/
     *     └── lib/
     * </pre>
     * </p>
     *
     * @param projectName     the name of the project
     * @param parentDirectory the directory where the project folder will be created
     * @return the result of the creation operation
     */
    public ProjectCreationResult createProject(String projectName, Path parentDirectory) {
        Path projectDir = parentDirectory.resolve(projectName);
        Path projectFile = projectDir.resolve(projectName + ".vfproj");

        try {
            // Create directory structure
            createDirectoryStructure(projectDir);

            // Generate configuration files
            createProjectFile(projectName, projectDir, projectFile);
            createStartupScene(projectDir.resolve("scenes"));
            createScriptProject(projectName, projectDir.resolve("scripts"));

            // Extract mType library
            extractLibrary(projectDir.resolve("scripts").resolve("lib"));

            return ProjectCreationResult.success(projectFile);

        } catch (IOException e) {
            // Attempt cleanup on failure
            cleanupOnFailure(projectDir);
            String errorMessage = e.getMessage() != null
                    ? e.getMessage()
                    : "Unknown error: " + e.getClass().getSimpleName();
            return ProjectCreationResult.failure(errorMessage);
        }
    }

    /**
     * Creates the project directory structure.
     *
     * @param projectDir the root project directory
     * @throws IOException if directory creation fails
     */
    private void createDirectoryStructure(Path projectDir) throws IOException {
        Files.createDirectories(projectDir);
        Files.createDirectories(projectDir.resolve("scenes"));
        Files.createDirectories(projectDir.resolve("assets"));
        Files.createDirectories(projectDir.resolve("scripts"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("game"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("compiled"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("lib"));
    }

    /**
     * Generates the .vfproj project file.
     *
     * @param projectName the project name
     * @param projectDir  the project directory
     * @param projectFile the path to the .vfproj file
     * @throws IOException if file writing fails
     */
    private void createProjectFile(String projectName, Path projectDir, Path projectFile) throws IOException {
        // Use LinkedHashMap to preserve field order in JSON
        Map<String, Object> projectConfig = new LinkedHashMap<>();
        projectConfig.put("schemaVersion", "1.0");
        projectConfig.put("projectName", projectName);
        projectConfig.put("version", "0.1.0");
        projectConfig.put("workingDirectory", ".");
        projectConfig.put("startupScene", "scenes/Main.vfScene");
        projectConfig.put("exeIconPath", "");
        projectConfig.put("engineVersion", ENGINE_VERSION);
        projectConfig.put("lastModified", formatTimestamp(Instant.now()));

        String json = gson.toJson(projectConfig);
        Files.writeString(projectFile, json);
    }

    /**
     * Creates an empty startup scene file by copying the template.
     *
     * @param scenesDir the scenes directory
     * @throws IOException if file creation fails
     */
    private void createStartupScene(Path scenesDir) throws IOException {
        Path sceneFile = scenesDir.resolve("Main.vfScene");

        try (InputStream templateStream = getClass().getResourceAsStream(SCENE_TEMPLATE_RESOURCE)) {
            if (templateStream == null) {
                throw new IOException("Scene template not found: " + SCENE_TEMPLATE_RESOURCE);
            }
            Files.copy(templateStream, sceneFile);
        }
    }

    /**
     * Creates the scripts.mtproj configuration file.
     *
     * @param projectName the project name
     * @param scriptsDir  the scripts directory
     * @throws IOException if file writing fails
     */
    private void createScriptProject(String projectName, Path scriptsDir) throws IOException {
        Path mtprojFile = scriptsDir.resolve("scripts.mtproj");

        String xml = """
                <?xml version="1.0" encoding="UTF-8"?>
                <Project Name="%s" Version="1.0.0">
                  <Source>
                    <Include>game/**/*.mt</Include>
                  </Source>
                  <Output Directory="compiled" />
                  <Imports>
                    <SearchPath>lib</SearchPath>
                  </Imports>
                </Project>
                """.formatted(projectName);

        Files.writeString(mtprojFile, xml);
    }

    /**
     * Extracts the liz.zip library from JAR resources to the lib directory.
     *
     * @param libDir the target lib directory
     * @throws IOException if extraction fails
     */
    private void extractLibrary(Path libDir) throws IOException {
        try (InputStream resourceStream = getClass().getResourceAsStream(LIZ_ZIP_RESOURCE)) {
            if (resourceStream == null) {
                throw new IOException("Library resource not found: " + LIZ_ZIP_RESOURCE);
            }

            try (ZipInputStream zipIn = new ZipInputStream(resourceStream)) {
                ZipEntry entry;
                while ((entry = zipIn.getNextEntry()) != null) {
                    try {
                        Path targetPath = libDir.resolve(entry.getName());

                        // Security: Prevent zip slip attack
                        if (!targetPath.normalize().startsWith(libDir.normalize())) {
                            throw new IOException("Invalid zip entry: " + entry.getName());
                        }

                        if (entry.isDirectory()) {
                            Files.createDirectories(targetPath);
                        } else {
                            // Ensure parent directory exists
                            Files.createDirectories(targetPath.getParent());
                            Files.copy(zipIn, targetPath);
                        }
                    } finally {
                        zipIn.closeEntry();
                    }
                }
            }
        }
    }

    /**
     * Attempts to clean up a partially created project on failure.
     *
     * @param projectDir the project directory to delete
     */
    private void cleanupOnFailure(Path projectDir) {
        try {
            if (Files.exists(projectDir)) {
                Files.walk(projectDir)
                        .sorted(Comparator.reverseOrder())
                        .forEach(path -> {
                            try {
                                Files.delete(path);
                            } catch (IOException ignored) {
                                // Best effort cleanup
                            }
                        });
            }
        } catch (IOException e) {
            System.err.println("Warning: Failed to cleanup partial project: " + e.getMessage());
        }
    }

    /**
     * Formats an Instant as an ISO-8601 timestamp string.
     *
     * @param instant the instant to format
     * @return the formatted timestamp
     */
    private String formatTimestamp(Instant instant) {
        return DateTimeFormatter.ISO_INSTANT.format(instant.atOffset(ZoneOffset.UTC));
    }
}
