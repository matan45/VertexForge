package com.vertexforge.launcher.settings;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import com.vertexforge.launcher.settings.LauncherState.SavedWindow;
import com.vertexforge.launcher.settings.LauncherState.SortKey;
import com.vertexforge.launcher.store.AtomicJsonStore;
import com.vertexforge.launcher.store.DocumentFormat;
import com.vertexforge.launcher.store.StoreSnapshot;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;

import java.nio.file.Path;
import java.time.Clock;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalDouble;
import java.util.OptionalInt;
import java.util.function.UnaryOperator;

/**
 * Reads and writes {@code ~/.vertexforge/launcher-state.json} (VK-1694): what the launcher remembers by itself.
 * <pre>
 * { "version": 1,
 *   "window": { "x": 120.0, "y": 80.0, "width": 1024.0, "height": 768.0, "maximized": false },
 *   "projectSort": [ { "column": "name", "ascending": true } ] }
 * </pre>
 * It is a file of its own, apart from {@code launcher-settings.json}, because it is written in the background (when
 * the window moves, when a column header is clicked): a background write must never set aside a settings file the user
 * is editing by hand. Reading never writes; each save changes only its own member and never throws (a failure is a
 * warning: losing a window position is not worth an error). Saves wait at most {@link #LOCK_TIMEOUT} for the file, so
 * that the store thread they share with the recent-projects list is never held up for long.
 */
public final class LauncherStateRepository {

    public static final String FILE_NAME = "launcher-state.json";
    public static final String WINDOW = "window";
    public static final String PROJECT_SORT = "projectSort";
    public static final Duration LOCK_TIMEOUT = Duration.ofSeconds(1);
    public static final DocumentFormat FORMAT = new DocumentFormat(1, OptionalInt.of(1), root -> {
        // Every member is optional and checked on its own.
    });

    private final AtomicJsonStore store;
    private final LauncherLog log;

    public LauncherStateRepository(Path file, Clock clock, LauncherLog log) {
        this.log = Objects.requireNonNull(log, "log");
        this.store = new AtomicJsonStore(file, FORMAT, clock, LOCK_TIMEOUT, log::warn);
    }

    public Path file() {
        return store.file();
    }

    /** What was remembered; never throws and never writes, moves or creates the file. */
    public LauncherState load() {
        StoreSnapshot snapshot = store.inspect();
        switch (snapshot.status()) {
            case ABSENT -> {
                return LauncherState.EMPTY;
            }
            case CORRUPT, CORRUPT_UNMOVABLE, UNREADABLE -> {
                log.warn("Ignoring the launcher state file " + store.file() + " (" + snapshot.detail()
                        + "); the window opens at its default place and the list in its own order");
                return LauncherState.EMPTY;
            }
            default -> {
                // OK, LEGACY or NEWER_READ_ONLY: read the members this version knows.
            }
        }
        JsonObject root = snapshot.root();
        return new LauncherState(window(root), projectSort(root));
    }

    /** Remembers the window, keeping every other member. Store thread; never throws. */
    public boolean saveWindow(SavedWindow window) {
        Objects.requireNonNull(window, "window");
        return save("the window position", root -> {
            JsonObject member = new JsonObject();
            member.addProperty("x", window.x());
            member.addProperty("y", window.y());
            member.addProperty("width", window.width());
            member.addProperty("height", window.height());
            member.addProperty("maximized", window.maximized());
            root.add(WINDOW, member);
            return root;
        });
    }

    /** Remembers the list's sort, keeping every other member; an empty sort removes it. Store thread; never throws. */
    public boolean saveProjectSort(List<SortKey> sort) {
        List<SortKey> keys = List.copyOf(sort);
        return save("the project list's sort", root -> {
            if (keys.isEmpty()) {
                root.remove(PROJECT_SORT);
                return root;
            }
            JsonArray member = new JsonArray();
            for (SortKey key : keys) {
                JsonObject element = new JsonObject();
                element.addProperty("column", key.column());
                element.addProperty("ascending", key.ascending());
                member.add(element);
            }
            root.add(PROJECT_SORT, member);
            return root;
        });
    }

