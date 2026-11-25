package com.vertexforge.launcher.scriptlib;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.scriptlib.UpdateJournal.InvalidJournalException;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.function.Consumer;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The note an update leaves while it swaps folders (VK-1691): its bytes, its strict reader and its folder names. */
class UpdateJournalTest {

    /** Where an update puts its backup, as the journal names it. */
    private static final String BACKUP = ".vertexforge/backups/scripts-lib-1.0.0-20260102-030405Z.zip";

    private static UpdateJournal journal(String backup) {
        return new UpdateJournal(1, "1a2b3c4d", "lib", "lib.new-1a2b3c4d", "lib.old-1a2b3c4d", true,
                "ab".repeat(32), backup, "1.1.0", "2026-01-02T03:04:05Z");
    }

    /** The JSON of a valid journal, changed by {@code edit}. */
    private static byte[] edited(Consumer<JsonObject> edit) {
        JsonObject json = JsonParser.parseString(new String(journal(BACKUP).toJson(), StandardCharsets.UTF_8))
                .getAsJsonObject();
        edit.accept(json);
        return json.toString().getBytes(StandardCharsets.UTF_8);
    }

    private static InvalidJournalException refused(byte[] bytes) {
        return assertThrows(InvalidJournalException.class, () -> UpdateJournal.parse(bytes));
    }

