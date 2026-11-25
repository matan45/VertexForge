package com.vertexforge.launcher.engine;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** {@link EngineContent}'s asset format version (VK-1690). */
class EngineContentTest {

    @TempDir
    Path tempDir;

    /** An OK engine in {@code tempDir/engine} whose manifest has this {@code assetFormatVersion} member text. */
    private EngineOption engine(String assetFormatMember) throws InvalidManifestException {
        Path exeDir = tempDir.resolve("engine");
        EngineManifest manifest = EngineManifest.parse("""
                {
                  "manifestVersion": 1,
                  "engineVersion": "1.1.0",
                  %s
                  "configuration": "Release",
                  "editorExe": "Editor.exe",
                  "workingDirectory": ".",
                  "templatesDir": "assets/templates",
                  "scriptLibDir": "assets/scripts/lib"
                }
                """.formatted(assetFormatMember == null ? "" : "\"assetFormatVersion\": " + assetFormatMember + ","));
        return new EngineOption(Optional.of(UUID.randomUUID()), exeDir.resolve("Editor.exe"), "",
                EngineHealth.ok(manifest, exeDir), false, false, false);
    }

    @Test
    void anEnginesAssetFormatVersionIsCarried() throws InvalidManifestException {
        EngineContent content = EngineContent.of(engine("\"1.2.3\"")).orElseThrow();

        assertEquals("1.2.3", content.assetFormatVersion());
        assertEquals("1.1.0", content.engineVersion());
    }

    @Test
    void aMissingOrMalformedAssetFormatVersionIsLeftOut() throws InvalidManifestException {
        assertNull(EngineContent.of(engine(null)).orElseThrow().assetFormatVersion());
        for (String member : List.of("null", "1", "\"1.0\"", "\"01.0.0\"", "\"1.0.0-beta\"", "\" 1.0.0\"", "\"\"",
                "\"x\"")) {
            assertNull(EngineContent.of(engine(member)).orElseThrow().assetFormatVersion(), member);
        }
    }

    @Test
    void theNineComponentConstructorKnowsNoAssetFormat() {
        EngineContent content = new EngineContent(Optional.empty(), "1.0.0", Path.of("t").toAbsolutePath(),
                Path.of("l").toAbsolutePath(), Optional.empty(), Optional.empty(), "label", "path", false);

        assertNull(content.assetFormatVersion());
        assertEquals(new EngineContent(Optional.empty(), "1.0.0", Path.of("t").toAbsolutePath(),
                Path.of("l").toAbsolutePath(), Optional.empty(), Optional.empty(), "label", "path", false, null),
                content);
    }

    @Test
    void aMalformedAssetFormatVersionIsRefusedByTheConstructor() {
        assertThrows(IllegalArgumentException.class, () -> new EngineContent(Optional.empty(), "1.0.0",
                Path.of("t").toAbsolutePath(), Path.of("l").toAbsolutePath(), Optional.empty(), Optional.empty(),
                "label", "path", false, "1.0"));
    }
}
