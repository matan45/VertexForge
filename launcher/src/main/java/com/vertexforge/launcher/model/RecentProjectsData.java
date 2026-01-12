package com.vertexforge.launcher.model;

import java.util.List;

/**
 * Data Transfer Object for JSON serialization of recent projects.
 * <p>
 * This record represents the JSON schema for persisting recent projects:
 * <pre>
 * {
 *   "version": 1,
 *   "projects": [
 *     {"name": "MyGame", "path": "C:/path/to.vfproj", "lastOpened": "2026-01-12T14:30:00Z"}
 *   ]
 * }
 * </pre>
 * </p>
 *
 * @param version  schema version for future migration support
 * @param projects list of project data entries
 */
public record RecentProjectsData(
        int version,
        List<ProjectData> projects
) {
    /**
     * Current schema version.
     */
    public static final int CURRENT_VERSION = 1;

    /**
     * Creates a new RecentProjectsData with the current schema version.
     *
     * @param projects list of project data entries
     * @return a new RecentProjectsData instance
     */
    public static RecentProjectsData create(List<ProjectData> projects) {
        return new RecentProjectsData(CURRENT_VERSION, projects);
    }

    /**
     * Data Transfer Object for a single project entry in JSON.
     *
     * @param name       display name of the project
     * @param path       absolute path to the .vfproj file (as string)
     * @param lastOpened ISO-8601 timestamp of when the project was last opened
     */
    public record ProjectData(
            String name,
            String path,
            String lastOpened
    ) {
    }
}
