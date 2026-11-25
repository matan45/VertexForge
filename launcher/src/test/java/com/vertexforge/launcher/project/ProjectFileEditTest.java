package com.vertexforge.launcher.project;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class ProjectFileEditTest {

    @TempDir
    Path tempDir;

    private Path original() {
        return tempDir.resolve("Projects").resolve("Game");
    }

    private Path copy() {
        return tempDir.resolve("Projects").resolve("Game-1.1.0");
    }

    private String edit(String text) throws InvalidProjectFileException {
        return new String(ProjectFileEdit.forCopy(text.getBytes(StandardCharsets.UTF_8), "Game-1.1.0", original(),
                copy()), StandardCharsets.UTF_8);
    }

    private static JsonObject json(String text) {
        return JsonParser.parseString(text).getAsJsonObject();
    }

    /** A JSON string literal holding {@code value}. */
    private static String quoted(String value) {
        return "\"" + value.replace("\\", "\\\\") + "\"";
    }

    @Test
    void onlyTheNameChangesAndEverythingElseKeepsItsPlaceAndText() throws InvalidProjectFileException {
        String original = """
                {
                  "schemaVersion": "1.0",
                  "custom": {"nested": [1, 2.50, {"deep": true}], "empty": {}},
                  "projectName": "Game",
                  "version": "0.1.0",
                  "workingDirectory": "assets",
                  "startupScene": "scenes/Main.vfScene",
                  "exeIconPath": "",
                  "engineVersion": "VertexForge 1.0.0",
                  "ratio": 1.50,
                  "big": 12345678901234567890,
                  "exponent": 1e3,
                  "negativeZero": -0,
                  "nothing": null,
                  "list": [],
                  "lastModified": "2026-01-02T03:04:05Z"
                }
                """;

        String edited = edit(original);

        JsonObject before = json(original);
        JsonObject after = json(edited);
        assertEquals(new ArrayList<>(before.keySet()), new ArrayList<>(after.keySet()), "members keep their order");
        assertEquals("Game-1.1.0", after.get("projectName").getAsString());
        after.addProperty("projectName", "Game");
        assertEquals(before, after, "nothing else changed");
        for (String number : List.of("\"ratio\": 1.50", "\"big\": 12345678901234567890", "\"exponent\": 1e3",
                "\"negativeZero\": -0", "2.50")) {
            assertTrue(edited.contains(number), "numbers keep their text: " + number + " in\n" + edited);
        }
        assertTrue(edited.contains("\"nothing\": null"), "null members are kept");
        assertTrue(edited.endsWith("}\n"), "LF and a final newline, as the original");
        assertFalse(edited.contains("\r"));
    }

    @Test
    void crlfLineEndsAreKept() throws InvalidProjectFileException {
        String edited = edit("{\r\n  \"projectName\": \"Game\",\r\n  \"workingDirectory\": \"assets\"\r\n}\r\n");

        assertEquals("{\r\n  \"projectName\": \"Game-1.1.0\",\r\n  \"workingDirectory\": \"assets\"\r\n}\r\n", edited);
    }

    @Test
    void noFinalNewlineStaysThatWay() throws InvalidProjectFileException {
        String edited = edit("{\"projectName\": \"Game\", \"workingDirectory\": \".\"}");

        assertEquals("{\n  \"projectName\": \"Game-1.1.0\",\n  \"workingDirectory\": \".\"\n}", edited);
    }

    @Test
    void aByteOrderMarkIsDropped() throws InvalidProjectFileException {
        byte[] edited = ProjectFileEdit.forCopy(("﻿{\"projectName\": \"Game\"}\n").getBytes(StandardCharsets.UTF_8),
                "Copy", original(), copy());

        assertArrayEquals("{\n  \"projectName\": \"Copy\"\n}\n".getBytes(StandardCharsets.UTF_8), edited);
    }

    @Test
    void theNameIsWrittenAsItIsWithoutHtmlEscapes() throws InvalidProjectFileException {
        byte[] edited = ProjectFileEdit.forCopy("{\"projectName\": \"Game\"}".getBytes(StandardCharsets.UTF_8),
                "Tom & Jerry's <Game> ü", original(), copy());

        String text = new String(edited, StandardCharsets.UTF_8);
        assertTrue(text.contains("\"projectName\": \"Tom & Jerry's <Game> ü\""), text);
    }

    @Test
    void anAbsoluteWorkingDirectoryInsideBecomesRelativeToTheCopy() throws InvalidProjectFileException {
        String assets = edit("{\"projectName\": \"Game\", \"workingDirectory\": "
                + quoted(original().resolve("assets").toString()) + "}");
        assertEquals("assets", json(assets).get("workingDirectory").getAsString());

        String deeper = edit("{\"projectName\": \"Game\", \"workingDirectory\": "
                + quoted(original().resolve("content").resolve("assets").toString()) + "}");
        assertEquals("content/assets", json(deeper).get("workingDirectory").getAsString());

        String self = edit("{\"projectName\": \"Game\", \"workingDirectory\": " + quoted(original().toString()) + "}");
        assertEquals(".", json(self).get("workingDirectory").getAsString());
    }

    @Test
    void anAbsoluteWorkingDirectoryInsideIsFoundWhateverItsCase() throws InvalidProjectFileException {
        assumeTrue(TestSupport.isWindows(), "only Windows ignores case in paths");

        String edited = edit("{\"projectName\": \"Game\", \"workingDirectory\": "
                + quoted(original().toString().toUpperCase(Locale.ROOT) + "\\Assets") + "}");

        assertEquals("Assets", json(edited).get("workingDirectory").getAsString());
    }

    @Test
    void relativeOrOutsidePathsAreKept() throws InvalidProjectFileException {
        String outside = tempDir.resolve("Shared").resolve("assets").toString();
        String sibling = tempDir.resolve("Projects").resolve("GameAssets").resolve("icon.png").toString();
        String original = "{\"projectName\": \"Game\", \"workingDirectory\": " + quoted(outside)
                + ", \"startupScene\": \"scenes/Main.vfScene\", \"exeIconPath\": " + quoted(sibling)
                + ", \"inputMapping\": \"input/actions.json\"}";

        JsonObject edited = json(edit(original));

        assertEquals(outside, edited.get("workingDirectory").getAsString());
        assertEquals("scenes/Main.vfScene", edited.get("startupScene").getAsString());
        assertEquals(sibling, edited.get("exeIconPath").getAsString());
        assertEquals("input/actions.json", edited.get("inputMapping").getAsString());
    }

    @Test
    void absolutePathsInsideMoveToTheSamePlaceInTheCopy() throws InvalidProjectFileException {
        String scene = original().resolve("assets").resolve("scenes").resolve("Main.vfScene").toString();
        String mapping = original().resolve("assets").resolve("input.json").toString();
        String original = "{\"projectName\": \"Game\", \"workingDirectory\": \"assets\", \"startupScene\": "
                + quoted(scene) + ", \"inputMapping\": " + quoted(mapping) + ", \"exeIconPath\": 42}";

        JsonObject edited = json(edit(original));

        assertEquals("assets", edited.get("workingDirectory").getAsString(), "a relative one is kept");
        assertEquals(copy().resolve("assets").resolve("scenes").resolve("Main.vfScene").toString(),
                edited.get("startupScene").getAsString());
        assertEquals(copy().resolve("assets").resolve("input.json").toString(),
                edited.get("inputMapping").getAsString());
        assertEquals(42, edited.get("exeIconPath").getAsInt(), "a value that is not a string is kept");
    }

    /** A '..' that leaves the folder and comes back by its name would lead the copy's editor into the ORIGINAL. */
    @Test
    void aRelativeWorkingDirectoryThatComesBackIntoTheOriginalIsRewritten() throws InvalidProjectFileException {
        JsonObject edited = json(edit("{\"projectName\": \"Game\", \"workingDirectory\": \"../Game/assets\","
                + " \"startupScene\": \"scenes/Main.vfScene\"}"));

        assertEquals("assets", edited.get("workingDirectory").getAsString());
        assertEquals("scenes/Main.vfScene", edited.get("startupScene").getAsString(),
                "a member relative to the working directory already leads into the copy");
    }

    @Test
    void relativeMembersThatComeBackIntoTheOriginalAreRepointed() throws InvalidProjectFileException {
        assumeTrue(TestSupport.isWindows(), "the expected texts use Windows paths");
        String original = "{\"projectName\": \"Game\", \"workingDirectory\": \"assets\","
                + " \"startupScene\": \"../../Game/assets/scenes/Main.vfScene\","
                + " \"inputMapping\": \"../../Game/input.json\","
                + " \"exeIconPath\": \"..\\\\..\\\\Game\\\\assets\\\\icon.png\"}";

        JsonObject edited = json(edit(original));

        assertEquals("assets", edited.get("workingDirectory").getAsString());
        // In the copy's working directory: relative to it, in the member's own separators.
        assertEquals("scenes/Main.vfScene", edited.get("startupScene").getAsString());
        assertEquals("icon.png", edited.get("exeIconPath").getAsString());
        // In the copy's folder but not its working directory: the absolute path in the copy.
        assertEquals(copy().resolve("input.json").toString().replace('\\', '/'),
                edited.get("inputMapping").getAsString());
    }

    @Test
    void relativeMembersThatLeaveTheOriginalAreKept() throws InvalidProjectFileException {
        JsonObject edited = json(edit("{\"projectName\": \"Game\", \"workingDirectory\": \"assets\","
                + " \"inputMapping\": \"../../Shared/input.json\"}"));

        assertEquals("../../Shared/input.json", edited.get("inputMapping").getAsString());
    }

    @Test
    void withoutAWorkingDirectoryMembersAreTakenRelativeToEachFolder() throws InvalidProjectFileException {
        JsonObject edited = json(edit("{\"projectName\": \"Game\", \"inputMapping\": \"input.json\"}"));

        assertEquals("input.json", edited.get("inputMapping").getAsString());
    }

    @Test
    void aPathWrittenWithForwardSlashesKeepsThem() throws InvalidProjectFileException {
        assumeTrue(TestSupport.isWindows(), "only Windows paths have two separators");
        String icon = original().resolve("assets").resolve("icon.png").toString().replace('\\', '/');

        JsonObject edited = json(edit("{\"projectName\": \"Game\", \"exeIconPath\": " + quoted(icon) + "}"));

        assertEquals(copy().resolve("assets").resolve("icon.png").toString().replace('\\', '/'),
                edited.get("exeIconPath").getAsString());
    }

    @Test
    void aFileTheReaderRefusesIsRefusedWithTheReadersWords() {
        for (String text : List.of("{\"projectName\": \"Game\", // note\n}", "{\"projectName\": \"Game\",}",
                "{\"projectName\": \"Game\"} {}", "{\"projectName\": ", "[1, 2]", "", "  ")) {
            InvalidProjectFileException expected = assertThrows(InvalidProjectFileException.class,
                    () -> ProjectFileReader.parse(text), text);

            InvalidProjectFileException actual = assertThrows(InvalidProjectFileException.class,
                    () -> ProjectFileEdit.forCopy(text.getBytes(StandardCharsets.UTF_8), "Copy", original(), copy()),
                    text);

            assertEquals(expected.getMessage(), actual.getMessage(), text);
        }
    }

    @Test
    void bytesThatAreNotUtf8OrTooManyAreRefused() {
        InvalidProjectFileException notText = assertThrows(InvalidProjectFileException.class,
                () -> ProjectFileEdit.forCopy(new byte[]{'{', (byte) 0xC3, '}'}, "Copy", original(), copy()));
        assertEquals("The file is not valid UTF-8 text", notText.getMessage());

        byte[] huge = new byte[ProjectFileReader.MAX_BYTES + 1];
        java.util.Arrays.fill(huge, (byte) ' ');
        InvalidProjectFileException tooLarge = assertThrows(InvalidProjectFileException.class,
                () -> ProjectFileEdit.forCopy(huge, "Copy", original(), copy()));
        assertEquals("The file is larger than 1 MiB, which no project file is", tooLarge.getMessage());
    }

    @Test
    void theFoldersMustBeAbsolute() {
        byte[] text = "{\"projectName\": \"Game\"}".getBytes(StandardCharsets.UTF_8);

        assertThrows(IllegalArgumentException.class, () -> ProjectFileEdit.forCopy(text, "Copy", Path.of("Game"),
                copy()));
        assertThrows(IllegalArgumentException.class, () -> ProjectFileEdit.forCopy(text, "Copy", original(),
                Path.of("Copy")));
    }
}
