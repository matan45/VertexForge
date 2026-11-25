package com.vertexforge.launcher.engine;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.store.CorruptDocumentException;
import com.vertexforge.launcher.store.DocumentFormat;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.Set;
import java.util.UUID;

/**
 * Turns {@code engines.json} into an {@link EngineRegistry} and back (VK-1681).
 * <pre>
 * { "version": 1,
 *   "defaultEngineId": "&lt;uuid&gt;" or null,
 *   "engines": [ { "id": "&lt;uuid&gt;", "path": "C:\\VertexForge\\bin\\Editor\\Release\\x64\\Editor.exe" } ] }
 * </pre>
 * A file without {@code version} (written by hand) is read as version 1. As for the recent projects, nothing is ever
 * dropped for a missing or invalid field, and members this build does not know are written back unchanged.
 */
public final class EngineRegistryCodec {

    public static final int CURRENT_VERSION = 1;
    public static final String ENGINES = "engines";
    public static final String DEFAULT_ENGINE_ID = "defaultEngineId";
    public static final DocumentFormat FORMAT =
            new DocumentFormat(CURRENT_VERSION, OptionalInt.of(1), EngineRegistryCodec::checkStructure);

    private static final String ID_SEED_PREFIX = "vf-engine:";

    private EngineRegistryCodec() {
        // Utility class - prevent instantiation
    }

    /** A known-version document is corrupt when {@code engines} is present but not a list. */
    static void checkStructure(JsonObject root) throws CorruptDocumentException {
        JsonElement engines = root.get(ENGINES);
        if (engines != null && !engines.isJsonNull() && !engines.isJsonArray()) {
            throw new CorruptDocumentException("its \"" + ENGINES + "\" member is not a list");
        }
    }

    /**
     * @param registry        the installs, in file order, and the default
     * @param skippedElements how many elements of {@code engines} were not JSON objects (they carry no engine and are
     *                        not written back)
     */
    public record Decoded(EngineRegistry registry, int skippedElements) {
    }

    /**
     * Reads every object in {@code engines}. An entry whose {@code id} is missing, malformed or already taken gets an
     * id derived from its content ({@code "vf-engine:" + normalized path}, or the entry's JSON when it has no path,
     * plus {@code "#n"} while that collides), so every read of the same file agrees on the ids. A
     * {@code defaultEngineId} that is not a UUID reads as no default.
     */
    public static Decoded decode(JsonObject root) {
        UUID defaultEngineId = defaultEngineId(root).orElse(null);
        JsonElement engines = root.get(ENGINES);
        if (engines == null || !engines.isJsonArray()) {
            return new Decoded(new EngineRegistry(List.of(), defaultEngineId), 0);
        }

        List<JsonObject> objects = new ArrayList<>();
        int skipped = 0;
        for (JsonElement element : engines.getAsJsonArray()) {
            if (element.isJsonObject()) {
                objects.add(element.getAsJsonObject());
            } else {
                skipped++;
            }
        }

        // First pass: a valid explicit id belongs to the first entry that carries it.
        Set<UUID> used = new HashSet<>();
        UUID[] ids = new UUID[objects.size()];
        for (int i = 0; i < objects.size(); i++) {
            Optional<UUID> explicit = uuid(EngineInstall.string(objects.get(i), EngineInstall.ID));
            if (explicit.isPresent() && used.add(explicit.get())) {
                ids[i] = explicit.get();
            }
        }
        // Second pass: everything else gets a content-derived id that no other entry uses.
        for (int i = 0; i < objects.size(); i++) {
            if (ids[i] != null) {
                continue;
            }
            String seed = seed(objects.get(i));
            UUID candidate = nameUuid(seed);
            for (int n = 1; used.contains(candidate); n++) {
                candidate = nameUuid(seed + "#" + n);
            }
            used.add(candidate);
            ids[i] = candidate;
        }

        List<EngineInstall> installs = new ArrayList<>(objects.size());
        for (int i = 0; i < objects.size(); i++) {
            installs.add(EngineInstall.fromJson(ids[i], objects.get(i)));
        }
        return new Decoded(new EngineRegistry(installs, defaultEngineId), skipped);
    }

