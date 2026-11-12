package com.vertexforge.launcher.settings;

/**
 * The launcher's own preferences, from {@code ~/.vertexforge/launcher-settings.json} (VK-1678). VK-1694 adds the
 * Settings dialog that writes them.
 *
 * @param recentProjectsLimit how many projects the recent list keeps; pinned ones and the one just added are never
 *                            dropped to honour it
 */
public record LauncherSettings(int recentProjectsLimit) {

    public static final int DEFAULT_RECENT_PROJECTS_LIMIT = 50;
    public static final int MIN_RECENT_PROJECTS_LIMIT = 1;
    public static final int MAX_RECENT_PROJECTS_LIMIT = 500;

    public static final LauncherSettings DEFAULTS = new LauncherSettings(DEFAULT_RECENT_PROJECTS_LIMIT);

    public LauncherSettings {
        if (recentProjectsLimit < MIN_RECENT_PROJECTS_LIMIT || recentProjectsLimit > MAX_RECENT_PROJECTS_LIMIT) {
            throw new IllegalArgumentException("recentProjectsLimit must be from " + MIN_RECENT_PROJECTS_LIMIT
                    + " to " + MAX_RECENT_PROJECTS_LIMIT + ": " + recentProjectsLimit);
        }
    }
}
