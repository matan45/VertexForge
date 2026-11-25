package com.vertexforge.launcher.template;

import com.google.gson.JsonObject;
import com.vertexforge.launcher.template.LibraryManifest.InvalidLibraryManifestException;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link LibraryManifest#parse} and the path rules it shares with the script-library update (VK-1691). */
class LibraryManifestParseTest {

    private static final String H1 = "1".repeat(64);
    private static final String H2 = "a".repeat(64);
    private static final String H3 = "0123456789abcdef".repeat(4);

    private static byte[] utf8(String text) {
        return text.getBytes(StandardCharsets.UTF_8);
    }

    /** The members of a valid record, in order, as raw JSON values; tests replace or drop one. */
    private static Map<String, String> validMembers() {
        Map<String, String> members = new LinkedHashMap<>();
        members.put("formatVersion", "1");
        members.put("engineVersion", "\"1.2.3\"");
        members.put("hashAlgorithm", "\"SHA-256\"");
        members.put("lineEndings", "\"lf\"");
        members.put("files", "{\"core/Array.mt\": \"" + H1 + "\"}");
        return members;
    }

    private static String object(Map<String, String> members) {
        StringBuilder text = new StringBuilder("{");
        members.forEach((name, value) -> {
            if (text.length() > 1) {
                text.append(", ");
            }
            text.append('"').append(name).append("\": ").append(value);
        });
        return text.append('}').toString();
    }

    /** A record whose {@code files} object has exactly one entry, built by Gson so any key is escaped correctly. */
    private static String withOneFile(String path, String hash) {
        JsonObject files = new JsonObject();
        files.addProperty(path, hash);
        Map<String, String> members = validMembers();
        members.put("files", files.toString());
        return object(members);
    }

    private static InvalidLibraryManifestException refused(String json) {
        return refused(utf8(json));
    }

    private static InvalidLibraryManifestException refused(byte[] bytes) {
        return assertThrows(InvalidLibraryManifestException.class, () -> LibraryManifest.parse(bytes));
    }