    /**
     * Writes {@code registry} into a copy of {@code baseRoot}: every other top-level member stays as it was and where
     * it was. {@code defaultEngineId} keeps its stored form while its meaning is unchanged (so a value this build
     * cannot read survives until the default is changed); a document without it gets it. Each entry is written from
     * its stored JSON; only an entry whose stored {@code id} differs from its resolved one is rebuilt, with {@code id}
     * first.
     */
    public static JsonObject encode(EngineRegistry registry, JsonObject baseRoot) {
        JsonArray array = new JsonArray();
        for (EngineInstall engine : registry.engines()) {
            array.add(encodeEntry(engine));
        }

        boolean defaultUnchanged = Objects.equals(defaultEngineId(baseRoot), registry.defaultEngineId());
        JsonElement defaultValue = registry.defaultEngineId()
                .<JsonElement>map(id -> new JsonPrimitive(id.toString()))
                .orElse(JsonNull.INSTANCE);

        JsonObject root = new JsonObject();
        boolean placedDefault = false;
        boolean placedEngines = false;
        for (Map.Entry<String, JsonElement> member : baseRoot.entrySet()) {
            switch (member.getKey()) {
                case DEFAULT_ENGINE_ID -> {
                    root.add(DEFAULT_ENGINE_ID, defaultUnchanged ? member.getValue().deepCopy() : defaultValue);
                    placedDefault = true;
                }
                case ENGINES -> {
                    root.add(ENGINES, array);
                    placedEngines = true;
                }
                default -> root.add(member.getKey(), member.getValue().deepCopy());
            }
        }
        if (!placedDefault) {
            root.add(DEFAULT_ENGINE_ID, defaultValue);
        }
        if (!placedEngines) {
            root.add(ENGINES, array);
        }
        return root;
    }

    private static JsonObject encodeEntry(EngineInstall engine) {
        JsonObject raw = engine.raw();
        String id = engine.id().toString();
        if (id.equals(EngineInstall.string(raw, EngineInstall.ID))) {
            return raw;
        }
        JsonObject out = new JsonObject();
        out.addProperty(EngineInstall.ID, id);
        for (Map.Entry<String, JsonElement> member : raw.entrySet()) {
            if (!member.getKey().equals(EngineInstall.ID)) {
                out.add(member.getKey(), member.getValue());
            }
        }
        return out;
    }

    private static Optional<UUID> defaultEngineId(JsonObject root) {
        return uuid(EngineInstall.string(root, DEFAULT_ENGINE_ID));
    }

    /** The UUID in {@code text}, only in its canonical form ({@code UUID.fromString} also accepts "1-1-1-1-1"). */
    public static Optional<UUID> uuid(String text) {
        if (text == null) {
            return Optional.empty();
        }
        try {
            UUID id = UUID.fromString(text);
            return id.toString().equalsIgnoreCase(text) ? Optional.of(id) : Optional.empty();
        } catch (IllegalArgumentException e) {
            return Optional.empty();
        }
    }

    private static String seed(JsonObject raw) {
        String rawPath = EngineInstall.string(raw, EngineInstall.PATH);
        if (rawPath != null) {
            Path path = RecentEntry.absolutePath(rawPath);
            return ID_SEED_PREFIX + (path != null ? path.toString() : rawPath);
        }
        return ID_SEED_PREFIX + raw;
    }

    /** The id derived from a seed; public so tests can predict a repaired entry's id. */
    public static UUID nameUuid(String seed) {
        return UUID.nameUUIDFromBytes(seed.getBytes(StandardCharsets.UTF_8));
    }

    /** The seed of an entry stored with this path and no usable id (before any {@code "#n"} suffix). */
    public static String seedForPath(Path normalizedAbsolutePath) {
        return ID_SEED_PREFIX + normalizedAbsolutePath;
    }
}
