package com.vertexforge.launcher.repository;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonSyntaxException;
import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.model.RecentProjectsData;
import com.vertexforge.launcher.model.RecentProjectsData.ProjectData;
import com.vertexforge.launcher.util.ProjectValidator;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.List;

/**
 * Repository for persisting recent projects to JSON file.
 * <p>
 * Handles loading and saving of the recent projects list to
 * {@code ~/.vertexforge/recent-projects.json}.
 * </p>
 */
public class RecentProjectsRepository {

    private static final String CONFIG_DIR = ".vertexforge";
    private static final String FILE_NAME = "recent-projects.json";

    private final Gson gson;
    private final Path storagePath;

    /**
     * Creates a new repository with the default storage location.
     */
    public RecentProjectsRepository() {
        this(getDefaultStoragePath());
    }

    /**
     * Creates a new repository with a custom storage path.
     *
     * @param storagePath path to the JSON file
     */
    public RecentProjectsRepository(Path storagePath) {
        this.storagePath = storagePath;
        this.gson = createGson();
    }

    /**
     * Loads the recent projects from the JSON file.
     * <p>
     * Returns an empty {@link RecentProjects} if:
     * <ul>
     *   <li>The file doesn't exist</li>
     *   <li>The file is corrupted or invalid JSON</li>
     *   <li>Any other IO error occurs</li>
     * </ul>
     * Invalid project paths are filtered out during loading.
     * </p>
     *
     * @return the loaded recent projects, or empty if unavailable
     */
    public RecentProjects load() {
        if (!Files.exists(storagePath)) {
            return new RecentProjects();
        }

        try {
            String json = Files.readString(storagePath);
            RecentProjectsData data = gson.fromJson(json, RecentProjectsData.class);

            if (data == null) {
                return new RecentProjects();
            }

            return fromData(data);

        } catch (IOException e) {
            System.err.println("Failed to read recent projects file: " + e.getMessage());
            return new RecentProjects();
        } catch (JsonSyntaxException e) {
            System.err.println("Invalid JSON in recent projects file: " + e.getMessage());
            return new RecentProjects();
        }
    }

    /**
     * Saves the recent projects to the JSON file.
     * <p>
     * Creates the parent directory if it doesn't exist.
     * </p>
     *
     * @param projects the recent projects to save
     * @return true if saved successfully, false otherwise
     */
    public boolean save(RecentProjects projects) {
        try {
            ensureDirectoryExists();

            RecentProjectsData data = toData(projects);
            String json = gson.toJson(data);
            Files.writeString(storagePath, json);

            return true;

        } catch (IOException e) {
            System.err.println("Failed to save recent projects: " + e.getMessage());
            return false;
        }
    }

    /**
     * Returns the storage path for the recent projects file.
     *
     * @return the path to the JSON file
     */
    public Path getStoragePath() {
        return storagePath;
    }

    /**
     * Checks if the storage file exists.
     *
     * @return true if the file exists
     */
    public boolean exists() {
        return Files.exists(storagePath);
    }

    /**
     * Deletes the storage file if it exists.
     *
     * @return true if deleted or didn't exist, false on error
     */
    public boolean delete() {
        try {
            return Files.deleteIfExists(storagePath);
        } catch (IOException e) {
            System.err.println("Failed to delete recent projects file: " + e.getMessage());
            return false;
        }
    }

    /**
     * Gets the default storage path (~/.vertexforge/recent-projects.json).
     *
     * @return the default storage path
     */
    private static Path getDefaultStoragePath() {
        String userHome = System.getProperty("user.home");
        return Paths.get(userHome, CONFIG_DIR, FILE_NAME);
    }

    /**
     * Creates and configures the Gson instance.
     *
     * @return configured Gson instance
     */
    private Gson createGson() {
        return new GsonBuilder()
                .setPrettyPrinting()
                .create();
    }

    /**
     * Ensures the parent directory exists.
     *
     * @throws IOException if directory creation fails
     */
    private void ensureDirectoryExists() throws IOException {
        Path parent = storagePath.getParent();
        if (parent != null && !Files.exists(parent)) {
            Files.createDirectories(parent);
        }
    }

    /**
     * Converts domain model to DTO for serialization.
     *
     * @param projects the domain model
     * @return the DTO for JSON serialization
     */
    private RecentProjectsData toData(RecentProjects projects) {
        List<ProjectData> projectDataList = projects.getAll().stream()
                .map(p -> new ProjectData(
                        p.name(),
                        p.path().toString(),
                        p.lastOpened().toString()
                ))
                .toList();

        return RecentProjectsData.create(projectDataList);
    }

    /**
     * Validates that a path is safe to use.
     * <p>
     * Security check to prevent path traversal attacks from malicious JSON files.
     * Ensures the path is absolute and normalized (no .. components).
     * </p>
     *
     * @param path the path to validate
     * @return true if the path is safe, false otherwise
     */
    private boolean isPathSafe(Path path) {
        // Must be absolute path
        if (!path.isAbsolute()) {
            return false;
        }

        // Normalize to resolve any ".." or "." components
        Path normalized = path.normalize();

        // After normalization, path should equal original (no traversal sequences)
        // This catches paths like "/home/user/../../../etc/passwd"
        if (!normalized.equals(path.toAbsolutePath().normalize())) {
            return false;
        }

        // Ensure normalized path doesn't contain suspicious components
        for (Path component : normalized) {
            String name = component.toString();
            if (name.equals("..") || name.equals(".")) {
                return false;
            }
        }

        return true;
    }

    /**
     * Converts DTO to domain model after deserialization.
     * <p>
     * Filters out invalid project paths and entries with parse errors.
     * </p>
     *
     * @param data the DTO from JSON
     * @return the domain model
     */
    private RecentProjects fromData(RecentProjectsData data) {
        RecentProjects projects = new RecentProjects();

        if (data.projects() == null) {
            return projects;
        }

        for (ProjectData pd : data.projects()) {
            try {
                if (pd.name() == null || pd.path() == null || pd.lastOpened() == null) {
                    continue;
                }

                Path path = Paths.get(pd.path());

                // Security: Validate path to prevent path traversal attacks
                if (!isPathSafe(path)) {
                    System.err.println("Skipping unsafe path: " + pd.path());
                    continue;
                }

                // Skip invalid project paths (file doesn't exist, wrong extension, etc.)
                if (!ProjectValidator.isValid(path)) {
                    continue;
                }

                Instant lastOpened = Instant.parse(pd.lastOpened());
                ProjectInfo projectInfo = new ProjectInfo(pd.name(), path, lastOpened);
                projects.add(projectInfo);

            } catch (DateTimeParseException e) {
                System.err.println("Invalid timestamp for project '" + pd.name() + "': " + pd.lastOpened());
            } catch (IllegalArgumentException e) {
                System.err.println("Invalid project data: " + e.getMessage());
            }
        }

        return projects;
    }
}
