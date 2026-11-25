package com.vertexforge.launcher.repository;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.repository.RecentProjectsCodec.Decoded;
import com.vertexforge.launcher.store.CorruptDocumentException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.time.Instant;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RecentProjectsCodecTest {

    private static final String T0 = "2026-01-01T00:00:00Z";

    @TempDir
    Path tempDir;

    private static JsonObject json(String text) {
        return JsonParser.parseString(text).getAsJsonObject();
    }

    private static String quote(Object value) {
        return new com.google.gson.JsonPrimitive(value.toString()).toString();
    }

    private String v1(Path... paths) {
        StringBuilder projects = new StringBuilder();
        for (Path path : paths) {
            if (!projects.isEmpty()) {
                projects.append(',');
            }
            projects.append("{\"name\":").append(quote(path.getFileName()))
                    .append(",\"path\":").append(quote(path))
                    .append(",\"lastOpened\":\"").append(T0).append("\"}");
        }
        return "{\"version\":1,\"projects\":[" + projects + "]}";
    }

    @Test
    void aVersionOneEntryGetsAnIdDerivedFromItsPath() {
        Path game = tempDir.resolve("Game").resolve("Game.vfproj");

        Decoded decoded = RecentProjectsCodec.decode(json(v1(game)));

        RecentEntry entry = decoded.projects().entries().getFirst();
        assertEquals(RecentProjectsCodec.nameUuid(RecentProjectsCodec.seedForPath(game.normalize())), entry.id());
        assertEquals("Game.vfproj", entry.name());
        assertEquals(game, entry.path());
        assertEquals(Instant.parse(T0), entry.lastOpened());
        assertFalse(entry.pinned());
        assertNull(entry.engineId());
    }

    @Test
    void derivedIdsAreTheSameOnEveryRead() {
        Path a = tempDir.resolve("A.vfproj");
        Path b = tempDir.resolve("B.vfproj");
        JsonObject root = json(v1(a, b));

        List<UUID> first = ids(RecentProjectsCodec.decode(root));
        List<UUID> second = ids(RecentProjectsCodec.decode(json(v1(a, b))));

        assertEquals(first, second);
        assertNotEquals(first.get(0), first.get(1));
    }

    @Test
    void theSamePathTwiceWithoutIdsGetsTwoIds() {
        Path game = tempDir.resolve("Game.vfproj");

        List<UUID> ids = ids(RecentProjectsCodec.decode(json(v1(game, game))));

        assertEquals(2, Set.copyOf(ids).size());
    }

    @Test
    void anExplicitIdIsKeptAndADuplicateIsReplaced() {
        UUID id = UUID.fromString("11111111-2222-3333-4444-555555555555");
        JsonObject root = json("""
                {"version": 2, "projects": [
                  {"id": "%s", "name": "First", "path": %s},
                  {"id": "%s", "name": "Second", "path": %s},
                  {"id": "not-a-uuid", "name": "Third", "path": %s},
                  {"id": "1-1-1-1-1", "name": "Fourth", "path": %s}]}
                """.formatted(id, quote(tempDir.resolve("1.vfproj")), id, quote(tempDir.resolve("2.vfproj")),
                quote(tempDir.resolve("3.vfproj")), quote(tempDir.resolve("4.vfproj"))));

        List<UUID> ids = ids(RecentProjectsCodec.decode(root));

        assertEquals(id, ids.get(0));
        assertEquals(4, Set.copyOf(ids).size());
        assertEquals(RecentProjectsCodec.nameUuid(RecentProjectsCodec.seedForPath(tempDir.resolve("2.vfproj"))),
                ids.get(1));
    }

    @Test
    void elementsThatAreNotObjectsAreSkippedAndCounted() {
        JsonObject root = json("{\"projects\": [null, 5, \"x\", [], {\"name\": \"Kept\"}]}");

        Decoded decoded = RecentProjectsCodec.decode(root);

        assertEquals(4, decoded.skippedElements());
        assertEquals(List.of("Kept"), decoded.projects().entries().stream().map(RecentEntry::name).toList());
    }

    @Test
    void entriesWithMissingOrInvalidFieldsAreKept() {
        JsonObject root = json("""
                {"projects": [
                  {"path": %s, "lastOpened": "yesterday"},
                  {"name": "Relative", "path": "projects/Relative/Relative.vfproj", "lastOpened": "%s"},
                  {"name": "NoPath", "lastOpened": "%s"},
                  {"name": null, "path": null, "lastOpened": null, "pinned": "yes"}]}
                """.formatted(quote(tempDir.resolve("Unnamed").resolve("Unnamed.vfproj")), T0, T0));

        List<RecentEntry> entries = RecentProjectsCodec.decode(root).projects().entries();

        assertEquals(4, entries.size());
        assertNull(entries.get(0).name());
        assertEquals("Unnamed", entries.get(0).displayName());
        assertNull(entries.get(0).lastOpened());
        assertNull(entries.get(1).path(), "a relative path is kept as text only");
        assertEquals("projects/Relative/Relative.vfproj", entries.get(1).rawPath());
        assertNull(entries.get(2).rawPath());
        assertNull(entries.get(3).name());
        assertFalse(entries.get(3).pinned());
        assertEquals(4, entries.stream().map(RecentEntry::id).collect(Collectors.toSet()).size());
    }

    @Test
    void encodeKeepsUnknownMembersAndTheirOrder() {
        JsonObject root = json("""
                {"version": 1, "before": true, "projects": [
                  {"name": "Old", "path": %s, "lastOpened": "%s", "future": {"x": 1}, "note": null}],
                 "after": [1, 2]}
                """.formatted(quote(tempDir.resolve("Old.vfproj")), T0));
        RecentProjects projects = RecentProjectsCodec.decode(root).projects();

        JsonObject encoded = RecentProjectsCodec.encode(projects, root);

        assertEquals(List.of("version", "before", "projects", "after"), List.copyOf(encoded.keySet()));
        JsonObject entry = encoded.getAsJsonArray("projects").get(0).getAsJsonObject();
        assertEquals(List.of("id", "name", "path", "lastOpened", "future", "note", "pinned", "engineId"),
                List.copyOf(entry.keySet()));
        assertEquals(json("{\"x\": 1}"), entry.get("future"));
        assertTrue(entry.get("note").isJsonNull());
        assertTrue(entry.get("engineId").isJsonNull());
        assertFalse(entry.get("pinned").getAsBoolean());
    }

    @Test
    void anEntryThatNeedsNothingIsWrittenBackExactlyAsRead() {
        JsonObject stored = json("""
                {"pinned": true, "id": "11111111-2222-3333-4444-555555555555", "name": "Game", "path": %s,
                 "lastOpened": "%s", "engineId": "dev", "custom": [1]}
                """.formatted(quote(tempDir.resolve("Game.vfproj")), T0));
        JsonArray projects = new JsonArray();
        projects.add(stored);
        JsonObject root = new JsonObject();
        root.addProperty("version", 2);
        root.add("projects", projects);

        JsonObject encoded = RecentProjectsCodec.encode(RecentProjectsCodec.decode(root).projects(), root);

        JsonObject entry = encoded.getAsJsonArray("projects").get(0).getAsJsonObject();
        assertEquals(stored, entry);
        assertEquals(List.copyOf(stored.keySet()), List.copyOf(entry.keySet()));
    }

    @Test
    void aNewEntryHasAllSixMembersInSchemaOrder() {
        UUID id = UUID.randomUUID();
        RecentProjects projects = RecentProjects.empty()
                .add(tempDir.resolve("New.vfproj"), "New", Instant.parse(T0), id).projects();

        JsonObject encoded = RecentProjectsCodec.encode(projects, new JsonObject());

        JsonObject entry = encoded.getAsJsonArray("projects").get(0).getAsJsonObject();
        assertEquals(List.of("id", "name", "path", "lastOpened", "pinned", "engineId"), List.copyOf(entry.keySet()));
        assertEquals(id.toString(), entry.get("id").getAsString());
        assertEquals(tempDir.resolve("New.vfproj").toString(), entry.get("path").getAsString());
    }

    @Test
    void aProjectsMemberThatIsNotAListIsCorruptForKnownVersions() {
        assertThrows(CorruptDocumentException.class, () -> RecentProjectsCodec.checkStructure(json("{\"projects\": 5}")));
        assertDoesNotThrow(() -> RecentProjectsCodec.checkStructure(json("{}")));
        assertDoesNotThrow(() -> RecentProjectsCodec.checkStructure(json("{\"projects\": null}")));
        // A newer file with another shape is still decoded, as an empty list.
        assertTrue(RecentProjectsCodec.decode(json("{\"version\": 9, \"projects\": {\"a\": 1}}")).projects().isEmpty());
    }

    private static List<UUID> ids(Decoded decoded) {
        return decoded.projects().entries().stream().map(RecentEntry::id).toList();
    }
}
