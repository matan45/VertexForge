package com.vertexforge.launcher.model;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.*;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Container for managing a collection of recently opened projects.
 * <p>
 * Maintains ordering by last-opened timestamp (most recent first),
 * enforces uniqueness by project path, and limits the maximum number of entries.
 * </p>
 */
public class RecentProjects {

    /**
     * Default maximum number of recent projects to track.
     */
    public static final int DEFAULT_MAX_SIZE = 10;

    private final int maxSize;
    private final Map<Path, ProjectInfo> projectsByPath;

    /**
     * Creates a new RecentProjects container with the default maximum size.
     */
    public RecentProjects() {
        this(DEFAULT_MAX_SIZE);
    }

    /**
     * Creates a new RecentProjects container with a custom maximum size.
     *
     * @param maxSize maximum number of projects to retain
     * @throws IllegalArgumentException if maxSize is less than 1
     */
    public RecentProjects(int maxSize) {
        if (maxSize < 1) {
            throw new IllegalArgumentException("Max size must be at least 1");
        }
        this.maxSize = maxSize;
        this.projectsByPath = new LinkedHashMap<>();
    }

    /**
     * Adds or updates a project in the collection.
     * <p>
     * If a project with the same path exists, it is updated with the new timestamp.
     * If the collection exceeds maxSize, the oldest project is removed.
     * </p>
     *
     * @param project the project to add or update
     * @return this instance for method chaining
     */
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

    /**
     * Updates the last-opened timestamp for an existing project.
     *
     * @param path the path of the project to update
     * @return true if the project was found and updated, false otherwise
     */
    public boolean touch(Path path) {
        ProjectInfo existing = projectsByPath.get(path);
        if (existing == null) {
            return false;
        }

        // Remove and re-add with new timestamp to update ordering
        projectsByPath.remove(path);
        ProjectInfo updated = existing.withLastOpened(Instant.now());
        projectsByPath.put(path, updated);

        return true;
    }

    /**
     * Removes a project from the collection.
     *
     * @param path the path of the project to remove
     * @return true if the project was removed, false if it wasn't present
     */
    public boolean remove(Path path) {
        return projectsByPath.remove(path) != null;
    }

    /**
     * Gets a project by its path.
     *
     * @param path the path to look up
     * @return an Optional containing the project if found
     */
    public Optional<ProjectInfo> get(Path path) {
        return Optional.ofNullable(projectsByPath.get(path));
    }

    /**
     * Checks if a project with the given path exists in the collection.
     *
     * @param path the path to check
     * @return true if a project with this path exists
     */
    public boolean contains(Path path) {
        return projectsByPath.containsKey(path);
    }

    /**
     * Returns all projects sorted by last-opened time (most recent first).
     *
     * @return an unmodifiable list of projects in sorted order
     */
    public List<ProjectInfo> getAll() {
        return projectsByPath.values().stream()
                .sorted(Comparator.comparing(ProjectInfo::lastOpened).reversed())
                .collect(Collectors.toUnmodifiableList());
    }

    /**
     * Returns all projects as a stream, sorted by last-opened time.
     *
     * @return a stream of projects (most recent first)
     */
    public Stream<ProjectInfo> stream() {
        return getAll().stream();
    }

    /**
     * Returns the number of projects in the collection.
     *
     * @return the size of the collection
     */
    public int size() {
        return projectsByPath.size();
    }

    /**
     * Checks if the collection is empty.
     *
     * @return true if there are no projects
     */
    public boolean isEmpty() {
        return projectsByPath.isEmpty();
    }

    /**
     * Removes all projects from the collection.
     */
    public void clear() {
        projectsByPath.clear();
    }

    /**
     * Returns the maximum size of this collection.
     *
     * @return the maximum number of projects that can be stored
     */
    public int getMaxSize() {
        return maxSize;
    }

    /**
     * Removes projects whose paths no longer exist on the filesystem.
     *
     * @return the number of projects removed
     */
    public int removeInvalid() {
        List<Path> toRemove = projectsByPath.keySet().stream()
                .filter(path -> !Files.exists(path))
                .toList();

        toRemove.forEach(projectsByPath::remove);
        return toRemove.size();
    }

    /**
     * Trims the collection to the maximum size by removing the oldest entry.
     * <p>
     * Only removes one entry at a time since add() is called once per operation.
     * This is O(n) for finding the oldest entry, compared to the previous
     * O(n log n) approach of sorting the entire collection.
     * </p>
     */
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
