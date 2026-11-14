package com.vertexforge.launcher.engine;

import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.Map;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

/** Where the packaged launcher's engine is, and the {@code VERTEXFORGE_EDITOR_PATH} override (VK-1681). */
class BundledEngineTest {

    private static final Path ROOT = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath().normalize();

    @Test
    void theBundledEditorIsBesideTheAppImageInTheReleaseFolder() {
        Path release = ROOT.resolve("dist/VertexForge");
        Path launcher = release.resolve("VertexForge Launcher/VertexForge Launcher.exe");

        assertEquals(Optional.of(release.resolve("bin/Editor/Release/x64/Editor.exe")),
                BundledEngine.expectedExecutable(launcher));
    }

    @Test
    void itDependsOnlyOnTheLaunchersOwnExeNeverOnTheWorkingDirectory() {
        Path launcher = ROOT.resolve("Elsewhere/VertexForge/VertexForge Launcher/VertexForge Launcher.exe");

        Path expected = BundledEngine.expectedExecutable(launcher).orElseThrow();

        assertEquals(ROOT.resolve("Elsewhere/VertexForge/bin/Editor/Release/x64/Editor.exe"), expected);
    }

    @Test
    void anExeTooCloseToTheRootHasNoReleaseFolder() {
        Path root = ROOT.getRoot();

        assertEquals(Optional.empty(), BundledEngine.expectedExecutable(root.resolve("Launcher.exe")));
    }

    @Test
    void theOverrideIsReadFromTheEnvironment() {
        Path editor = ROOT.resolve("custom/Editor.exe");

        assertEquals(Optional.empty(), EditorOverride.from(Map.<String, String>of()::get));
        assertEquals(Optional.empty(), EditorOverride.from(Map.of(EditorOverride.ENV_VAR, "  ")::get));
        assertEquals(editor, EditorOverride.from(Map.of(EditorOverride.ENV_VAR, editor.toString())::get)
                .orElseThrow().executable());
    }

    @Test
    void anOverrideMayBeQuotedOrPadded() {
        Path editor = ROOT.resolve("Program Files/VertexForge/Editor.exe");

        assertEquals(editor, EditorOverride.from(Map.of(EditorOverride.ENV_VAR, "\"" + editor + "\"")::get)
                .orElseThrow().executable());
        assertEquals(editor, EditorOverride.from(Map.of(EditorOverride.ENV_VAR, " " + editor + " ")::get)
                .orElseThrow().executable());
    }

    @Test
    void aRelativeOverrideKeepsItsTextButNamesNoEditor() {
        EditorOverride override = EditorOverride.from(Map.of(EditorOverride.ENV_VAR, "bin/Editor.exe")::get)
                .orElseThrow();

        assertEquals("bin/Editor.exe", override.rawValue());
        assertNull(override.executable());
    }
}
