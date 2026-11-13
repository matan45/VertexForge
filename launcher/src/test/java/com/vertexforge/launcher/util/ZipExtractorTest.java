package com.vertexforge.launcher.util;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ZipExtractorTest {

    @TempDir
    Path tempDir;

    /** Builds a zip; a name ending in '/' becomes a directory entry. */
    private static byte[] zip(Map<String, String> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream out = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, String> entry : entries.entrySet()) {
                out.putNextEntry(new ZipEntry(entry.getKey()));
                if (!entry.getKey().endsWith("/")) {
                    out.write(entry.getValue().getBytes(StandardCharsets.UTF_8));
                }
                out.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    @Test
    void extractsFilesAndFoldersByteForByte() throws IOException {
        Map<String, String> entries = new LinkedHashMap<>();
        entries.put("empty/", "");
        entries.put("core/Collection.mt", "class Collection {}\r\n");
        entries.put("engine/oop/Actor.mt", "class Actor {}\n");
        Path target = Files.createDirectory(tempDir.resolve("lib"));

        ZipExtractor.extract(new ByteArrayInputStream(zip(entries)), target);

        assertTrue(Files.isDirectory(target.resolve("empty")));
        assertArrayEquals("class Collection {}\r\n".getBytes(StandardCharsets.UTF_8),
                Files.readAllBytes(target.resolve("core/Collection.mt")));
        assertArrayEquals("class Actor {}\n".getBytes(StandardCharsets.UTF_8),
                Files.readAllBytes(target.resolve("engine/oop/Actor.mt")));
    }

    @Test
    void tellsTheListenerAboutEveryFileButNoFolder() throws IOException {
        Map<String, String> entries = new LinkedHashMap<>();
        entries.put("empty/", "");
        entries.put("core/Collection.mt", "a");
        entries.put("engine/oop/Actor.mt", "b");
        Path target = Files.createDirectory(tempDir.resolve("lib"));
        List<Path> heard = new ArrayList<>();

        ZipExtractor.extract(new ByteArrayInputStream(zip(entries)), target, heard::add);

        assertEquals(List.of(target.resolve("core/Collection.mt"), target.resolve("engine/oop/Actor.mt")), heard);
        for (Path file : heard) {
            assertTrue(Files.isRegularFile(file), file + " is written before the listener hears about it");
        }
    }

    @Test
    void aListenerThatThrowsStopsTheExtraction() throws IOException {
        Map<String, String> entries = new LinkedHashMap<>();
        entries.put("a.mt", "a");
        entries.put("b.mt", "b");
        Path target = Files.createDirectory(tempDir.resolve("lib"));

        IOException error = assertThrows(IOException.class,
                () -> ZipExtractor.extract(new ByteArrayInputStream(zip(entries)), target, file -> {
                    throw new IOException("stop at " + file.getFileName());
                }));

        assertEquals("stop at a.mt", error.getMessage());
        assertFalse(Files.exists(target.resolve("b.mt")));
    }

    @Test
    void rejectsAnEntryThatEscapesTheTargetFolder() throws IOException {
        Path target = Files.createDirectory(tempDir.resolve("lib"));
        byte[] evil = zip(Map.of("../evil.txt", "escaped"));

        IOException error = assertThrows(IOException.class,
                () -> ZipExtractor.extract(new ByteArrayInputStream(evil), target));

        assertEquals("Invalid zip entry: ../evil.txt", error.getMessage());
        assertFalse(Files.exists(tempDir.resolve("evil.txt")));
        try (Stream<Path> files = Files.list(tempDir)) {
            assertEquals(List.of(target), files.toList());
        }
    }

    @Test
    void rejectsASiblingFolderWithTheSamePrefix() throws IOException {
        Path target = Files.createDirectory(tempDir.resolve("lib"));
        byte[] sibling = zip(Map.of("../lib2/x.mt", "escaped"));

        assertThrows(IOException.class, () -> ZipExtractor.extract(new ByteArrayInputStream(sibling), target));
        assertFalse(Files.exists(tempDir.resolve("lib2")));
    }
}
