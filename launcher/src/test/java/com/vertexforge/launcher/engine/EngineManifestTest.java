package com.vertexforge.launcher.engine;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class EngineManifestTest {

    /** Verbatim from bin/Editor/Development/x64/engine.json, as tools/engine_version.lua wrote it (VK-1680). */
    static final String DEV_MANIFEST = """
            {
              "manifestVersion": 1,
              "engineVersion": "1.0.0",
              "assetFormatVersion": "1.0.0",
              "projectSchemaVersion": "1.0",
              "pluginApiVersion": 25,
              "configuration": "Development",
              "gitCommit": "599a8a308a224dbf3e8e48577e26a3f1509e4896",
              "editorExe": "Editor.exe",
              "workingDirectory": "../../../../VFEngine/editor",
              "templatesDir": "../../../../assets/templates",
              "scriptLibDir": "../../../../assets/scripts/lib"
            }
            """;

    @TempDir
    Path tempDir;

    private static String withMember(String key, String jsonValue) {
        return DEV_MANIFEST.replaceFirst("\"" + key + "\": [^,\\n]*", "\"" + key + "\": " + jsonValue);
    }

    private static String reason(String text) {
        return assertThrows(InvalidManifestException.class, () -> EngineManifest.parse(text)).getMessage();
    }

    @Test
    void theDevManifestReadsWithEveryMember() throws InvalidManifestException {
        EngineManifest manifest = EngineManifest.parse(DEV_MANIFEST);

        assertEquals(1, manifest.manifestVersion());
        assertEquals("1.0.0", manifest.engineVersion());
        assertEquals("1.0.0", manifest.assetFormatVersion());
        assertEquals("1.0", manifest.projectSchemaVersion());
        assertEquals(25, manifest.pluginApiVersion());
        assertEquals("Development", manifest.configuration());
        assertEquals("599a8a308a224dbf3e8e48577e26a3f1509e4896", manifest.gitCommit());
        assertEquals("Editor.exe", manifest.editorExe());
        assertEquals("1.0.0 Development", manifest.describe());
    }

    @Test
    void pathsAreRelativeToTheManifestsFolder() throws InvalidManifestException {
        EngineManifest manifest = EngineManifest.parse(DEV_MANIFEST);
        Path exeDir = tempDir.resolve("repo/bin/Editor/Development/x64");

        assertEquals(tempDir.resolve("repo/VFEngine/editor"), manifest.workingDirectoryIn(exeDir));
        assertEquals(Optional.of(tempDir.resolve("repo/assets/templates")), manifest.templatesDirIn(exeDir));
        assertEquals(Optional.of(tempDir.resolve("repo/assets/scripts/lib")), manifest.scriptLibDirIn(exeDir));
    }

    @Test
    void aReleaseManifestStartsInItsOwnFolder() throws InvalidManifestException {
        EngineManifest manifest = EngineManifest.parse(withMember("workingDirectory", "\".\""));

        assertEquals(tempDir, manifest.workingDirectoryIn(tempDir));
    }

    @Test
    void informationalMembersMayBeMissingNullOrOfTheWrongType() throws InvalidManifestException {
        EngineManifest manifest = EngineManifest.parse("""
                { "manifestVersion": 1, "engineVersion": "2.3.4", "configuration": "Release",
                  "gitCommit": null, "pluginApiVersion": "25", "assetFormatVersion": 1,
                  "editorExe": "Editor.exe", "workingDirectory": ".", "future": {"x": 1} }
                """);

        assertEquals("2.3.4 Release", manifest.describe());
        assertNull(manifest.gitCommit());
        assertNull(manifest.pluginApiVersion());
        assertNull(manifest.assetFormatVersion());
        assertNull(manifest.projectSchemaVersion());
        assertEquals(Optional.empty(), manifest.templatesDirIn(tempDir));
    }

    @Test
    void aLeadingByteOrderMarkIsAllowed() throws InvalidManifestException {
        assertEquals("1.0.0", EngineManifest.parse("﻿" + DEV_MANIFEST).engineVersion());
    }

    @Test
    void aNewerManifestVersionIsRejectedBeforeAnythingElse() {
        String newer = "{\"manifestVersion\": 2, \"somethingNew\": true}";

        assertTrue(reason(newer).contains("newer than this launcher supports"), reason(newer));
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "[]", "null", "{", "{} {}", "{\"manifestVersion\": 1,}"})
    void textThatIsNotOneJsonObjectIsRejected(String text) {
        reason(text);
    }

    @Test
    void theRequiredMembersAreCheckedStrictly() {
        assertTrue(reason(withMember("manifestVersion", "0")).contains("manifestVersion"));
        assertTrue(reason(withMember("manifestVersion", "1.5")).contains("manifestVersion"));
        assertTrue(reason(withMember("manifestVersion", "\"1\"")).contains("manifestVersion"));
        assertTrue(reason(withMember("engineVersion", "\"1.0\"")).contains("MAJOR.MINOR.PATCH"));
        assertTrue(reason(withMember("engineVersion", "\"01.0.0\"")).contains("MAJOR.MINOR.PATCH"));
        assertTrue(reason(withMember("engineVersion", "1")).contains("engineVersion"));
        assertTrue(reason(withMember("configuration", "\"Profile\"")).contains("Debug, Development, Release"));
        assertTrue(reason(withMember("configuration", "\"development\"")).contains("configuration"));
        assertTrue(reason(withMember("editorExe", "\"bin/Editor.exe\"")).contains("editorExe"));
        assertTrue(reason(withMember("editorExe", "\"..\"")).contains("editorExe"));
        assertTrue(reason(withMember("editorExe", "\"\"")).contains("editorExe"));
        assertTrue(reason(withMember("workingDirectory", "\"\"")).contains("workingDirectory"));
        assertTrue(reason(withMember("workingDirectory", "null")).contains("workingDirectory"));
        assertTrue(reason(DEV_MANIFEST.replace("\"editorExe\": \"Editor.exe\",", "")).contains("editorExe"));
    }

    @Test
    void readIfPresentIsEmptyWithoutAManifest() throws IOException, InvalidManifestException {
        assertEquals(Optional.empty(), EngineManifest.readIfPresent(tempDir));
        assertThrows(NoSuchFileException.class, () -> EngineManifest.read(tempDir.resolve("engine.json")));
    }

    @Test
    void readIfPresentReadsTheManifestBesideTheEditor() throws IOException, InvalidManifestException {
        Files.writeString(tempDir.resolve(EngineManifest.FILE_NAME), DEV_MANIFEST, StandardCharsets.UTF_8);

        assertEquals("Development", EngineManifest.readIfPresent(tempDir).orElseThrow().configuration());
    }

    @Test
    void aManifestThatIsNotUtf8IsRejected() throws IOException {
        Files.write(tempDir.resolve(EngineManifest.FILE_NAME), new byte[]{'{', (byte) 0xC3, (byte) 0x28, '}'});

        InvalidManifestException error = assertThrows(InvalidManifestException.class,
                () -> EngineManifest.readIfPresent(tempDir));
        assertTrue(error.getMessage().contains("UTF-8"), error.getMessage());
    }
}
