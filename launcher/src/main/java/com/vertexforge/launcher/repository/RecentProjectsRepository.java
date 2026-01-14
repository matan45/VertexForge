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
import java.util.List;

public class RecentProjectsRepository {

    private static final String CONFIG_DIR = ".vertexforge";
    private static final String FILE_NAME = "recent-projects.json";

    private final Gson gson;
    private final Path storagePath;

    public RecentProjectsRepository() {
        this(getDefaultStoragePath());
    }

    public RecentProjectsRepository(Path storagePath) {
        this.storagePath = storagePath;
        this.gson = createGson();
    }

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

    public void save(RecentProjects projects) {
        try {
            ensureDirectoryExists();

            RecentProjectsData data = toData(projects);
            String json = gson.toJson(data);
            Files.writeString(storagePath, json);

        } catch (IOException e) {
            System.err.println("Failed to save recent projects: " + e.getMessage());
        }
    }

    private static Path getDefaultStoragePath() {
        String userHome = System.getProperty("user.home");
        return Paths.get(userHome, CONFIG_DIR, FILE_NAME);
    }

    private Gson createGson() {
        return new GsonBuilder()
                .setPrettyPrinting()
                .create();
    }

    private void ensureDirectoryExists() throws IOException {
        Path parent = storagePath.getParent();
        if (parent != null && !Files.exists(parent)) {
            Files.createDirectories(parent);
        }
    }

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
