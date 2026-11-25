package com.vertexforge.launcher.template;

import org.junit.jupiter.api.Test;

import java.nio.charset.CharacterCodingException;
import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** The same strings as VFEngine/tests/test_project_templates.cpp, whose C++ twins check the engine's templates. */
class PlaceholdersTest {

    private static final String NAME = "Tom & Jerry's {Game} $1 ü";

    @Test
    void anXmlAttributeEscapesOnlyWhatXmlNeeds() {
        assertEquals("a &amp; b &lt;c&gt; &quot;d&quot; 'e'", Placeholders.xmlAttribute("a & b <c> \"d\" 'e'"));
        assertEquals("Tom &amp; Jerry's {Game} $1 ü", Placeholders.xmlAttribute(NAME));
    }

    @Test
    void anMTypeStringEscapesBackslashesQuotesAndBraces() {
        assertEquals("a\\\\b \\\"c\\\" \\{d\\}", Placeholders.mtypeString("a\\b \"c\" {d}"));
        assertEquals("Tom & Jerry's \\{Game\\} $1 ü", Placeholders.mtypeString(NAME));
    }

    @Test
    void theFillIsChosenByTheFilesExtension() {
        assertEquals(Placeholders.Fill.XML_ATTRIBUTE, Placeholders.fillFor("assets/scripts/scripts.mtproj"));
        assertEquals(Placeholders.Fill.XML_ATTRIBUTE, Placeholders.fillFor("x.MTPROJ"));
        assertEquals(Placeholders.Fill.MTYPE_STRING, Placeholders.fillFor("game/Main.mt"));
        assertEquals(Placeholders.Fill.NONE, Placeholders.fillFor(".gitignore"));
        assertEquals(Placeholders.Fill.NONE, Placeholders.fillFor("template.json"));
        assertEquals(Placeholders.Fill.NONE, Placeholders.fillFor("a.mtlib"));
    }

    @Test
    void fillingReplacesEveryPlaceholderAndKeepsTheRestOfTheBytes() throws CharacterCodingException {
        byte[] text = "<Project Name=\"{{PROJECT_NAME}}\">\r\n{{PROJECT_NAME}}\r\n".getBytes(StandardCharsets.UTF_8);

        byte[] filled = Placeholders.fill(Placeholders.Fill.XML_ATTRIBUTE, text, NAME);

        assertEquals("<Project Name=\"Tom &amp; Jerry's {Game} $1 ü\">\r\nTom &amp; Jerry's {Game} $1 ü\r\n",
                new String(filled, StandardCharsets.UTF_8));
    }

    @Test
    void aFileWithoutThePlaceholderComesBackUnchanged() throws CharacterCodingException {
        byte[] text = "﻿no placeholder\r\n".getBytes(StandardCharsets.UTF_8);

        assertSame(text, Placeholders.fill(Placeholders.Fill.MTYPE_STRING, text, NAME));
    }

    @Test
    void aFileThatIsNotUtf8IsNotFilledButNoneFilesAreNeverDecoded() throws CharacterCodingException {
        byte[] latin1 = {'{', '{', (byte) 0xE9, '}', '}'};

        assertThrows(CharacterCodingException.class,
                () -> Placeholders.fill(Placeholders.Fill.MTYPE_STRING, latin1, NAME));
        assertArrayEquals(latin1, Placeholders.fill(Placeholders.Fill.NONE, latin1, NAME));
    }
}
