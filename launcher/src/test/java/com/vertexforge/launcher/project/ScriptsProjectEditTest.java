package com.vertexforge.launcher.project;

import com.vertexforge.launcher.project.ScriptsProjectEdit.Result;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** VK-1695: a copy's scripts.mtproj gets the copy's name, and nothing else in the file changes. */
class ScriptsProjectEditTest {

    /** The repository root: the launcher's tests run from launcher/ (surefire passes it, see the pom). */
    private static final Path REPO_ROOT = Path.of(System.getProperty("vertexforge.repoRoot", ".."));

    private static byte[] utf8(String text) {
        return text.getBytes(StandardCharsets.UTF_8);
    }

    private static String renamed(String original, String name) {
        Result result = ScriptsProjectEdit.rename(utf8(original), name);
        return new String(assertInstanceOf(Result.Renamed.class, result).bytes(), StandardCharsets.UTF_8);
    }

    private static String unchangedReason(byte[] original) {
        return assertInstanceOf(Result.Unchanged.class, ScriptsProjectEdit.rename(original, "Copy")).reason();
    }

    @Test
    void theTemplatesFileChangesOnlyInsideTheNameQuotes() throws IOException {
        byte[] template = Files.readAllBytes(
                REPO_ROOT.resolve("assets/templates/empty/project/assets/scripts/scripts.mtproj"));
        String text = new String(template, StandardCharsets.UTF_8);
        assertTrue(text.contains("Name=\"{{PROJECT_NAME}}\""), "the template is what this test expects");

        Result result = ScriptsProjectEdit.rename(template, "Game-copy");

        byte[] bytes = assertInstanceOf(Result.Renamed.class, result).bytes();
        assertArrayEquals(utf8(text.replace("Name=\"{{PROJECT_NAME}}\"", "Name=\"Game-copy\"")), bytes);
    }

    /** Projects older than the templates hold the engine's own file: no prolog, Name="VertexForgeScripts". */
    @Test
    void theOldEngineFileWithoutAPrologIsRenamedToo() throws IOException {
        byte[] legacy = Files.readAllBytes(REPO_ROOT.resolve("assets/scripts/scripts.mtproj"));
        String text = new String(legacy, StandardCharsets.UTF_8);
        assertTrue(text.contains("Name=\"VertexForgeScripts\""), "the engine's file is what this test expects");

        assertEquals(text.replace("Name=\"VertexForgeScripts\"", "Name=\"Mine\""), renamed(text, "Mine"));
    }

    @Test
    void lineEndingsCommentsAndOtherAttributesStay() {
        String original = "<!-- a comment -->\r\n<?xml version=\"1.0\"?>\r\n<!-- another -->\r\n"
                + "<Project Version=\"1.0.0\"   Name = \"Old\"  Extra=\"x\">\r\n  <Source/>\r\n</Project>\r\n";

        assertEquals(original.replace("\"Old\"", "\"New\""), renamed(original, "New"));
    }

    @Test
    void aByteOrderMarkIsKept() {
        String original = "﻿<Project Name=\"Old\"/>";

        assertEquals("﻿<Project Name=\"New\"/>", renamed(original, "New"));
    }

    @Test
    void theLastNameWinsAsInMType() {
        assertEquals("<Project Name=\"A\" Name=\"New\">", renamed("<Project Name=\"A\" Name=\"B\">", "New"));
    }

    @Test
    void onlyTheAttributeNamedExactlyNameCounts() {
        assertEquals("<Project NameX=\"a\" xName=\"b\" name=\"c\" Name=\"New\">",
                renamed("<Project NameX=\"a\" xName=\"b\" name=\"c\" Name=\"Old\">", "New"));
    }

    /** The escape a project creation uses: mType decodes no entities, so this stays a label. */
    @Test
    void theNewNameIsEscapedAsACreationEscapesIt() {
        assertEquals("<Project Name=\"Tom &amp; Jerry's\">", renamed("<Project Name=\"Old\">", "Tom & Jerry's"));
    }

    @Test
    void nonAsciiNamesAreWrittenAsUtf8() {
        assertEquals("<Project Name=\"Ünïcödé 名前\">", renamed("<Project Name=\"Old\">", "Ünïcödé 名前"));
    }

    @Test
    void renamingTwiceGivesTheSameBytes() {
        String once = renamed("<Project Version=\"1\" Name=\"Old\">\n</Project>\n", "New");

        assertEquals(once, renamed(once, "New"));
    }

    @Test
    void anEmptyNameValueIsReplaced() {
        assertEquals("<Project Name=\"New\">", renamed("<Project Name=\"\">", "New"));
    }

    @Test
    void whatMTypeWouldNotReadAsANameIsLeftAlone() {
        assertTrue(unchangedReason(utf8("<Project Version=\"1\">")).contains("no Name"));
        // Single quotes are no value to mType's parser.
        assertTrue(unchangedReason(utf8("<Project Name='Old'>")).contains("no Name"));
        assertTrue(unchangedReason(utf8("<Project Name=\"Old")).contains("never ends"));
        assertTrue(unchangedReason(utf8("<Solution Name=\"Old\">")).contains("<Solution>"));
        assertTrue(unchangedReason(utf8("Project Name=\"Old\"")).contains("XML element"));
        assertTrue(unchangedReason(utf8("<?xml version=\"1.0\"")).contains("prolog"));
        assertTrue(unchangedReason(utf8("")).contains("XML element"));
        // A comment that never ends swallows the whole file, as in mType.
        assertTrue(unchangedReason(utf8("<!-- <Project Name=\"Old\">")).contains("XML element"));
    }

    @Test
    void utf16AndOversizedFilesAreLeftAlone() {
        byte[] utf16 = "﻿<Project Name=\"Old\">".getBytes(StandardCharsets.UTF_16LE);
        assertTrue(unchangedReason(utf16).contains("UTF-16"));

        byte[] big = new byte[ScriptsProjectEdit.MAX_BYTES + 1];
        assertTrue(unchangedReason(big).contains("larger"));
    }
}