    private boolean save(String what, UnaryOperator<JsonObject> mutation) {
        try {
            StoreSnapshot written = store.update(mutation);
            written.quarantinedTo().ifPresent(movedTo -> log.warn("The launcher state file was damaged; it was kept as "
                    + movedTo.getFileName() + " and a new one was written"));
            return true;
        } catch (StoreWriteException e) {
            log.warn("Could not remember " + what + ": " + e.getMessage());
            return false;
        }
    }

    private Optional<SavedWindow> window(JsonObject root) {
        JsonElement value = root.get(WINDOW);
        if (value == null || value.isJsonNull()) {
            return Optional.empty();
        }
        if (value.isJsonObject()) {
            JsonObject member = value.getAsJsonObject();
            OptionalDouble x = number(member, "x");
            OptionalDouble y = number(member, "y");
            OptionalDouble width = number(member, "width");
            OptionalDouble height = number(member, "height");
            Optional<Boolean> maximized = flag(member, "maximized");
            if (x.isPresent() && y.isPresent() && width.isPresent() && height.isPresent() && maximized.isPresent()
                    && SavedWindow.valid(x.getAsDouble(), y.getAsDouble(), width.getAsDouble(),
                    height.getAsDouble())) {
                return Optional.of(new SavedWindow(x.getAsDouble(), y.getAsDouble(), width.getAsDouble(),
                        height.getAsDouble(), maximized.get()));
            }
        }
        log.warn("Ignoring \"" + WINDOW + "\": " + value + " in " + store.file()
                + "; the window opens at its default place");
        return Optional.empty();
    }

    private List<SortKey> projectSort(JsonObject root) {
        JsonElement value = root.get(PROJECT_SORT);
        if (value == null || value.isJsonNull()) {
            return List.of();
        }
        if (!value.isJsonArray()) {
            log.warn("Ignoring \"" + PROJECT_SORT + "\": " + value + " in " + store.file()
                    + "; the list keeps its own order");
            return List.of();
        }
        List<SortKey> keys = new ArrayList<>();
        for (JsonElement element : value.getAsJsonArray()) {
            Optional<SortKey> key = sortKey(element);
            if (key.isPresent()) {
                keys.add(key.get());
            } else {
                log.warn("Ignoring the sort key " + element + " in " + store.file());
            }
        }
        return keys;
    }

    private static Optional<SortKey> sortKey(JsonElement element) {
        if (!element.isJsonObject()) {
            return Optional.empty();
        }
        JsonObject object = element.getAsJsonObject();
        JsonElement column = object.get("column");
        Optional<Boolean> ascending = flag(object, "ascending");
        if (column == null || !column.isJsonPrimitive() || !column.getAsJsonPrimitive().isString()
                || ascending.isEmpty()) {
            return Optional.empty();
        }
        return Optional.of(new SortKey(column.getAsString(), ascending.get()));
    }

    private static OptionalDouble number(JsonObject object, String name) {
        JsonElement value = object.get(name);
        if (value == null || !value.isJsonPrimitive() || !value.getAsJsonPrimitive().isNumber()) {
            return OptionalDouble.empty();
        }
        double number = value.getAsDouble();
        return Double.isFinite(number) ? OptionalDouble.of(number) : OptionalDouble.empty();
    }

    /** A boolean member; absent counts as {@code false}, anything but a boolean as invalid. */
    private static Optional<Boolean> flag(JsonObject object, String name) {
        JsonElement value = object.get(name);
        if (value == null || value.isJsonNull()) {
            return Optional.of(false);
        }
        if (value.isJsonPrimitive()) {
            JsonPrimitive primitive = value.getAsJsonPrimitive();
            if (primitive.isBoolean()) {
                return Optional.of(primitive.getAsBoolean());
            }
        }
        return Optional.empty();
    }
}
