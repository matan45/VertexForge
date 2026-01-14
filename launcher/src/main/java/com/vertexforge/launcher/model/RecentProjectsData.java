package com.vertexforge.launcher.model;

import java.util.List;


public record RecentProjectsData(
        int version,
        List<ProjectData> projects
) {
    public static final int CURRENT_VERSION = 1;

    public static RecentProjectsData create(List<ProjectData> projects) {
        return new RecentProjectsData(CURRENT_VERSION, projects);
    }

    public record ProjectData(
            String name,
            String path,
            String lastOpened
    ) {
    }
}
