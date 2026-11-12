package com.vertexforge.launcher.model;

import com.google.gson.JsonElement;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.time.Instant;
import java.time.format.DateTimeParseException;
import java.util.Objects;
import java.util.UUID;

/**
 * One entry of the recent-projects list (schema v2, VK-1678).
 * <p>
 * {@code raw} is the entry's JSON object exactly as stored, and it is the source of truth: the typed fields are read
 * from it, a change copies it and sets one member, and members this build does not know are written back unchanged.
 * Nothing is ever dropped because a field is missing or invalid; such a field simply reads as {@code null}.
 *
 * @param id         the entry's identity; never re-derived once resolved
 * @param name       the stored name, or {@code null}
 * @param rawPath    the stored path text, or {@code null}
 * @param path       {@code rawPath} as an absolute, normalized path; {@code null} when it is absent, relative or invalid
 * @param lastOpened when the project was last opened or created, or {@code null} when missing or invalid
 * @param pinned     whether the entry is pinned (it is then never trimmed); {@code false} unless stored as a boolean
 * @param engineId   the engine install the project is bound to (VK-1681), or {@code null}
 * @param raw        the stored JSON object; copied in and out
 */
public record RecentEntry(
        UUID id,
        String name,
        String rawPath,
        Path path,
        Instant lastOpened,
        boolean pinned,
        String engineId,
        JsonObject raw
) {
    public static final String ID = "id";
    public static final String NAME = "name";
    public static final String PATH = "path";
    public static final String LAST_OPENED = "lastOpened";
    public static final String PINNED = "pinned";
    public static final String ENGINE_ID = "engineId";

    private static final String PROJECT_EXTENSION = ".vfproj";

    public RecentEntry {
        Objects.requireNonNull(id, "id");
        raw = Objects.requireNonNull(raw, "raw").deepCopy();
    }

    @Override
    public JsonObject raw() {
        return raw.deepCopy();
    }

    /** Reads the typed fields of a stored entry; {@code id} is resolved by the caller (the codec). */
    public static RecentEntry fromJson(UUID id, JsonObject raw) {
        String rawPath = string(raw, PATH);
        return new RecentEntry(
                id,
                string(raw, NAME),
                rawPath,
                absolutePath(rawPath),
                instant(string(raw, LAST_OPENED)),
                bool(raw, PINNED),
                string(raw, ENGINE_ID),
                raw);
    }

    /** A new entry with all six v2 members, in schema order. */
    public static RecentEntry create(UUID id, Path path, String name, Instant lastOpened) {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(lastOpened, "lastOpened");
        JsonObject raw = new JsonObject();
        raw.addProperty(ID, id.toString());
        raw.addProperty(NAME, name);
        raw.addProperty(PATH, path.toString());
        raw.addProperty(LAST_OPENED, lastOpened.toString());
        raw.addProperty(PINNED, false);
        raw.add(ENGINE_ID, JsonNull.INSTANCE);
        return fromJson(id, raw);
    }

    public RecentEntry withLastOpened(Instant instant) {
        Objects.requireNonNull(instant, "instant");
        return with(LAST_OPENED, new JsonPrimitive(instant.toString()));
    }

    public RecentEntry withName(String newName) {
        if (Objects.equals(newName, name)) {
            return this;
        }
        return with(NAME, newName == null ? JsonNull.INSTANCE : new JsonPrimitive(newName));
    }

    public RecentEntry withPinned(boolean newPinned) {
        return with(PINNED, new JsonPrimitive(newPinned));
    }

    /** The name to show: the stored name, else the project file's name without {@code .vfproj}, else the raw path. */
    public String displayName() {
        if (name != null && !name.isBlank()) {
            return name;
        }
        if (path != null && path.getFileName() != null) {
            String fileName = path.getFileName().toString();
            return fileName.endsWith(PROJECT_EXTENSION)
                    ? fileName.substring(0, fileName.length() - PROJECT_EXTENSION.length())
                    : fileName;
        }
        if (rawPath != null && !rawPath.isBlank()) {
            return rawPath;
        }
        return "(unnamed)";
    }

    /** Copies {@code raw}, sets one member (an existing member keeps its position) and keeps this entry's id. */
    private RecentEntry with(String key, JsonElement value) {
        JsonObject changed = raw.deepCopy();
        changed.add(key, value);
        return fromJson(id, changed);
    }

    /** The member as a string; {@code null} when it is absent, JSON {@code null} or not a string. */
    static String string(JsonObject object, String key) {
        JsonElement element = object.get(key);
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            return null;
        }
        return element.getAsString();
    }

    private static boolean bool(JsonObject object, String key) {
        JsonElement element = object.get(key);
        return element != null && element.isJsonPrimitive() && element.getAsJsonPrimitive().isBoolean()
                && element.getAsBoolean();
    }

    /** The absolute, normalized form of {@code text}; {@code null} when it is absent, relative or not a valid path. */
    public static Path absolutePath(String text) {
        if (text == null || text.isBlank()) {
            return null;
        }
        try {
            Path path = Path.of(text);
            return path.isAbsolute() ? path.normalize() : null;
        } catch (InvalidPathException e) {
            return null;
        }
    }

    private static Instant instant(String text) {
        if (text == null) {
            return null;
        }
        try {
            return Instant.parse(text);
        } catch (DateTimeParseException e) {
            return null;
        }
    }
}
