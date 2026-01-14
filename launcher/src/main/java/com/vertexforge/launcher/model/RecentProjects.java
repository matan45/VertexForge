package com.vertexforge.launcher.model;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.*;

public class RecentProjects {
    public static final int DEFAULT_MAX_SIZE = 10;

    private final int maxSize;
    private final Map<Path, ProjectInfo> projectsByPath;

    public RecentProjects() {
        this(DEFAULT_MAX_SIZE);
    }

    public RecentProjects(int maxSize) {
        if (maxSize < 1) {
            throw new IllegalArgumentException("Max size must be at least 1");
        }
        this.maxSize = maxSize;
        this.projectsByPath = new LinkedHashMap<>();
    }


    public RecentProjects add(ProjectInfo project) {
        Objects.requireNonNull(project, "Project cannot be null");

        // Remove existing entry if present (will be re-added)
        projectsByPath.remove(project.path());

        // Add the project
        projectsByPath.put(project.path(), project);

        // Trim if exceeds max size (remove oldest entries)
        trimToSize();

        return this;
    }

    public void touch(Path path) {
        ProjectInfo existing = projectsByPath.get(path);
        if (existing == null) {
            return;
        }

        // Remove and re-add with new timestamp to update ordering
        projectsByPath.remove(path);
        ProjectInfo updated = existing.withLastOpened(Instant.now());
        projectsByPath.put(path, updated);

    }

    public void remove(Path path) {
        projectsByPath.remove(path);
    }


    public List<ProjectInfo> getAll() {
        return projectsByPath.values().stream()
                .sorted(Comparator.comparing(ProjectInfo::lastOpened).reversed())
                .toList();
    }

    public int size() {
        return projectsByPath.size();
    }

    public boolean isEmpty() {
        return projectsByPath.isEmpty();
    }

    public void removeInvalid() {
        List<Path> toRemove = projectsByPath.keySet().stream()
                .filter(path -> !Files.exists(path))
                .toList();

        toRemove.forEach(projectsByPath::remove);
    }

    public int countInvalid() {
        return (int) projectsByPath.keySet().stream()
                .filter(path -> !Files.exists(path))
                .count();
    }

    private void trimToSize() {
        while (projectsByPath.size() > maxSize) {
            // Find the entry with the oldest (minimum) lastOpened timestamp
            Path oldestPath = projectsByPath.entrySet().stream()
                    .min(Comparator.comparing(e -> e.getValue().lastOpened()))
                    .map(Map.Entry::getKey)
                    .orElse(null);

            if (oldestPath != null) {
                projectsByPath.remove(oldestPath);
            } else {
                break;
            }
        }
    }
}