    private static void assertRefusedSaying(String json, String fragment) {
        InvalidLibraryManifestException e = refused(json);
        assertFalse(e.newerFormat(), e.getMessage());
        assertTrue(e.getMessage().contains(fragment), "\"" + e.getMessage() + "\" should say " + fragment);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // What is read
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void whatToJsonWritesParsesBackToTheSameRecord() throws Exception {
        Map<String, String> files = new LinkedHashMap<>();
        files.put("engine/Log.mt", H2);
        files.put("core/Array.mt", H1);
        files.put("engine/oop/Behaviour.mt", H3);

        LibraryManifest.Contents contents = LibraryManifest.parse(LibraryManifest.toJson("1.2.3", files));

        assertEquals("1.2.3", contents.engineVersion());
        assertEquals(new TreeMap<>(files), contents.files());
        assertEquals(List.of("core/Array.mt", "engine/Log.mt", "engine/oop/Behaviour.mt"),
                List.copyOf(contents.files().keySet()), "sorted");
    }

    @Test
    void anEmptyFileListIsARecordOfAnEmptyLibrary() throws Exception {
        LibraryManifest.Contents contents = LibraryManifest.parse(LibraryManifest.toJson("1.0.0", Map.of()));

        assertTrue(contents.files().isEmpty());
    }

    @Test
    void aByteOrderMarkIsAccepted() throws Exception {
        byte[] json = LibraryManifest.toJson("1.0.0", Map.of("core/Array.mt", H1));
        byte[] withBom = new byte[json.length + 3];
        withBom[0] = (byte) 0xEF;
        withBom[1] = (byte) 0xBB;
        withBom[2] = (byte) 0xBF;
        System.arraycopy(json, 0, withBom, 3, json.length);

        assertEquals(Map.of("core/Array.mt", H1), LibraryManifest.parse(withBom).files());
    }

    @Test
    void crlfLineEndingsBetweenMembersAreFine() throws Exception {
        String json = new String(LibraryManifest.toJson("1.0.0", Map.of("core/Array.mt", H1)),
                StandardCharsets.UTF_8).replace("\n", "\r\n");

        assertEquals(Map.of("core/Array.mt", H1), LibraryManifest.parse(utf8(json)).files());
    }

    @Test
    void unknownMembersAreIgnored() throws Exception {
        Map<String, String> members = validMembers();
        members.put("comment", "{\"nested\": [1, 2, {\"x\": null}]}");
        members.put("generatedBy", "\"a newer minor launcher\"");

        assertEquals(Map.of("core/Array.mt", H1), LibraryManifest.parse(utf8(object(members))).files());
    }

    @Test
    void sidecarAndManifestEntriesAreSkipped() throws Exception {
        String json = object(Map.of("formatVersion", "1", "engineVersion", "\"1.0.0\"", "hashAlgorithm",
                "\"SHA-256\"", "lineEndings", "\"lf\"", "files", "{\"engine/Log.mt\": \"" + H1
                        + "\", \"engine/Log.mt.vfmeta\": \"" + H2 + "\", \"engine/Other.VFMETA\": \"" + H2
                        + "\", \".vflib.json\": \"" + H3 + "\"}"));

        assertEquals(Map.of("engine/Log.mt", H1), LibraryManifest.parse(utf8(json)).files());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The document
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void bytesThatAreNotUtf8AreRefused() {
        InvalidLibraryManifestException e = refused(new byte[]{'{', (byte) 0xFF, '}'});

        assertFalse(e.newerFormat());
        assertTrue(e.getMessage().contains("not UTF-8"), e.getMessage());
    }

    @Test
    void aTopLevelThatIsNotAnObjectIsRefused() {
        assertRefusedSaying("[1, 2]", "not a JSON object");
        assertRefusedSaying("\"text\"", "not a JSON object");
        assertRefusedSaying("1", "not a JSON object");
    }

    @Test
    void emptyOrMalformedTextIsRefused() {
        assertRefusedSaying("", "not valid JSON");
        assertRefusedSaying("   ", "not valid JSON");
        assertRefusedSaying("{\"formatVersion\": 1,", "not valid JSON");
        // Lenient JSON is not taken: a comment, unquoted names, single quotes.
        assertRefusedSaying("{/* c */ \"formatVersion\": 1}", "not valid JSON");
        assertRefusedSaying("{formatVersion: 1}", "not valid JSON");
        assertRefusedSaying("{'formatVersion': 1}", "not valid JSON");
    }

    @Test
    void dataAfterTheObjectIsRefused() {
        String valid = object(validMembers());
        for (String trailing : List.of(" {}", "x", "\n[]", ",")) {
            InvalidLibraryManifestException e = refused(valid + trailing);
            assertFalse(e.newerFormat(), e.getMessage());
            assertTrue(e.getMessage().contains("JSON"), trailing + ": " + e.getMessage());
        }
    }

    @Test
    void whitespaceAfterTheObjectIsFine() throws Exception {
        assertEquals(Map.of("core/Array.mt", H1),
                LibraryManifest.parse(utf8(object(validMembers()) + " \r\n\t\n")).files());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // formatVersion
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aNewerFormatIsReportedEvenWhenEveryOtherMemberIsBroken() {
        String json = "{\"formatVersion\": 2, \"engineVersion\": 5, \"hashAlgorithm\": \"MD5\", "
                + "\"lineEndings\": [\"crlf\"], \"files\": {\"../x\": \"NOTAHASH\", \"a\\\\b\": 3}, "
                + "\"engineVersion\": \"again\"}";

        InvalidLibraryManifestException e = refused(json);

        assertTrue(e.newerFormat(), e.getMessage());
        assertTrue(e.getMessage().contains("format version 2"), e.getMessage());
    }

    @Test
    void aNewerFormatIsReportedWhenMembersAreMissing() {
        InvalidLibraryManifestException e = refused("{\"formatVersion\": 7}");

        assertTrue(e.newerFormat());
        assertTrue(e.getMessage().contains("format version 7"), e.getMessage());
    }

    @Test
    void aNewerFormatStatedTwiceIsStillNewer() {
        // The first value counts; the repeat is a problem only a reader of this format would report.
        InvalidLibraryManifestException e = refused("{\"formatVersion\": 3, \"formatVersion\": 1}");

        assertTrue(e.newerFormat(), e.getMessage());
    }

    @Test
    void aMissingOrMalformedFormatVersionIsRefusedAsInvalid() {
        Map<String, String> members = validMembers();
        members.remove("formatVersion");
        assertRefusedSaying(object(members), "has no \"formatVersion\"");

        for (String value : List.of("\"1\"", "0", "-1", "1.0", "1e0", "null", "true", "[1]", "1234567890")) {
            members = validMembers();
            members.put("formatVersion", value);
            assertRefusedSaying(object(members), "\"formatVersion\" is not a whole number");
        }
        // A leading zero is not JSON at all.
        members = validMembers();
        members.put("formatVersion", "01");
        assertRefusedSaying(object(members), "not valid JSON");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The other members
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theEngineVersionMustBeMajorMinorPatch() {
        Map<String, String> members = validMembers();
        members.remove("engineVersion");
        assertRefusedSaying(object(members), "\"engineVersion\" is missing");

        for (String value : List.of("\"1.0\"", "\"VertexForge 1.0.0\"", "\"v1.0.0\"", "\"1.0.0-beta\"", "\"\"",
                "100", "null", "{}")) {
            members = validMembers();
            members.put("engineVersion", value);
            assertRefusedSaying(object(members), "\"engineVersion\" is missing or not a MAJOR.MINOR.PATCH version");
        }
    }

    @Test
    void theHashAlgorithmMustBeSha256() {
        Map<String, String> members = validMembers();
        members.remove("hashAlgorithm");
        assertRefusedSaying(object(members), "\"hashAlgorithm\" is not \"SHA-256\"");

        for (String value : List.of("\"sha-256\"", "\"SHA256\"", "\"MD5\"", "256")) {
            members = validMembers();
            members.put("hashAlgorithm", value);
            assertRefusedSaying(object(members), "\"hashAlgorithm\" is not \"SHA-256\"");
        }
    }

    @Test
    void theLineEndingsMustBeLf() {
        Map<String, String> members = validMembers();
        members.remove("lineEndings");
        assertRefusedSaying(object(members), "\"lineEndings\" is not \"lf\"");

        for (String value : List.of("\"LF\"", "\"crlf\"", "\"\"", "false")) {
            members = validMembers();
            members.put("lineEndings", value);
            assertRefusedSaying(object(members), "\"lineEndings\" is not \"lf\"");
        }
    }

    @Test
    void theFilesMustBeAnObject() {
        Map<String, String> members = validMembers();
        members.remove("files");
        assertRefusedSaying(object(members), "it has no \"files\"");

        for (String value : List.of("[]", "\"core/Array.mt\"", "null", "1")) {
            members = validMembers();
            members.put("files", value);
            assertRefusedSaying(object(members), "\"files\" is not an object");
        }
    }

    @Test
    void aMemberThatAppearsTwiceIsRefused() {
        for (String member : List.of("engineVersion", "hashAlgorithm", "lineEndings", "files")) {
            String json = object(validMembers()).replaceFirst("\\}$", "")
                    + ", \"" + member + "\": " + validMembers().get(member) + "}";
            assertRefusedSaying(json, "\"" + member + "\" appears twice");
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The files
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aFileWithAnInvalidPathIsRefused() {
        for (String path : List.of("a\\b.mt", "..", "../x.mt", "a/../b.mt", "./a.mt", "a/./b.mt", "/a.mt", "a//b.mt",
                "a/", "C:/a.mt", "a.mt:stream", "")) {
            assertRefusedSaying(withOneFile(path, H1), "which is not a valid library path");
        }
    }

    @Test
    void aFileListedTwiceIsRefused() {
        String json = object(Map.of("formatVersion", "1", "engineVersion", "\"1.0.0\"", "hashAlgorithm",
                "\"SHA-256\"", "lineEndings", "\"lf\"", "files", "{\"engine/Log.mt\": \"" + H1
                        + "\", \"engine/Log.mt\": \"" + H1 + "\"}"));

        assertRefusedSaying(json, "it lists \"engine/Log.mt\" twice");
    }

    @Test
    void twoFilesThatDifferOnlyInCaseAreRefused() {
        String json = object(Map.of("formatVersion", "1", "engineVersion", "\"1.0.0\"", "hashAlgorithm",
                "\"SHA-256\"", "lineEndings", "\"lf\"", "files", "{\"engine/Log.mt\": \"" + H1
                        + "\", \"ENGINE/log.MT\": \"" + H2 + "\"}"));

        assertRefusedSaying(json, "both \"engine/Log.mt\" and \"ENGINE/log.MT\", which Windows sees as one file");
    }

    @Test
    void twoSidecarsThatDifferOnlyInCaseAreRefusedToo() {
        // The case rule is checked before an entry is known to be skipped.
        String json = object(Map.of("formatVersion", "1", "engineVersion", "\"1.0.0\"", "hashAlgorithm",
                "\"SHA-256\"", "lineEndings", "\"lf\"", "files", "{\"a.mt.vfmeta\": \"" + H1
                        + "\", \"A.MT.VFMETA\": \"" + H1 + "\"}"));

        assertRefusedSaying(json, "which Windows sees as one file");
    }

    @Test
    void aHashThatIsNot64LowercaseHexDigitsIsRefused() {
        for (String hash : List.of("A".repeat(64), "g".repeat(64), "1".repeat(63), "1".repeat(65), "",
                " " + "1".repeat(63))) {
            assertRefusedSaying(withOneFile("core/Array.mt", hash), "is not 64 lowercase hex digits");
        }
        Map<String, String> members = validMembers();
        members.put("files", "{\"core/Array.mt\": 12}");
        assertRefusedSaying(object(members), "is not 64 lowercase hex digits");
        members.put("files", "{\"core/Array.mt\": null}");
        assertRefusedSaying(object(members), "is not 64 lowercase hex digits");
        members.put("files", "{\"core/Array.mt\": [\"" + H1 + "\"]}");
        assertRefusedSaying(object(members), "is not 64 lowercase hex digits");
    }

    @Test
    void theFirstProblemOfTheFilesIsTheOneReported() {
        String json = object(Map.of("formatVersion", "1", "engineVersion", "\"1.0.0\"", "hashAlgorithm",
                "\"SHA-256\"", "lineEndings", "\"lf\"", "files", "{\"ok.mt\": \"" + H1
                        + "\", \"bad.mt\": \"nope\", \"../x.mt\": \"" + H1 + "\"}"));

        assertRefusedSaying(json, "the hash of \"bad.mt\"");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The path rules
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void onlyTheRootManifestAndSidecarsAreNotLibraryFiles() {
        assertTrue(LibraryManifest.isLibraryFile("engine/Log.mt"));
        assertTrue(LibraryManifest.isLibraryFile("README"));
        assertFalse(LibraryManifest.isLibraryFile(".vflib.json"));
        assertFalse(LibraryManifest.isLibraryFile(".VFLIB.JSON"));
        assertTrue(LibraryManifest.isLibraryFile("sub/.vflib.json"), "only the library's own record is left out");
        assertFalse(LibraryManifest.isLibraryFile("engine/Log.mt.vfmeta"));
        assertFalse(LibraryManifest.isLibraryFile("engine/Log.mt.VfMeta"));
    }

    @Test
    void aSidecarNeedsANameBeforeItsExtension() {
        assertTrue(LibraryManifest.isSidecar("a.vfmeta"));
        assertTrue(LibraryManifest.isSidecar("x/Log.mt.VFMETA"));
        assertFalse(LibraryManifest.isSidecar(".vfmeta"));
        assertFalse(LibraryManifest.isSidecar("vfmeta"));
        assertFalse(LibraryManifest.isSidecar("a.vfmeta.mt"));
        assertFalse(LibraryManifest.isSidecar("a.vfmet"));
    }

    @Test
    void thePathKeyUpperCasesEachCharacterWithoutChangingTheLength() {
        assertEquals("ENGINE/LOG.MT", LibraryManifest.pathKey("engine/Log.mt"));
        assertEquals(LibraryManifest.pathKey("Engine/LOG.mt"), LibraryManifest.pathKey("engine/log.MT"));
        // String.toUpperCase would turn the sharp s into "SS"; Windows compares names character by character.
        assertEquals(6, LibraryManifest.pathKey("straße").length());
        assertEquals("STRAßE", LibraryManifest.pathKey("straße"));
    }

    @Test
    void aValidPathIsRelativeAndSlashSeparated() {
        assertTrue(LibraryManifest.isValidPath("a.mt"));
        assertTrue(LibraryManifest.isValidPath("engine/oop/Behaviour.mt"));
        assertTrue(LibraryManifest.isValidPath(".hidden/x"));
        assertTrue(LibraryManifest.isValidPath("a..b/c"));
        assertFalse(LibraryManifest.isValidPath(null));
        assertFalse(LibraryManifest.isValidPath(""));
        assertFalse(LibraryManifest.isValidPath("a\\b"));
        assertFalse(LibraryManifest.isValidPath("C:x"));
        assertFalse(LibraryManifest.isValidPath("/a"));
        assertFalse(LibraryManifest.isValidPath("a/"));
        assertFalse(LibraryManifest.isValidPath("a//b"));
        assertFalse(LibraryManifest.isValidPath("."));
        assertFalse(LibraryManifest.isValidPath("a/.."));
    }
}
