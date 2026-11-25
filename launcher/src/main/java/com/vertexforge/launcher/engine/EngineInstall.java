package com.vertexforge.launcher.engine;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.model.RecentEntry;

import java.nio.file.Path;
import java.util.Objects;
import java.util.UUID;

/**
 * One registered engine install in {@code engines.json} (VK-1681).
 * <p>
 * As for the recent projects, {@code raw} is the entry's JSON object exactly as stored and the source of truth:
 * members this build does not know are written back unchanged, and an invalid path never drops the entry (it then
 * reads as {@code null} and the engine checks as Broken, so that it can still be removed).
 *
 * @param id         the install's identity; projects bind to it
 * @param rawPath    the stored path text, or {@code null}
 * @param executable {@code rawPath} as an absolute, normalized path to the editor; {@code null} when it is absent,
 *                   relative or invalid
 * @param raw        the stored JSON object; copied in and out
 */
public record EngineInstall(UUID id, String rawPath, Path executable, JsonObject raw) {

    public static final String ID = "id";
    public static final String PATH = "path";

    public EngineInstall {
        Objects.requireNonNull(id, "id");
        raw = Objects.requireNonNull(raw, "raw").deepCopy();
    }

    @Override
    public JsonObject raw() {
        return raw.deepCopy();
    }

    /** Reads the typed fields of a stored entry; {@code id} is resolved by the caller (the codec). */
    public static EngineInstall fromJson(UUID id, JsonObject raw) {
        String rawPath = string(raw, PATH);
        return new EngineInstall(id, rawPath, RecentEntry.absolutePath(rawPath), raw);
    }

    /** A new entry for the editor at {@code executable} (its real path). */
    public static EngineInstall create(UUID id, Path executable) {
        Objects.requireNonNull(executable, "executable");
        JsonObject raw = new JsonObject();
        raw.addProperty(ID, id.toString());
        raw.addProperty(PATH, executable.toString());
        return fromJson(id, raw);
    }

    /** The member as a string; {@code null} when it is absent, JSON {@code null} or not a string. */
    static String string(JsonObject object, String key) {
        JsonElement element = object.get(key);
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            return null;
        }
        return element.getAsString();
    }
}
