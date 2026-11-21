package com.vertexforge.launcher.settings;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.store.AtomicJsonStore;
import com.vertexforge.launcher.store.DocumentFormat;
import com.vertexforge.launcher.store.StoreSnapshot;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;

import java.math.BigDecimal;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.time.Clock;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * Reads and writes {@code ~/.vertexforge/launcher-settings.json} (VK-1678). The editor's {@code editor_settings.json}
 * lives in the same folder, so the launcher keeps its own file.
 * <pre>
 * { "version": 1, "recentProjectsLimit": 50, "afterLaunch": "minimize", "showEditorConsole": true,
 *   "defaultProjectLocation": "D:\\Projects", "logRetention": 20 }
 * </pre>
 * The file may have been written by hand, so reading is forgiving and never changes the file: a missing
 * {@code version} means 1, and a corrupt file or an invalid value falls back to the default with a warning. Only
 * {@link #save} (the Settings dialog, VK-1683, VK-1694) writes it: the members it owns, keeping every other one; like
 * every {@link AtomicJsonStore} write, it first sets a corrupt file aside. What the launcher remembers by itself is
 * written to {@link LauncherStateRepository}'s file instead, so that no background write ever sets aside a file the
 * user is editing by hand.
 */
public final class LauncherSettingsRepository {

    public static final String FILE_NAME = "launcher-settings.json";
    public static final String RECENT_PROJECTS_LIMIT = "recentProjectsLimit";
    public static final String AFTER_LAUNCH = "afterLaunch";
    public static final String SHOW_EDITOR_CONSOLE = "showEditorConsole";
    public static final String DEFAULT_PROJECT_LOCATION = "defaultProjectLocation";
    public static final String LOG_RETENTION = "logRetention";
    public static final DocumentFormat FORMAT = new DocumentFormat(1, OptionalInt.of(1), root -> {
        // Every member is optional and checked on its own.
    });

    private final AtomicJsonStore store;
    private final LauncherLog log;
    /** The file was written by a newer launcher when it was last read or saved, so every save is refused. */
    private volatile boolean newer;

    public LauncherSettingsRepository(Path file, Clock clock, LauncherLog log) {
        this.log = Objects.requireNonNull(log, "log");
        this.store = new AtomicJsonStore(file, FORMAT, clock, AtomicJsonStore.DEFAULT_LOCK_TIMEOUT, log::warn);
    }

    /**
     * Whether the file was written by a newer version of the launcher when it was last read or saved: this version then
     * never overwrites it, so the Settings dialog cannot save (VK-1694). Never touches the disk; any thread.
     */
    public boolean newerFile() {
        return newer;
    }

    /** The settings; never throws and never writes, moves or creates the file. */
    public LauncherSettings load() {
        StoreSnapshot snapshot = store.inspect();
        newer = snapshot.status() == StoreStatus.NEWER_READ_ONLY;
        switch (snapshot.status()) {
            case ABSENT -> {
                return LauncherSettings.DEFAULTS;
            }
            case CORRUPT, CORRUPT_UNMOVABLE, UNREADABLE -> {
                log.warn("Ignoring the launcher settings file " + store.file() + " (" + snapshot.detail()
                        + "); using the default settings");
                return LauncherSettings.DEFAULTS;
            }
            default -> {
                // OK, LEGACY or NEWER_READ_ONLY: read the members this version knows.
            }
        }
        return fromRoot(snapshot.root());
    }

    /**
     * Writes every member the settings own (VK-1683, VK-1694), keeping every other member, and returns the settings as
     * written. An unset default project location removes its member. Blocks on the file lock, so it runs on the store
     * thread.
     *
     * @throws StoreWriteException when nothing was written (a newer or unreadable file, a lock timeout, ...)
     */
    public LauncherSettings save(LauncherSettings settings) throws StoreWriteException {
        Objects.requireNonNull(settings, "settings");
        StoreSnapshot written;
        try {
            written = store.update(root -> {
                root.addProperty(RECENT_PROJECTS_LIMIT, settings.recentProjectsLimit());
                root.addProperty(AFTER_LAUNCH, settings.afterLaunch().jsonName());
                root.addProperty(SHOW_EDITOR_CONSOLE, settings.showEditorConsole());
                settings.defaultProjectLocation().ifPresentOrElse(
                        location -> root.addProperty(DEFAULT_PROJECT_LOCATION, location.toString()),
                        () -> root.remove(DEFAULT_PROJECT_LOCATION));
                root.addProperty(LOG_RETENTION, settings.logRetention());
                return root;
            });
        } catch (StoreWriteException e) {
            if (e.reason() == StoreWriteException.Reason.NEWER_VERSION) {
                newer = true;
            }
            log.warn("Failed to save the launcher settings: " + e.getMessage());
            throw e;
        }
        newer = false;
        written.quarantinedTo().ifPresent(movedTo -> log.warn("The launcher settings file was damaged; it was kept as "
                + movedTo.getFileName() + " and a new one was written"));
        return fromRoot(written.root());
    }

    private LauncherSettings fromRoot(JsonObject root) {
        return new LauncherSettings(recentProjectsLimit(root), afterLaunch(root), showEditorConsole(root),
                defaultProjectLocation(root), logRetention(root));
    }

    /** An absolute folder path; an empty text means none, anything else that is not one is ignored with a warning. */
    private Optional<Path> defaultProjectLocation(JsonObject root) {
        JsonElement value = root.get(DEFAULT_PROJECT_LOCATION);
        if (value == null || value.isJsonNull()) {
            return Optional.empty();
        }
        if (value.isJsonPrimitive() && value.getAsJsonPrimitive().isString()) {
            String text = value.getAsString();
            if (text.isBlank()) {
                return Optional.empty();
            }
            Optional<Path> location = absolutePath(text);
            if (location.isPresent()) {
                return location;
            }
        }
        log.warn("Ignoring \"" + DEFAULT_PROJECT_LOCATION + "\": " + value + " in " + store.file()
                + ": it must be the absolute path of a folder; Create Project starts with an empty location");
        return Optional.empty();
    }

    /** {@code text} as an absolute path, or empty when it is not one (relative, or not a valid path). */
    public static Optional<Path> absolutePath(String text) {
        try {
            Path path = Path.of(text);
            return path.isAbsolute() ? Optional.of(path) : Optional.empty();
        } catch (InvalidPathException e) {
            return Optional.empty();
        }
    }

    private int logRetention(JsonObject root) {
        JsonElement value = root.get(LOG_RETENTION);
        if (value == null || value.isJsonNull()) {
            return LauncherSettings.DEFAULT_LOG_RETENTION;
        }
        OptionalInt retention = wholeNumber(value);
        if (retention.isEmpty()
                || retention.getAsInt() < LauncherSettings.MIN_LOG_RETENTION
                || retention.getAsInt() > LauncherSettings.MAX_LOG_RETENTION) {
            log.warn("Ignoring \"" + LOG_RETENTION + "\": " + value + " in " + store.file()
                    + ": it must be a whole number from " + LauncherSettings.MIN_LOG_RETENTION + " to "
                    + LauncherSettings.MAX_LOG_RETENTION + "; using " + LauncherSettings.DEFAULT_LOG_RETENTION);
            return LauncherSettings.DEFAULT_LOG_RETENTION;
        }
        return retention.getAsInt();
    }

    private LauncherSettings.AfterLaunch afterLaunch(JsonObject root) {
        JsonElement value = root.get(AFTER_LAUNCH);
        if (value == null || value.isJsonNull()) {
            return LauncherSettings.DEFAULT_AFTER_LAUNCH;
        }
        if (value.isJsonPrimitive() && value.getAsJsonPrimitive().isString()) {
            Optional<LauncherSettings.AfterLaunch> parsed =
                    LauncherSettings.AfterLaunch.fromJsonName(value.getAsString());
            if (parsed.isPresent()) {
                return parsed.get();
            }
        }
        log.warn("Ignoring \"" + AFTER_LAUNCH + "\": " + value + " in " + store.file()
                + ": it must be \"minimize\", \"close\" or \"stay\"; using \""
                + LauncherSettings.DEFAULT_AFTER_LAUNCH.jsonName() + "\"");
        return LauncherSettings.DEFAULT_AFTER_LAUNCH;
    }

    private boolean showEditorConsole(JsonObject root) {
        JsonElement value = root.get(SHOW_EDITOR_CONSOLE);
        if (value == null || value.isJsonNull()) {
            return LauncherSettings.DEFAULT_SHOW_EDITOR_CONSOLE;
        }
        if (value.isJsonPrimitive() && value.getAsJsonPrimitive().isBoolean()) {
            return value.getAsBoolean();
        }
        log.warn("Ignoring \"" + SHOW_EDITOR_CONSOLE + "\": " + value + " in " + store.file()
                + ": it must be true or false; using " + LauncherSettings.DEFAULT_SHOW_EDITOR_CONSOLE);
        return LauncherSettings.DEFAULT_SHOW_EDITOR_CONSOLE;
    }

    private int recentProjectsLimit(JsonObject root) {
        JsonElement value = root.get(RECENT_PROJECTS_LIMIT);
        if (value == null || value.isJsonNull()) {
            return LauncherSettings.DEFAULT_RECENT_PROJECTS_LIMIT;
        }
        OptionalInt limit = wholeNumber(value);
        if (limit.isEmpty()
                || limit.getAsInt() < LauncherSettings.MIN_RECENT_PROJECTS_LIMIT
                || limit.getAsInt() > LauncherSettings.MAX_RECENT_PROJECTS_LIMIT) {
            log.warn("Ignoring \"" + RECENT_PROJECTS_LIMIT + "\": " + value + " in " + store.file()
                    + ": it must be a whole number from " + LauncherSettings.MIN_RECENT_PROJECTS_LIMIT + " to "
                    + LauncherSettings.MAX_RECENT_PROJECTS_LIMIT + "; using "
                    + LauncherSettings.DEFAULT_RECENT_PROJECTS_LIMIT);
            return LauncherSettings.DEFAULT_RECENT_PROJECTS_LIMIT;
        }
        return limit.getAsInt();
    }

    private static OptionalInt wholeNumber(JsonElement element) {
        if (!element.isJsonPrimitive() || !element.getAsJsonPrimitive().isNumber()) {
            return OptionalInt.empty();
        }
        try {
            BigDecimal value = element.getAsBigDecimal();
            return OptionalInt.of(value.intValueExact());
        } catch (ArithmeticException | NumberFormatException e) {
            return OptionalInt.empty();
        }
    }
}
