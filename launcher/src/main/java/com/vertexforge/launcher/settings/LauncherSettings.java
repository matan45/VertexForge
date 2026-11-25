package com.vertexforge.launcher.settings;

import java.nio.file.Path;
import java.util.Locale;
import java.util.Objects;
import java.util.Optional;

/**
 * The launcher's own preferences, from {@code ~/.vertexforge/launcher-settings.json} (VK-1678). The Settings dialog
 * writes them all (VK-1683, VK-1694). What the launcher remembers by itself (the window, the list's sort) lives in
 * {@link LauncherState} instead.
 *
 * @param recentProjectsLimit    how many projects the recent list keeps; pinned ones and the one just added are never
 *                               dropped to honour it
 * @param afterLaunch            what the launcher does once an editor it started is ready (VK-1683)
 * @param showEditorConsole      whether the editor opens a console window of its own (VK-1683); only engines that
 *                               advertise the {@code --console} flag get it
 * @param defaultProjectLocation the folder Create Project starts in (VK-1694); empty for none (the field starts
 *                               empty). Always absolute
 * @param logRetention           how many per-launch logs are kept in {@code ~/.vertexforge/logs} (VK-1694)
 */
public record LauncherSettings(int recentProjectsLimit, AfterLaunch afterLaunch, boolean showEditorConsole,
                               Optional<Path> defaultProjectLocation, int logRetention) {

    public static final int DEFAULT_RECENT_PROJECTS_LIMIT = 50;
    public static final int MIN_RECENT_PROJECTS_LIMIT = 1;
    public static final int MAX_RECENT_PROJECTS_LIMIT = 500;
    public static final AfterLaunch DEFAULT_AFTER_LAUNCH = AfterLaunch.MINIMIZE;
    public static final boolean DEFAULT_SHOW_EDITOR_CONSOLE = true;
    /** As many as the launcher kept before the setting existed ({@code SessionLogs.KEEP}; a test pins them equal). */
    public static final int DEFAULT_LOG_RETENTION = 20;
    public static final int MIN_LOG_RETENTION = 1;
    public static final int MAX_LOG_RETENTION = 200;

    public static final LauncherSettings DEFAULTS = new LauncherSettings(DEFAULT_RECENT_PROJECTS_LIMIT);

    /** What the launcher does once the editor it started reports that it is ready (VK-1683). */
    public enum AfterLaunch {
        /** Minimizes, and comes back when the editor exits. */
        MINIMIZE("Minimize, and come back when the editor closes"),
        /** Closes the launcher; the editor keeps running and its log stays complete. */
        CLOSE("Close the launcher"),
        /** Stays as it is. */
        STAY("Stay open");

        private final String label;

        AfterLaunch(String label) {
            this.label = label;
        }

        /** The value stored in the settings file: {@code "minimize"}, {@code "close"} or {@code "stay"}. */
        public String jsonName() {
            return name().toLowerCase(Locale.ROOT);
        }

        public String label() {
            return label;
        }

        public static Optional<AfterLaunch> fromJsonName(String text) {
            for (AfterLaunch value : values()) {
                if (value.jsonName().equals(text)) {
                    return Optional.of(value);
                }
            }
            return Optional.empty();
        }
    }

    public LauncherSettings {
        checkRecentProjectsLimit(recentProjectsLimit);
        Objects.requireNonNull(afterLaunch, "afterLaunch");
        Objects.requireNonNull(defaultProjectLocation, "defaultProjectLocation");
        defaultProjectLocation.ifPresent(location -> {
            if (!location.isAbsolute()) {
                throw new IllegalArgumentException("defaultProjectLocation must be absolute: " + location);
            }
        });
        checkLogRetention(logRetention);
    }

    /** The given limit and the default for everything else. */
    public LauncherSettings(int recentProjectsLimit) {
        this(recentProjectsLimit, DEFAULT_AFTER_LAUNCH, DEFAULT_SHOW_EDITOR_CONSOLE);
    }

    /** The given limit and launch behaviour (VK-1683), and the default for everything else. */
    public LauncherSettings(int recentProjectsLimit, AfterLaunch afterLaunch, boolean showEditorConsole) {
        this(recentProjectsLimit, afterLaunch, showEditorConsole, Optional.empty(), DEFAULT_LOG_RETENTION);
    }

    /** Throws when {@code limit} is outside {@link #MIN_RECENT_PROJECTS_LIMIT}..{@link #MAX_RECENT_PROJECTS_LIMIT}. */
    public static int checkRecentProjectsLimit(int limit) {
        if (limit < MIN_RECENT_PROJECTS_LIMIT || limit > MAX_RECENT_PROJECTS_LIMIT) {
            throw new IllegalArgumentException("recentProjectsLimit must be from " + MIN_RECENT_PROJECTS_LIMIT
                    + " to " + MAX_RECENT_PROJECTS_LIMIT + ": " + limit);
        }
        return limit;
    }

    /** Throws when {@code retention} is outside {@link #MIN_LOG_RETENTION}..{@link #MAX_LOG_RETENTION}. */
    public static int checkLogRetention(int retention) {
        if (retention < MIN_LOG_RETENTION || retention > MAX_LOG_RETENTION) {
            throw new IllegalArgumentException("logRetention must be from " + MIN_LOG_RETENTION + " to "
                    + MAX_LOG_RETENTION + ": " + retention);
        }
        return retention;
    }
}
