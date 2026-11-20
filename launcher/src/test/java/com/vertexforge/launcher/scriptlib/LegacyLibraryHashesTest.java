package com.vertexforge.launcher.scriptlib;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.template.LibraryManifest;
import org.junit.jupiter.api.Test;

import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedSet;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The legacy baseline the launcher carries, and its strict reader (VK-1691). */
class LegacyLibraryHashesTest {

    private static final String THROUGH = "9fdd282afb26bc403270a1366af7c482278a5447";
    private static final String H1 = "1".repeat(64);
    private static final String H2 = "2".repeat(64);

    /** A set over {@code known} (path to hashes), through a dummy commit. */
    static LegacyLibraryHashes hashes(Map<String, List<String>> known) {
        JsonObject files = new JsonObject();
        known.forEach((path, list) -> {
            JsonArray array = new JsonArray();
            list.forEach(array::add);
            files.add(path, array);
        });
        return LegacyLibraryHashes.parse(json(files.toString()));
    }

    private static String json(String files) {
        return "{\"formatVersion\": 1, \"hashAlgorithm\": \"SHA-256\", \"lineEndings\": \"lf\", \"through\": \""
                + "0".repeat(40) + "\", \"files\": " + files + "}";
    }

    private static void assertRefused(String json, String fragment) {
        IllegalStateException e = assertThrows(IllegalStateException.class, () -> LegacyLibraryHashes.parse(json));
        assertTrue(e.getMessage().contains(fragment), "\"" + e.getMessage() + "\" should say " + fragment);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The bundled set
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theBundledSetLoadsOnceAndRunsThroughTheTrackingCommit() {
        LegacyLibraryHashes bundled = LegacyLibraryHashes.bundled();

        assertSame(bundled, LegacyLibraryHashes.bundled(), "read once");
        assertEquals(THROUGH, bundled.through());
        assertEquals(333, bundled.files().size());
        assertEquals(573, bundled.pairCount());
    }

    @Test
    void theFirstLaunchersOneFolderTooDeepLibraryIsKnown() {
        Map<String, SortedSet<String>> files = LegacyLibraryHashes.bundled().files();
        List<String> deep = files.keySet().stream().filter(path -> path.startsWith("lib/")).toList();

        assertEquals(72, deep.size());
        assertEquals(72, deep.stream().mapToInt(path -> files.get(path).size()).sum(), "one version each");
        assertTrue(files.containsKey("lib/engine/Log.mt"));
        assertTrue(files.containsKey("engine/Log.mt"));
    }

    @Test
    void theBundledSetHasNoTwoPathsWindowsSeesAsOne() {
        Set<String> keys = new HashSet<>();
        for (String path : LegacyLibraryHashes.bundled().files().keySet()) {
            assertTrue(keys.add(LibraryManifest.pathKey(path)), path);
            assertTrue(LibraryManifest.isValidPath(path) && LibraryManifest.isLibraryFile(path), path);
        }
    }

    @Test
    void knownVersionsAreLookedUpByKey() {
        LegacyLibraryHashes bundled = LegacyLibraryHashes.bundled();

        Set<String> log = bundled.hashes(LibraryManifest.pathKey("engine/Log.mt"));
        assertFalse(log.isEmpty());
        assertEquals(bundled.files().get("engine/Log.mt"), log);
        assertEquals(log, bundled.hashes("ENGINE/LOG.MT"));
        assertTrue(bundled.hashes("engine/Log.mt").isEmpty(), "a path that is not a key finds nothing");
        assertTrue(bundled.hashes(LibraryManifest.pathKey("engine/NeverExisted.mt")).isEmpty());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The reader
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aValidSetReadsBack() {
        LegacyLibraryHashes set = hashes(Map.of("core/Array.mt", List.of(H2, H1), "Engine/Log.mt", List.of(H1)));

        assertEquals("0".repeat(40), set.through());
        assertEquals(List.of(H1, H2), List.copyOf(set.files().get("core/Array.mt")), "sorted");
        assertEquals(3, set.pairCount());
        assertEquals(Set.of(H1), set.hashes("ENGINE/LOG.MT"));
    }

    @Test
    void anUnknownMemberIsRefused() {
        assertRefused(json("{}").replaceFirst("\\}$", ", \"extra\": 1}"), "unknown member \"extra\"");
    }

    @Test
    void aMemberThatAppearsTwiceIsRefused() {
        assertRefused(json("{}").replaceFirst("\\}$", ", \"through\": \"" + THROUGH + "\"}"),
                "\"through\" appears twice");
    }

    @Test
    void theHeaderMembersMustBeExact() {
        assertRefused(json("{}").replace("\"formatVersion\": 1", "\"formatVersion\": 2"), "\"formatVersion\" is not 1");
        assertRefused(json("{}").replace("\"SHA-256\"", "\"MD5\""), "SHA-256 hashes over lf line endings");
        assertRefused(json("{}").replace("\"lf\"", "\"crlf\""), "SHA-256 hashes over lf line endings");
        assertRefused(json("{}").replace("0".repeat(40), "0".repeat(39)), "\"through\" is not a 40-digit commit");
        assertRefused(json("{}").replace("0".repeat(40), "A".repeat(40)), "\"through\" is not a 40-digit commit");
        assertRefused("{\"formatVersion\": 1, \"hashAlgorithm\": \"SHA-256\", \"lineEndings\": \"lf\", \"through\": \""
                + THROUGH + "\"}", "it has no \"files\"");
    }

    @Test
    void aBadHashIsRefused() {
        for (String hash : List.of("A".repeat(64), "1".repeat(63), "z".repeat(64), "")) {
            assertRefused(json("{\"a.mt\": [\"" + hash + "\"]}"), "\"a.mt\" has an invalid or repeated hash");
        }
    }

    @Test
    void aRepeatedHashIsRefused() {
        assertRefused(json("{\"a.mt\": [\"" + H1 + "\", \"" + H1 + "\"]}"), "\"a.mt\" has an invalid or repeated hash");
    }

    @Test
    void aPathWithoutHashesIsRefused() {
        assertRefused(json("{\"a.mt\": []}"), "\"a.mt\" has no hashes");
    }

    @Test
    void pathsThatDifferOnlyInCaseAreRefused() {
        assertRefused(json("{\"engine/Log.mt\": [\"" + H1 + "\"], \"ENGINE/log.mt\": [\"" + H2 + "\"]}"),
                "are the same path");
    }

    @Test
    void aPathThatIsNotALibraryFileIsRefused() {
        for (String path : List.of("../a.mt", "/a.mt", "a//b.mt", "a.mt.vfmeta", ".vflib.json", "C:/a.mt")) {
            assertRefused(json("{\"" + path + "\": [\"" + H1 + "\"]}"), "is not a library path");
        }
    }

    @Test
    void malformedJsonIsRefused() {
        assertRefused(json("{}") + " {}", "not valid JSON");
        assertRefused("{\"formatVersion\": 1,", "not valid JSON");
        assertThrows(IllegalStateException.class, () -> LegacyLibraryHashes.parse("[]"));
        assertThrows(IllegalStateException.class, () -> LegacyLibraryHashes.parse(json("{\"a.mt\": \"" + H1 + "\"}")),
                "a hash list must be an array");
    }
}
