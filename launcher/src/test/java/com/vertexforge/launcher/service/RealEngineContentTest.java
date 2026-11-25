package com.vertexforge.launcher.service;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.template.LibraryManifest;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * A project made from this repository's own {@code assets/} as the engine, the way a dev build's engine.json names it
 * (VK-1689 acceptance): its library is byte-identical to the engine's, the examples arrive, and the files the
 * template fills in name the project. Needs the full repo checkout ({@code vertexforge.repoRoot}, set by the pom).
 */
class RealEngineContentTest {

    @TempDir
    Path tempDir;

    private static Path repoAssets() {
        Path assets = Path.of(System.getProperty("vertexforge.repoRoot", "..")).toAbsolutePath().normalize()
                .resolve("assets");
        assertTrue(Files.isDirectory(assets.resolve("templates/empty")), assets + " has no templates/empty");
        return assets;
    }

    /** Every file below {@code dir} ('/'-separated, relative) with its bytes. */
    private static Map<String, byte[]> tree(Path dir) throws IOException {
        Map<String, byte[]> files = new TreeMap<>();
        try (Stream<Path> paths = Files.walk(dir)) {
            for (Path path : paths.filter(Files::isRegularFile).toList()) {
                files.put(dir.relativize(path).toString().replace('\\', '/'), Files.readAllBytes(path));
            }
        }
        return files;
    }

    @Test
    void aProjectFromTheRepositorysContentCarriesTheEnginesLibraryAndExamples() throws IOException {
        Path assets = repoAssets();
        EngineContent engine = new EngineContent(Optional.of(UUID.randomUUID()), "1.0.0", assets.resolve("templates"),
                assets.resolve("scripts/lib"), Optional.of(assets.resolve("scripts/game/examples")),
                Optional.of(assets.resolve("vfx/library")), "repo", "", false);
        Path parent = Files.createDirectory(tempDir.resolve("parent"));
        String name = "Tom & Jerry's {Game}";

        ProjectCreationResult result = new ProjectCreator(TestSupport.fixedClock(),
                TestSupport.log(tempDir.resolve(".logs")).log).create(new CreateRequest(name, parent, engine, "empty",
                Set.of("examples", "vfxLibrary")), CreationMonitor.NONE);

        assertTrue(result.success(), result::errorMessage);
        Path project = result.projectPath().getParent();

        // AC: scripts/lib is byte-identical to the engine's library (plus the launcher's own .vflib.json).
        Map<String, byte[]> engineLibrary = tree(assets.resolve("scripts/lib"));
        Map<String, byte[]> projectLibrary = tree(project.resolve("assets/scripts/lib"));
        assertTrue(projectLibrary.remove(LibraryManifest.FILE_NAME) != null, "the library manifest is written");
        assertEquals(engineLibrary.keySet(), projectLibrary.keySet());
        for (Map.Entry<String, byte[]> file : engineLibrary.entrySet()) {
            assertArrayEquals(file.getValue(), projectLibrary.get(file.getKey()), file.getKey());
        }

        // AC: with examples on, lib/engine/oop/Behaviour.mt and game/examples/PlayerMovement.mt are present.
        assertTrue(Files.isRegularFile(project.resolve("assets/scripts/lib/engine/oop/Behaviour.mt")));
        assertTrue(Files.isRegularFile(project.resolve("assets/scripts/game/examples/PlayerMovement.mt")));
        assertTrue(Files.isRegularFile(project.resolve("assets/vfx/library/sparks.vfVFX")));
        assertTrue(Files.isRegularFile(project.resolve(".gitignore")));

        JsonObject vfproj = JsonParser.parseString(Files.readString(result.projectPath())).getAsJsonObject();
        assertEquals("VertexForge 1.0.0", vfproj.get("engineVersion").getAsString());
        assertEquals("assets", vfproj.get("workingDirectory").getAsString());
        assertEquals("scenes/Main.vfScene", vfproj.get("startupScene").getAsString());

        String mtproj = Files.readString(project.resolve("assets/scripts/scripts.mtproj"));
        assertTrue(mtproj.contains("Name=\"Tom &amp; Jerry's {Game}\""), mtproj);
        String main = Files.readString(project.resolve("assets/scripts/game/Main.mt"));
        assertTrue(main.contains("\"Hello from Tom & Jerry's \\{Game\\}!\""), main);
        assertFalse(main.contains("{{PROJECT_NAME}}") || mtproj.contains("{{PROJECT_NAME}}"));

        JsonObject library = JsonParser.parseString(Files.readString(
                project.resolve("assets/scripts/lib").resolve(LibraryManifest.FILE_NAME))).getAsJsonObject();
        assertEquals(engineLibrary.size(), library.getAsJsonObject("files").size());
        for (Map.Entry<String, byte[]> file : engineLibrary.entrySet()) {
            assertEquals(LibraryManifest.hash(file.getValue()),
                    library.getAsJsonObject("files").get(file.getKey()).getAsString(), file.getKey());
        }
    }
}