    private static void assertInvalid(byte[] bytes, String fragment) {
        InvalidJournalException e = refused(bytes);
        assertFalse(e.newerFormat(), e.getMessage());
        assertTrue(e.getMessage().contains(fragment), "\"" + e.getMessage() + "\" should say " + fragment);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Bytes
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aJournalReadsBackAsWritten() throws Exception {
        UpdateJournal journal = journal(".vertexforge/backups/scripts-lib-1.0.0-20260102-030405Z.zip");

        assertEquals(journal, UpdateJournal.parse(journal.toJson()));
    }

    @Test
    void aJournalWithoutABackupReadsBackWithoutOne() throws Exception {
        UpdateJournal journal = new UpdateJournal(1, "00000001", "lib", "lib.new-00000001", "lib.old-00000001", false,
                "0".repeat(64), null, "1.0.0", "2026-01-02T03:04:05Z");

        String text = new String(journal.toJson(), StandardCharsets.UTF_8);
        assertFalse(text.contains("backup"), text);
        UpdateJournal read = UpdateJournal.parse(journal.toJson());
        assertNull(read.backup());
        assertEquals(journal, read);
    }

    @Test
    void theBytesAreUtf8WithLfLineEndings() {
        String text = new String(journal("a/ü.zip").toJson(), StandardCharsets.UTF_8);

        assertFalse(text.contains("\r"));
        assertTrue(text.endsWith("}\n"));
        assertTrue(text.contains("a/ü.zip"), "no HTML escaping, no \\u escapes for letters");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The reader
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aNewerFormatIsReportedBeforeAnythingElse() {
        InvalidJournalException e = refused("{\"formatVersion\": 2}".getBytes(StandardCharsets.UTF_8));

        assertTrue(e.newerFormat());
        assertTrue(e.getMessage().contains("format version 2"), e.getMessage());
    }

    @Test
    void aFormatVersionWrittenAsTextIsAccepted() throws Exception {
        assertEquals(journal(BACKUP), UpdateJournal.parse(edited(json -> json.addProperty("formatVersion", "1"))));
    }

    @Test
    void aMissingOrMalformedFormatVersionIsInvalid() {
        assertInvalid(edited(json -> json.remove("formatVersion")), "\"formatVersion\" is missing");
        for (String value : List.of("0", "-1", "1.5", "one")) {
            assertInvalid(edited(json -> json.addProperty("formatVersion", value)), "\"formatVersion\" is missing");
        }
        assertInvalid(edited(json -> json.add("formatVersion", new JsonObject())), "\"formatVersion\" is missing");
    }

    @Test
    void everyMemberButTheBackupIsRequired() {
        for (String member : List.of("id", "library", "staging", "previous", "hadLibrary", "newManifestSha256",
                "engineVersion", "startedAt")) {
            assertInvalid(edited(json -> json.remove(member)), "a member is missing or has the wrong type");
            assertInvalid(edited(json -> json.addProperty(member, 7)), "a member is missing or has the wrong type");
        }
        assertInvalid(edited(json -> json.addProperty("hadLibrary", "true")), "a member is missing");
    }

    @Test
    void aBackupOfTheWrongTypeCountsAsNone() throws Exception {
        assertNull(UpdateJournal.parse(edited(json -> json.addProperty("backup", 1))).backup());
    }

    @Test
    void unknownMembersAreIgnored() throws Exception {
        assertEquals(journal(BACKUP), UpdateJournal.parse(edited(json -> json.addProperty("note", "hi"))));
    }

    @Test
    void aMemberThatAppearsTwiceIsInvalid() {
        String text = new String(journal(BACKUP).toJson(), StandardCharsets.UTF_8).replaceFirst("\\}\\s*$",
                ", \"library\": \"lib\"}");

        assertInvalid(text.getBytes(StandardCharsets.UTF_8), "\"library\" appears twice");
    }

    @Test
    void textThatIsNotOneJsonObjectIsInvalid() {
        assertInvalid("".getBytes(StandardCharsets.UTF_8), "not valid JSON");
        assertInvalid("[]".getBytes(StandardCharsets.UTF_8), "not valid JSON");
        assertInvalid((new String(journal(BACKUP).toJson(), StandardCharsets.UTF_8) + "{}")
                .getBytes(StandardCharsets.UTF_8), "JSON");
        assertInvalid("{\"formatVersion\": 1".getBytes(StandardCharsets.UTF_8), "not valid JSON");
    }

    @Test
    void theFolderNamesMustBeTheLibrarysOwnUpdateFolders() {
        String reason = "its folder names are not the library's own update folders";
        assertInvalid(edited(json -> json.addProperty("staging", "other.new-1a2b3c4d")), reason);
        assertInvalid(edited(json -> json.addProperty("staging", "lib.old-1a2b3c4d")), reason);
        assertInvalid(edited(json -> json.addProperty("previous", "lib.new-1a2b3c4d")), reason);
        assertInvalid(edited(json -> json.addProperty("staging", "lib.new-1a2b3c4")), reason);
        assertInvalid(edited(json -> json.addProperty("staging", "lib.new-1a2b3c4g")), reason);
        assertInvalid(edited(json -> json.addProperty("staging", "../lib.new-1a2b3c4d")), reason);
        assertInvalid(edited(json -> json.addProperty("previous", "lib.old-1a2b3c4d/..")), reason);
        assertInvalid(edited(json -> json.addProperty("library", "scripts/lib")), reason);
        assertInvalid(edited(json -> json.addProperty("library", "..")), reason);
        assertInvalid(edited(json -> json.addProperty("library", "C:")), reason);
    }

    @Test
    void theFolderNamesAreComparedIgnoringCase() throws Exception {
        UpdateJournal read = UpdateJournal.parse(edited(json -> {
            json.addProperty("library", "Lib");
            json.addProperty("staging", "LIB.NEW-1A2B3C4D");
            json.addProperty("previous", "lib.Old-1a2b3c4d");
        }));

        assertEquals("LIB.NEW-1A2B3C4D", read.staging());
    }

    @Test
    void theLibraryMustBeTheProjectsLibFolder() {
        String reason = "its folder names are not the library's own update folders";
        assertInvalid(edited(json -> {
            json.addProperty("library", "game");
            json.addProperty("staging", "game.new-1a2b3c4d");
            json.addProperty("previous", "game.old-1a2b3c4d");
        }), reason);
    }

    @Test
    void theBackupMustBeAZipOfTheUpdateInTheStateFolder() throws Exception {
        String reason = "\"backup\" is not a backup of the project's state folder";
        for (String backup : List.of("x.zip", "backups/scripts-lib-1.zip", ".vertexforge/scripts-lib-1.zip",
                ".vertexforge/backups/other.zip", ".vertexforge/backups/scripts-lib-1.tar",
                ".vertexforge/backups/sub/scripts-lib-1.zip", "../.vertexforge/backups/scripts-lib-1.zip",
                ".vertexforge/backups/../backups/scripts-lib-1.zip", "/.vertexforge/backups/scripts-lib-1.zip",
                "C:/x/.vertexforge/backups/scripts-lib-1.zip", ".vertexforge\\backups\\scripts-lib-1.zip",
                ".vertexforge/backups/scripts-lib-?.zip", ".vertexforge/backups/scripts-lib-\u0001.zip", "")) {
            assertInvalid(edited(json -> json.addProperty("backup", backup)), reason);
        }
        assertEquals(".VERTEXFORGE/Backups/SCRIPTS-LIB-x.ZIP", UpdateJournal.parse(edited(json ->
                json.addProperty("backup", ".VERTEXFORGE/Backups/SCRIPTS-LIB-x.ZIP"))).backup(), "ignoring case");
        assertTrue(UpdateJournal.isBackupPath(BACKUP));
        assertTrue(UpdateJournal.isBackupPath(".vertexforge/backups/scripts-lib-legacy-20260102-030405Z-2.zip"));
    }

    @Test
    void theManifestHashMustBe64LowercaseHexDigits() {
        String reason = "\"newManifestSha256\" is not 64 lowercase hex digits";
        assertInvalid(edited(json -> json.addProperty("newManifestSha256", "AB".repeat(32))), reason);
        assertInvalid(edited(json -> json.addProperty("newManifestSha256", "ab".repeat(31))), reason);
    }

    @Test
    void theConstructorTakesOnlySingleFolderNames() {
        for (String bad : List.of("", ".", "..", "a/b", "a\\b", "C:")) {
            assertThrows(IllegalArgumentException.class, () -> new UpdateJournal(1, "x", bad, "lib.new-1a2b3c4d",
                    "lib.old-1a2b3c4d", true, "0".repeat(64), null, "1.0.0", "t"), bad);
        }
        assertThrows(IllegalArgumentException.class, () -> new UpdateJournal(1, "x", "lib", "a/lib.new-1a2b3c4d",
                "lib.old-1a2b3c4d", true, "0".repeat(64), null, "1.0.0", "t"));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Names
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theUpdateFoldersAreNamedAfterTheLibrary() {
        assertEquals("lib.new-1a2b3c4d", UpdateJournal.stagingName("lib", "1a2b3c4d"));
        assertEquals("Lib.old-1a2b3c4d", UpdateJournal.previousName("Lib", "1a2b3c4d"));
    }

    @Test
    void anUpdateFolderIsTheLibrarysNameAnInfixAndEightHexDigits() {
        assertTrue(UpdateJournal.looksLikeUpdateFolder("lib", "lib.new-1a2b3c4d"));
        assertTrue(UpdateJournal.looksLikeUpdateFolder("lib", "lib.old-00000000"));
        assertTrue(UpdateJournal.looksLikeUpdateFolder("lib", "LIB.NEW-1A2B3C4D"), "ignoring case");
        assertTrue(UpdateJournal.looksLikeUpdateFolder("Lib", "lib.Old-abcdef01"));

        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "lib"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "lib.new-1a2b3c4"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "lib.new-1a2b3c4d5"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "lib.new-1a2b3c4z"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "lib.tmp-1a2b3c4d"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "lib-new-1a2b3c4d"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "other.new-1a2b3c4d"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "xlib.new-1a2b3c4"));
        assertFalse(UpdateJournal.looksLikeUpdateFolder("lib", "lib.new-+1a2b3c4"));
    }
}
