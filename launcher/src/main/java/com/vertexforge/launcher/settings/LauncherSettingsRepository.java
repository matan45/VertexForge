package com.vertexforge.launcher.settings;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.store.AtomicJsonStore;
import com.vertexforge.launcher.store.DocumentFormat;
import com.vertexforge.launcher.store.StoreSnapshot;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;

import java.math.BigDecimal;
import java.nio.file.Path;
import java.time.Clock;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * Reads and writes {@code ~/.vertexforge/launcher-settings.json} (VK-1678). The editor's {@code editor_settings.json}
 * lives in the same folder, so the launcher keeps its own file.
 * <pre>
 * { "version": 1, "recentProjectsLimit": 50, "afterLaunch": "minimize", "showEditorConsole": true }
 * </pre>
 * The file may have been written by hand, so reading is forgiving and never changes the file: a missing
 * {@code version} means 1, and a corrupt file or an invalid value falls back to the default with a warning.
 * {@link #save} (the Settings dialog, VK-1683) writes only the members it owns and keeps every other one; like every
 * {@link AtomicJsonStore} write, it first sets a corrupt file aside.
 */
public final class LauncherSettingsRepository {

    public static final String FILE_NAME = "launcher-settings.json";
    public static final String RECENT_PROJECTS_LIMIT = "recentProjectsLimit";
    public static final String AFTER_LAUNCH = "afterLaunch";
    public static final String SHOW_EDITOR_CONSOLE = "showEditorConsole";
    public static final DocumentFormat FORMAT = new DocumentFormat(1, OptionalInt.of(1), root -> {
        // Every member is optional and checked on its own.
    });

    private final AtomicJsonStore store;
    private final LauncherLog log;

    public LauncherSettingsRepository(Path file, Clock clock, LauncherLog log) {
        this.log = Objects.requireNonNull(log, "log");
        this.store = new AtomicJsonStore(file, FORMAT, clock, AtomicJsonStore.DEFAULT_LOCK_TIMEOUT, log::warn);
    }

    /** The settings; never throws and never writes, moves or creates the file. */
    public LauncherSettings load() {
        StoreSnapshot snapshot = store.inspect();
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
     * Writes the launch behaviour (VK-1683), keeping every other member, and returns the settings as written. Blocks
     * on the file lock, so it runs on the store thread.
     *
     * @throws StoreWriteException when nothing was written (a newer or unreadable file, a lock timeout, ...)
     */
    public LauncherSettings save(LauncherSettings.AfterLaunch afterLaunch, boolean showEditorConsole)
            throws StoreWriteException {
        Objects.requireNonNull(afterLaunch, "afterLaunch");
        StoreSnapshot written;
        try {
            written = store.update(root -> {
                root.addProperty(AFTER_LAUNCH, afterLaunch.jsonName());
                root.addProperty(SHOW_EDITOR_CONSOLE, showEditorConsole);
                return root;
            });
        } catch (StoreWriteException e) {
            log.warn("Failed to save the launcher settings: " + e.getMessage());
            throw e;
        }
        written.quarantinedTo().ifPresent(movedTo -> log.warn("The launcher settings file was damaged; it was kept as "
                + movedTo.getFileName() + " and a new one was written"));
        return fromRoot(written.root());
    }

    private LauncherSettings fromRoot(JsonObject root) {
        return new LauncherSettings(recentProjectsLimit(root), afterLaunch(root), showEditorConsole(root));
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
