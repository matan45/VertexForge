package com.vertexforge.launcher.service;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectCreationResult;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectCreatorTest {

    @TempDir
    Path tempDir;

    private ProjectCreator creator() {
        return new ProjectCreator(TestSupport.fixedClock(), TestSupport.log(tempDir.resolve(".logs")).log);
    }

    private ProjectCreationResult create(String name) {
        return creator().createProject(name, tempDir);
    }

    private static byte[] resource(String name) throws IOException {
        try (InputStream in = ProjectCreatorTest.class.getResourceAsStream(name)) {
            assertNotNull(in, "missing classpath resource " + name);
            return in.readAllBytes();
        }
    }

    /** Entry name (always '/'-separated) to bytes, files only. */
    static Map<String, byte[]> zipEntries(byte[] zip) throws IOException {
        Map<String, byte[]> entries = new TreeMap<>();
        try (ZipInputStream in = new ZipInputStream(new java.io.ByteArrayInputStream(zip))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                if (!entry.isDirectory()) {
                    entries.put(entry.getName(), in.readAllBytes());
                }
            }
        }
        return entries;
    }

    @Test
    void createsTheFolderTree() {
        ProjectCreationResult result = create("Game");

        assertTrue(result.success(), () -> "creation failed: " + result.errorMessage());
        assertNull(result.errorMessage());
        Path projectDir = tempDir.resolve("Game");
        assertEquals(projectDir.resolve("Game.vfproj"), result.projectPath());
        for (String dir : List.of("scenes", "assets", "scripts", "scripts/game", "scripts/compiled", "scripts/lib")) {
            assertTrue(Files.isDirectory(projectDir.resolve(dir)), dir + " is missing");
        }
    }

    @Test
    void writesTheProjectFileFieldsInOrder() throws IOException {
        Path projectFile = create("Game").projectPath();

        JsonObject json = JsonParser.parseString(Files.readString(projectFile)).getAsJsonObject();

        assertEquals(List.of("schemaVersion", "projectName", "version", "workingDirectory", "startupScene",
                "exeIconPath", "engineVersion", "lastModified"), new ArrayList<>(json.keySet()));
        assertEquals("1.0", json.get("schemaVersion").getAsString());
        assertEquals("Game", json.get("projectName").getAsString());
        assertEquals("0.1.0", json.get("version").getAsString());
        assertEquals(".", json.get("workingDirectory").getAsString());
        assertEquals("scenes/Main.vfScene", json.get("startupScene").getAsString());
        assertEquals("", json.get("exeIconPath").getAsString());
        assertEquals("VertexForge 1.0.0", json.get("engineVersion").getAsString());
        assertEquals("2026-01-02T03:04:05Z", json.get("lastModified").getAsString());
    }

    @Test
    void copiesTheSceneTemplateByteForByte() throws IOException {
        create("Game");

        assertArrayEquals(resource("/templates/Main.vfScene"),
                Files.readAllBytes(tempDir.resolve("Game/scenes/Main.vfScene")));
    }

    @Test
    void writesTheScriptProject() throws IOException {
        create("Game");

        String expected = """
                <?xml version="1.0" encoding="UTF-8"?>
                <Project Name="Game" Version="1.0.0">
                  <Source>
                    <Include>game/**/*.mt</Include>
                  </Source>
                  <Output Directory="compiled" />
                  <Imports>
                    <SearchPath>lib</SearchPath>
                  </Imports>
                </Project>
                """;
        assertEquals(expected, Files.readString(tempDir.resolve("Game/scripts/scripts.mtproj")));
    }

    @Test
    void writesTheMainScript() throws IOException {
        create("Game");

        String script = Files.readString(tempDir.resolve("Game/scripts/game/Main.mt"));
        assertTrue(script.contains("@Script"));
        assertTrue(script.contains("public class Main"));
        assertTrue(script.contains("import * from \"engine/Log.mt\";"));
    }

    @Test
    void extractsTheWholeScriptLibrary() throws IOException {
        create("Game");
        Path libDir = tempDir.resolve("Game/scripts/lib");
        Map<String, byte[]> expected = zipEntries(resource("/templates/script-library.zip"));

        List<Path> extracted;
        try (Stream<Path> files = Files.walk(libDir)) {
            extracted = files.filter(Files::isRegularFile).toList();
        }

        assertFalse(expected.isEmpty());
        assertEquals(expected.size(), extracted.size());
        for (Map.Entry<String, byte[]> entry : expected.entrySet()) {
            Path file = libDir.resolve(entry.getKey());
            assertTrue(Files.isRegularFile(file), entry.getKey() + " was not extracted");
            assertArrayEquals(entry.getValue(), Files.readAllBytes(file), entry.getKey());
        }
    }

    @Test
    void failsWithoutLeavingAnythingWhenTheLocationIsAFile() throws IOException {
        Path blocker = Files.writeString(tempDir.resolve("blocker"), "not a folder");

        ProjectCreationResult result = creator().createProject("Game", blocker);

        assertFalse(result.success());
        assertNull(result.projectPath());
        assertNotNull(result.errorMessage());
        assertTrue(Files.isRegularFile(blocker));
        assertEquals("not a folder", Files.readString(blocker));
        try (Stream<Path> entries = Files.list(tempDir)) {
            assertEquals(List.of(blocker), entries.filter(p -> !p.getFileName().toString().equals(".logs")).toList());
        }
    }
}
