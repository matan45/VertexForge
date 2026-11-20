package com.vertexforge.launcher.template;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class LibraryManifestTest {

    private static byte[] utf8(String text) {
        return text.getBytes(StandardCharsets.UTF_8);
    }

    @Test
    void theHashIsSha256AfterCrlfBecomesLf() {
        // SHA-256 of "a\n".
        assertEquals("87428fc522803d31065e7bce3cf03fe475096631e5e07bbd7a0fde60c4cf25c7", LibraryManifest.hash(utf8("a\n")));
        assertEquals(LibraryManifest.hash(utf8("a\n")), LibraryManifest.hash(utf8("a\r\n")));
        assertEquals(LibraryManifest.hash(utf8("x\ny\n")), LibraryManifest.hash(utf8("x\r\ny\r\n")));
    }

    @Test
    void aLoneCarriageReturnIsContentNotALineEnding() {
        assertNotEquals(LibraryManifest.hash(utf8("a\rb")), LibraryManifest.hash(utf8("ab")));
        assertNotEquals(LibraryManifest.hash(utf8("a\r")), LibraryManifest.hash(utf8("a")));
        // Only the CR right before the LF goes: "a\r\r\n" is "a\r\n" after the change, not "a\n".
        assertNotEquals(LibraryManifest.hash(utf8("a\n")), LibraryManifest.hash(utf8("a\r\r\n")));
    }

    @Test
    void aFileLargerThanTheBufferHashesLikeItsLfTwin() {
        String line = "x".repeat(1000) + "\r\n";
        String crlf = line.repeat(40);

        assertEquals(LibraryManifest.hash(utf8(crlf.replace("\r\n", "\n"))), LibraryManifest.hash(utf8(crlf)));
    }

    @Test
    void theJsonIsSortedLfAndNamesTheHashingRule() {
        Map<String, String> files = new LinkedHashMap<>();
        files.put("engine/Log.mt", "b");
        files.put("core/Array.mt", "a");

        String text = new String(LibraryManifest.toJson("1.2.3", files), StandardCharsets.UTF_8);

        assertFalse(text.contains("\r"));
        assertTrue(text.endsWith("}\n"));
        assertFalse(text.startsWith("﻿"));
        JsonObject json = JsonParser.parseString(text).getAsJsonObject();
        assertEquals(List.of("formatVersion", "engineVersion", "hashAlgorithm", "lineEndings", "files"),
                new ArrayList<>(json.keySet()));
        assertEquals(1, json.get("formatVersion").getAsInt());
        assertEquals("1.2.3", json.get("engineVersion").getAsString());
        assertEquals("SHA-256", json.get("hashAlgorithm").getAsString());
        assertEquals("lf", json.get("lineEndings").getAsString());
        assertEquals(List.of("core/Array.mt", "engine/Log.mt"), new ArrayList<>(json.getAsJsonObject("files").keySet()));
    }
}
