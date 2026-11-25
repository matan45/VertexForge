package com.vertexforge.launcher.engine;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.vertexforge.launcher.store.CorruptDocumentException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** engines.json to {@link EngineRegistry} and back, and the registry's own operations (VK-1681). */
class EngineRegistryCodecTest {

    private static final String ID_A = "5b0c9d1e-7f0a-4d3b-9a51-2f6f1c0e8a42";
    private static final String ID_B = "c3a8e1f2-0b6d-4e5a-8f7c-1d2e3f4a5b6c";

    @TempDir
    Path tempDir;

    private String path(String name) {
        return tempDir.resolve(name).resolve("Editor.exe").toString();
    }

    private static JsonObject parse(String json) {
        return JsonParser.parseString(json).getAsJsonObject();
    }

    private JsonObject twoEngines() {
        JsonObject root = new JsonObject();
        root.addProperty("version", 1);
        root.addProperty("note", "kept");
        root.addProperty(EngineRegistryCodec.DEFAULT_ENGINE_ID, ID_B);
        JsonArray engines = new JsonArray();
        JsonObject a = new JsonObject();
        a.addProperty("id", ID_A);
        a.addProperty("path", path("a"));
        a.addProperty("future", 7);
        JsonObject b = new JsonObject();
        b.addProperty("id", ID_B);
        b.addProperty("path", path("b"));
        engines.add(a);
        engines.add(b);
        root.add(EngineRegistryCodec.ENGINES, engines);
        return root;
    }

    @Test
    void anEnginesListThatIsNotAListIsCorrupt() {
        assertThrows(CorruptDocumentException.class,
                () -> EngineRegistryCodec.checkStructure(parse("{\"engines\": {}}")));
        assertDoesNotThrow(() -> EngineRegistryCodec.checkStructure(parse("{\"engines\": null}")));
        assertDoesNotThrow(() -> EngineRegistryCodec.checkStructure(parse("{\"defaultEngineId\": 5}")));
        assertDoesNotThrow(() -> EngineRegistryCodec.checkStructure(new JsonObject()));
    }

    @Test
    void decodingReadsEveryEngineAndTheDefault() {
        EngineRegistryCodec.Decoded decoded = EngineRegistryCodec.decode(twoEngines());

        EngineRegistry registry = decoded.registry();
        assertEquals(List.of(UUID.fromString(ID_A), UUID.fromString(ID_B)),
                registry.engines().stream().map(EngineInstall::id).toList());
        assertEquals(Path.of(path("a")), registry.engines().getFirst().executable());
        assertEquals(Optional.of(UUID.fromString(ID_B)), registry.defaultEngineId());
        assertEquals(0, decoded.skippedElements());
    }

    @Test
    void anUnchangedRegistryEncodesToTheSameDocument() {
        JsonObject root = twoEngines();

        JsonObject encoded = EngineRegistryCodec.encode(EngineRegistryCodec.decode(root).registry(), root);

        assertEquals(root, encoded);
        assertEquals(List.of("version", "note", "defaultEngineId", "engines"), List.copyOf(encoded.keySet()));
    }

    @Test
    void nonObjectsAreSkippedAndBadEntriesKept() {
        JsonObject root = parse("{\"engines\": [null, 3, {\"path\": 5}, {\"id\": \"1-1-1-1-1\", \"path\": \"rel\"}]}");

        EngineRegistryCodec.Decoded decoded = EngineRegistryCodec.decode(root);

        assertEquals(2, decoded.skippedElements());
        assertEquals(2, decoded.registry().engines().size());
        for (EngineInstall install : decoded.registry().engines()) {
            assertNull(install.executable(), "an invalid or relative path is kept as an unusable entry");
        }
        JsonObject encoded = EngineRegistryCodec.encode(decoded.registry(), root);
        assertEquals(2, encoded.getAsJsonArray(EngineRegistryCodec.ENGINES).size(), "the skipped elements are dropped");
    }

