package com.vertexforge.launcher.repository;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.store.CorruptDocumentException;
import com.vertexforge.launcher.store.DocumentFormat;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.Set;
import java.util.UUID;

/**
 * Turns the recent-projects document into {@link RecentProjects} and back (VK-1678).
 * <pre>
 * { "version": 2,
 *   "projects": [ { "id": "&lt;uuid&gt;", "name": "Game", "path": "C:\\Projects\\Game\\Game.vfproj",
 *                   "lastOpened": "2026-01-02T03:04:05Z", "pinned": false, "engineId": null } ] }
 * </pre>
 * Version 1 files (no {@code version} member, or {@code 1}; entries without {@code id}, {@code pinned} or
 * {@code engineId}) are read the same way and written as version 2.
 */
public final class RecentProjectsCodec {

    public static final int CURRENT_VERSION = 2;
    public static final String PROJECTS = "projects";
    public static final DocumentFormat FORMAT =
            new DocumentFormat(CURRENT_VERSION, OptionalInt.of(1), RecentProjectsCodec::checkStructure);

    private static final String ID_SEED_PREFIX = "vf-recent:";

    private RecentProjectsCodec() {
        // Utility class - prevent instantiation
    }

    /** A known-version document is corrupt when {@code projects} is present but not a list. */
    static void checkStructure(JsonObject root) throws CorruptDocumentException {
        JsonElement projects = root.get(PROJECTS);
        if (projects != null && !projects.isJsonNull() && !projects.isJsonArray()) {
            throw new CorruptDocumentException("its \"projects\" member is not a list");
        }
    }

    /**
     * @param projects        the entries, in file order
     * @param skippedElements how many elements of {@code projects} were not JSON objects (they carry no project and
     *                        are not written back)
     */
    public record Decoded(RecentProjects projects, int skippedElements) {
    }

    /**
     * Reads every object in {@code projects}; nothing is dropped for a missing or invalid field. This is also used
     * for display on a newer file, where an unknown shape simply yields an empty list.
     * <p>
     * An entry whose {@code id} is missing, malformed or already taken gets an id derived from its content
     * ({@code "vf-recent:" + normalized path}, or the entry's JSON when it has no path, plus {@code "#n"} while that
     * collides). Ids therefore depend only on the file, so every read of the same file (by any process) and the file
     * that is finally written all agree on them.
     */
    public static Decoded decode(JsonObject root) {
        JsonElement projects = root.get(PROJECTS);
        if (projects == null || !projects.isJsonArray()) {
            return new Decoded(RecentProjects.empty(), 0);
        }

        List<JsonObject> objects = new ArrayList<>();
        int skipped = 0;
        for (JsonElement element : projects.getAsJsonArray()) {
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
            Optional<UUID> explicit = explicitId(objects.get(i));
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

        List<RecentEntry> entries = new ArrayList<>(objects.size());
        for (int i = 0; i < objects.size(); i++) {
            entries.add(RecentEntry.fromJson(ids[i], objects.get(i)));
        }
        return new Decoded(new RecentProjects(entries), skipped);
    }

    /**
     * Writes {@code projects} into a copy of {@code baseRoot}: every other top-level member stays as it was and where
     * it was. Each entry is written from its stored JSON; only an entry whose stored {@code id} differs from its
     * resolved one is rebuilt (with {@code id} first), and missing {@code pinned} / {@code engineId} members are
     * added. An entry that needs neither is written back exactly as it was read.
     */
    public static JsonObject encode(RecentProjects projects, JsonObject baseRoot) {
        JsonArray array = new JsonArray();
        for (RecentEntry entry : projects.entries()) {
            array.add(encodeEntry(entry));
        }

        JsonObject root = new JsonObject();
        boolean placed = false;
        for (Map.Entry<String, JsonElement> member : baseRoot.entrySet()) {
            if (member.getKey().equals(PROJECTS)) {
                root.add(PROJECTS, array);
                placed = true;
            } else {
                root.add(member.getKey(), member.getValue().deepCopy());
            }
        }
        if (!placed) {
            root.add(PROJECTS, array);
        }
        return root;
    }

    private static JsonObject encodeEntry(RecentEntry entry) {
        JsonObject raw = entry.raw();
        String id = entry.id().toString();
        JsonObject out;
        if (id.equals(stringMember(raw, RecentEntry.ID))) {
            out = raw;
        } else {
            out = new JsonObject();
            out.addProperty(RecentEntry.ID, id);
            for (Map.Entry<String, JsonElement> member : raw.entrySet()) {
                if (!member.getKey().equals(RecentEntry.ID)) {
                    out.add(member.getKey(), member.getValue());
                }
            }
        }
        if (!out.has(RecentEntry.PINNED)) {
            out.addProperty(RecentEntry.PINNED, false);
        }
        if (!out.has(RecentEntry.ENGINE_ID)) {
            out.add(RecentEntry.ENGINE_ID, JsonNull.INSTANCE);
        }
        return out;
    }

    private static Optional<UUID> explicitId(JsonObject raw) {
        String text = stringMember(raw, RecentEntry.ID);
        if (text == null) {
            return Optional.empty();
        }
        try {
            UUID id = UUID.fromString(text);
            // UUID.fromString also accepts short forms such as "1-1-1-1-1"; only the canonical form counts.
            return id.toString().equalsIgnoreCase(text) ? Optional.of(id) : Optional.empty();
        } catch (IllegalArgumentException e) {
            return Optional.empty();
        }
    }

    private static String seed(JsonObject raw) {
        String rawPath = stringMember(raw, RecentEntry.PATH);
        if (rawPath != null) {
            Path path = RecentEntry.absolutePath(rawPath);
            return ID_SEED_PREFIX + (path != null ? path.toString() : rawPath);
        }
        return ID_SEED_PREFIX + raw;
    }

    /** The id derived from a seed; public so tests and tools can predict a migrated entry's id. */
    public static UUID nameUuid(String seed) {
        return UUID.nameUUIDFromBytes(seed.getBytes(StandardCharsets.UTF_8));
    }

    /** The seed of an entry stored with this path and no usable id (before any {@code "#n"} suffix). */
    public static String seedForPath(Path normalizedAbsolutePath) {
        return ID_SEED_PREFIX + normalizedAbsolutePath;
    }

    private static String stringMember(JsonObject object, String key) {
        JsonElement element = object.get(key);
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            return null;
        }
        return element.getAsString();
    }
}
