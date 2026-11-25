package com.vertexforge.launcher.template;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class TemplateManifestTest {

    static final String EMPTY = """
            {
              "formatVersion": 1,
              "id": "empty",
              "name": "Empty",
              "description": "Just the library.",
              "minimumEngineVersion": "1.0.0",
              "workingDirectory": "assets",
              "startupScene": "scenes/Main.vfScene",
              "scriptLibrary": "assets/scripts/lib",
              "options": [
                {"id": "examples", "name": "Include examples", "description": "d", "default": true,
                 "target": "assets/scripts/game/examples"},
                {"id": "vfxLibrary", "name": "Include VFX library", "target": "assets/vfx/library"}
              ],
              "future": {"x": 1}
            }
            """;

    private static String reason(String text) {
        return assertThrows(TemplateException.class, () -> TemplateManifest.parse(text)).getMessage();
    }

    /** {@link #EMPTY} with the first {@code key} member's value replaced; for members that fit on one line. */
    private static String with(String key, String jsonValue) {
        return EMPTY.replaceFirst("\"" + key + "\": [^,\\n]*",
                java.util.regex.Matcher.quoteReplacement("\"" + key + "\": " + jsonValue));
    }

    @Test
    void theEngineTemplateFormatReadsWithEveryMember() throws TemplateException {
        TemplateManifest manifest = TemplateManifest.parse(EMPTY);

        assertEquals("empty", manifest.id());
        assertEquals("Empty", manifest.name());
        assertEquals("Just the library.", manifest.description());
        assertEquals("1.0.0", manifest.minimumEngineVersion());
        assertEquals("assets", manifest.workingDirectory());
        assertEquals("scenes/Main.vfScene", manifest.startupScene());
        assertEquals("assets/scenes/Main.vfScene", manifest.startupScenePath());
        assertEquals("assets/scripts/lib", manifest.scriptLibrary());
        assertEquals(List.of(
                new TemplateManifest.Option("examples", "Include examples", "d", true, "assets/scripts/game/examples"),
                new TemplateManifest.Option("vfxLibrary", "Include VFX library", "", false, "assets/vfx/library")),
                manifest.options());
    }

    @Test
    void theRepositorysOwnTemplateReads() throws Exception {
        Path file = Path.of(System.getProperty("vertexforge.repoRoot", ".."))
                .resolve("assets/templates/empty/template.json");
        assertTrue(Files.isRegularFile(file), file + " is missing");

        TemplateManifest manifest = TemplateManifest.read(file);

        assertEquals("empty", manifest.id());
        assertEquals("assets", manifest.workingDirectory());
        assertEquals(List.of("examples", "vfxLibrary"), manifest.options().stream().map(TemplateManifest.Option::id)
                .toList());
    }

    @Test
    void theProjectFolderMayBeTheWorkingDirectory() throws TemplateException {
        TemplateManifest manifest = TemplateManifest.parse(with("workingDirectory", "\".\""));

        assertEquals(".", manifest.workingDirectory());
        assertEquals("scenes/Main.vfScene", manifest.startupScenePath());
    }

    @Test
    void aNewerFormatIsRefusedBeforeAnythingElseIsRead() {
        assertTrue(reason("{\"formatVersion\": 2}").contains("newer than this launcher supports"));
        assertTrue(reason("{\"formatVersion\": 0}").contains("\"formatVersion\""));
        assertTrue(reason("{}").contains("\"formatVersion\""));
    }

    @Test
    void requiredMembersMustBeThere() {
        for (String key : List.of("id", "name", "minimumEngineVersion", "workingDirectory", "startupScene",
                "scriptLibrary")) {
            String without = EMPTY.replaceFirst("\\s*\"" + key + "\": [^,\\n]*,", "");
            assertTrue(reason(without).contains("\"" + key + "\" is missing"), key);
        }
    }

    @ParameterizedTest
    @ValueSource(strings = {"\"/abs\"", "\"C:/abs\"", "\"a\\\\b\"", "\"a/../b\"", "\"./a\"", "\"a//b\"", "\"a/\"",
            "\"a.\"", "\"a \"", "\"\"", "\".vertexforge/x\"", "\".VertexForge\""})
    void aPathThatIsNotPlainlyInsideTheProjectIsRefused(String value) {
        assertTrue(reason(with("scriptLibrary", value)).startsWith(TemplateManifest.FILE_NAME + " is not valid: "),
                value);
    }

    @Test
    void theStartupSceneMustBeAScene() {
        assertTrue(reason(with("startupScene", "\"scenes/Main.json\"")).contains("not a .vfScene file"));
    }

    @Test
    void optionsAreCheckedOneByOne() {
        assertTrue(reason(EMPTY.replace("\"vfxLibrary\"", "\"examples\"")).contains("listed twice"));
        assertTrue(reason(EMPTY.replace("\"default\": true", "\"default\": \"yes\"")).contains("true or false"));
        assertTrue(reason(EMPTY.replace("\"target\": \"assets/vfx/library\"", "\"target\": \"../vfx\""))
                .contains("target"));
        assertTrue(reason(EMPTY.substring(0, EMPTY.indexOf("\"options\"")) + "\"options\": {}}")
                .contains("\"options\" is not an array"));
    }

    @Test
    void invalidJsonAndAByteOrderMark() throws TemplateException {
        assertTrue(reason("{").contains("not valid JSON"));
        // Strict parsing refuses a second document after the first.
        assertTrue(reason(EMPTY + "{}").startsWith(TemplateManifest.FILE_NAME + " is not valid: "));
        assertEquals("empty", TemplateManifest.parse("\uFEFF" + EMPTY).id());
    }

    @Test
    void versionsCompareNumerically() {
        assertTrue(TemplateManifest.compareVersions("1.10.0", "1.9.0") > 0);
        assertEquals(0, TemplateManifest.compareVersions("2.0.0", "2.0.0"));
        assertTrue(TemplateManifest.compareVersions("0.9.9", "1.0.0") < 0);
        assertThrows(IllegalArgumentException.class, () -> TemplateManifest.compareVersions("1.0", "1.0.0"));
    }

    @Test
    void anEngineMustBeAsNewAsTheTemplateAsks() throws TemplateException {
        TemplateManifest manifest = TemplateManifest.parse(with("minimumEngineVersion", "\"1.2.0\""));

        assertTrue(manifest.supportsEngine("1.2.0"));
        assertTrue(manifest.supportsEngine("2.0.0"));
        assertFalse(manifest.supportsEngine("1.1.9"));
    }
}