    @Test
    void missingOrDuplicateIdsGetStableIdsDerivedFromThePath() {
        String json = "{\"engines\": [{\"id\": \"" + ID_A + "\", \"path\": " + quote(path("a")) + "},"
                + "{\"id\": \"" + ID_A + "\", \"path\": " + quote(path("b")) + "},"
                + "{\"path\": " + quote(path("c")) + "}]}";

        List<UUID> first = EngineRegistryCodec.decode(parse(json)).registry().engines().stream()
                .map(EngineInstall::id).toList();
        List<UUID> again = EngineRegistryCodec.decode(parse(json)).registry().engines().stream()
                .map(EngineInstall::id).toList();

        assertEquals(UUID.fromString(ID_A), first.get(0));
        assertEquals(EngineRegistryCodec.nameUuid(EngineRegistryCodec.seedForPath(Path.of(path("b")))), first.get(1));
        assertEquals(EngineRegistryCodec.nameUuid(EngineRegistryCodec.seedForPath(Path.of(path("c")))), first.get(2));
        assertEquals(first, again, "every read of the same file agrees on the ids");
    }

    @Test
    void anUnreadableDefaultIsKeptUntilTheDefaultChanges() {
        JsonObject root = parse("{\"defaultEngineId\": \"not-a-uuid\", \"engines\": [{\"id\": \"" + ID_A
                + "\", \"path\": " + quote(path("a")) + "}]}");
        EngineRegistry registry = EngineRegistryCodec.decode(root).registry();

        assertEquals(Optional.empty(), registry.defaultEngineId());
        assertEquals("not-a-uuid", EngineRegistryCodec.encode(registry, root)
                .get(EngineRegistryCodec.DEFAULT_ENGINE_ID).getAsString());
        assertEquals(ID_A, EngineRegistryCodec.encode(registry.withDefault(UUID.fromString(ID_A)), root)
                .get(EngineRegistryCodec.DEFAULT_ENGINE_ID).getAsString());
    }

    @Test
    void aNewDocumentGetsANullDefaultThenTheEngines() {
        EngineRegistry registry = EngineRegistry.empty().add(Path.of(path("a")), UUID.fromString(ID_A)).registry();

        JsonObject encoded = EngineRegistryCodec.encode(registry, new JsonObject());

        assertEquals(List.of("defaultEngineId", "engines"), List.copyOf(encoded.keySet()));
        assertTrue(encoded.get(EngineRegistryCodec.DEFAULT_ENGINE_ID).isJsonNull());
        JsonObject entry = encoded.getAsJsonArray(EngineRegistryCodec.ENGINES).get(0).getAsJsonObject();
        assertEquals(Map.of("id", ID_A, "path", path("a")),
                Map.of("id", entry.get("id").getAsString(), "path", entry.get("path").getAsString()));
    }

    // -----------------------------------------------------------------------------------------------------------
    // EngineRegistry
    // -----------------------------------------------------------------------------------------------------------

    @Test
    void addingAnEditorTwiceKeepsTheFirstEntry() {
        EngineRegistry.Added first = EngineRegistry.empty().add(Path.of(path("a")), UUID.fromString(ID_A));

        EngineRegistry.Added again = first.registry().add(Path.of(path("a")), UUID.fromString(ID_B));

        assertTrue(again.existed());
        assertSame(first.registry(), again.registry());
        assertEquals(UUID.fromString(ID_A), again.engine().id());
    }

    @Test
    void removingTheDefaultEngineClearsTheDefault() {
        EngineRegistry registry = EngineRegistryCodec.decode(twoEngines()).registry();

        EngineRegistry withoutB = registry.remove(UUID.fromString(ID_B));
        EngineRegistry withoutA = registry.remove(UUID.fromString(ID_A));

        assertEquals(Optional.empty(), withoutB.defaultEngineId());
        assertEquals(Optional.of(UUID.fromString(ID_B)), withoutA.defaultEngineId());
        assertSame(registry, registry.remove(UUID.randomUUID()));
    }

    @Test
    void theDefaultMustBeARegisteredEngine() {
        EngineRegistry registry = EngineRegistryCodec.decode(twoEngines()).registry();

        assertSame(registry, registry.withDefault(UUID.randomUUID()), "an engine removed meanwhile");
        assertSame(registry, registry.withDefault(UUID.fromString(ID_B)), "already the default");
        assertEquals(Optional.of(UUID.fromString(ID_A)),
                registry.withDefault(UUID.fromString(ID_A)).defaultEngineId());
        assertEquals(Optional.empty(), registry.withDefault(null).defaultEngineId());
        assertEquals(registry.engines(), registry.withDefault(null).engines());
    }

    private static String quote(String text) {
        return new JsonPrimitive(text).toString();
    }
}
