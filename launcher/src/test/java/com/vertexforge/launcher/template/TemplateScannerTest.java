package com.vertexforge.launcher.template;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineContent;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/** {@link TemplateScanner#SYSTEM} over a fake engine's folders, and the blueprints its catalog builds (VK-1689). */
class TemplateScannerTest {

    @TempDir
    Path tempDir;

    private EngineContent engine;

    @BeforeEach
    void setUp() {
        engine = TestSupport.engineContent(tempDir);
    }

    private EngineContent with(Path templates, Path library, Optional<Path> examples, Optional<Path> vfx,
                               String version) {
        return new EngineContent(engine.engineId(), version, templates, library, examples, vfx, "engine", "", false);
    }

    private static List<String> relative(List<TemplateCatalog.FileEntry> files) {
        return files.stream().map(TemplateCatalog.FileEntry::relative).toList();
    }

    @Test
    void readsTheLibraryTheTemplatesAndTheOptionFolders() throws IOException {
        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(engine);

        assertEquals(List.of("core/Array.mt", "engine/Log.mt", "engine/oop/Behaviour.mt"), relative(catalog.library()));
        assertEquals(List.of("empty"), catalog.templates().stream().map(TemplateCatalog.Template::id).toList());
        assertEquals(List.of(".gitignore", "assets/scripts/game/Main.mt", "assets/scripts/scripts.mtproj"),
                relative(catalog.templates().getFirst().payload()));
        assertEquals(List.of("PlayerMovement.mt"), relative(catalog.optionFiles().get(TemplateOptionKind.EXAMPLES)));
        assertEquals(List.of("sparks.vfVFX"), relative(catalog.optionFiles().get(TemplateOptionKind.VFX_LIBRARY)));
        assertEquals(List.of(), catalog.notes());
    }

    @Test
    void theBlueprintListsTheTemplateThenTheLibraryThenTheOptions() throws Exception {
        ProjectBlueprint blueprint = TemplateScanner.SYSTEM.scan(engine).blueprint("empty",
                Set.of("vfxLibrary", "examples"));

        assertEquals(List.of(".gitignore", "assets/scripts/game/Main.mt", "assets/scripts/scripts.mtproj",
                "assets/scripts/lib/core/Array.mt", "assets/scripts/lib/engine/Log.mt",
                "assets/scripts/lib/engine/oop/Behaviour.mt", "assets/scripts/game/examples/PlayerMovement.mt",
                "assets/vfx/library/sparks.vfVFX"), blueprint.entries().stream().map(ProjectBlueprint.Entry::target)
                .toList());
        assertEquals(List.of(Placeholders.Fill.NONE, Placeholders.Fill.MTYPE_STRING, Placeholders.Fill.XML_ATTRIBUTE,
                Placeholders.Fill.NONE, Placeholders.Fill.NONE, Placeholders.Fill.NONE, Placeholders.Fill.NONE,
                Placeholders.Fill.NONE), blueprint.entries().stream().map(ProjectBlueprint.Entry::fill).toList());
        assertEquals("engine/Log.mt", blueprint.entries().get(4).libraryPath());
        assertEquals(null, blueprint.entries().getLast().libraryPath(), "only library files are hashed");
        assertEquals("assets/scripts/lib/.vflib.json", blueprint.libraryManifestTarget());
        assertEquals(1 + 8 + 1, blueprint.fileCount());
    }

    @Test
    void aMissingLibraryOrTemplatesFolderFailsTheWholeRead() {
        IOException noLibrary = assertThrows(IOException.class, () -> TemplateScanner.SYSTEM.scan(with(
                engine.templatesDir(), tempDir.resolve("nope"), Optional.empty(), Optional.empty(), "1.0.0")));
        assertTrue(noLibrary.getMessage().startsWith("The script library "), noLibrary.getMessage());

        IOException noTemplates = assertThrows(IOException.class, () -> TemplateScanner.SYSTEM.scan(with(
                tempDir.resolve("nope"), engine.scriptLibDir(), Optional.empty(), Optional.empty(), "1.0.0")));
        assertTrue(noTemplates.getMessage().startsWith("The templates folder "), noTemplates.getMessage());
    }

    @Test
    void anEmptyLibraryFailsTheRead() throws IOException {
        Path empty = Files.createDirectory(tempDir.resolve("empty-lib"));
        Files.writeString(empty.resolve(LibraryManifest.FILE_NAME), "{}");

        IOException e = assertThrows(IOException.class, () -> TemplateScanner.SYSTEM.scan(with(engine.templatesDir(),
                empty, Optional.empty(), Optional.empty(), "1.0.0")));
        assertTrue(e.getMessage().endsWith("has no files."), e.getMessage());
    }

    @Test
    void anEnginesOwnLibraryManifestIsNeverCopied() throws IOException {
        Files.writeString(engine.scriptLibDir().resolve(LibraryManifest.FILE_NAME), "{}");

        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(engine);

        assertEquals(3, catalog.library().size());
        assertTrue(catalog.notes().getFirst().contains(LibraryManifest.FILE_NAME), catalog.notes().toString());
    }

    @Test
    void anUnusableTemplateIsSkippedWithItsReason() throws IOException {
        Path templates = engine.templatesDir();
        Files.createDirectories(templates.resolve("no-manifest/project"));
        Files.createDirectories(templates.resolve("wrong-id/project"));
        Files.writeString(templates.resolve("wrong-id/template.json"),
                Files.readString(templates.resolve("empty/template.json")));
        Files.createDirectories(templates.resolve("newer"));
        Files.writeString(templates.resolve("newer/template.json"),
                Files.readString(templates.resolve("empty/template.json")).replace("\"empty\"", "\"newer\"")
                        .replace("\"1.0.0\"", "\"9.0.0\""));
        Files.createDirectories(templates.resolve("with-project-file/project"));
        Files.writeString(templates.resolve("with-project-file/template.json"),
                Files.readString(templates.resolve("empty/template.json")).replace("\"empty\"", "\"with-project-file\""));
        Files.writeString(templates.resolve("with-project-file/project/Game.vfproj"), "{}");

        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(engine);

        assertEquals(List.of("empty"), catalog.templates().stream().map(TemplateCatalog.Template::id).toList());
        String notes = String.join("\n", catalog.notes());
        assertTrue(notes.contains("\"no-manifest\" was skipped: it has no template.json."), notes);
        assertTrue(notes.contains("\"wrong-id\" was skipped: its id \"empty\" is not its folder's name."), notes);
        assertTrue(notes.contains("\"newer\" was skipped: it needs VertexForge 9.0.0 or newer."), notes);
        assertTrue(notes.contains("the launcher writes the project file itself"), notes);
    }

    @Test
    void anOptionWithoutItsEngineFolderIsUnavailableWithTheReason() throws Exception {
        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(with(engine.templatesDir(), engine.scriptLibDir(),
                Optional.empty(), Optional.of(tempDir.resolve("gone")), "1.0.0"));

        assertEquals(Optional.of("this engine's engine.json names no examplesDir folder."),
                catalog.unavailableReason(TemplateOptionKind.EXAMPLES));
        assertTrue(catalog.unavailableReason(TemplateOptionKind.VFX_LIBRARY).orElseThrow().contains("does not exist"));
        TemplateException e = assertThrows(TemplateException.class,
                () -> catalog.blueprint("empty", Set.of("examples")));
        assertTrue(e.getMessage().startsWith("\"Include examples\" cannot be added: "), e.getMessage());
        // Without the option, the template still works.
        assertEquals(8, catalog.blueprint("empty", Set.of()).fileCount());
    }

    @Test
    void anOptionTheTemplateDoesNotOfferIsRefused() throws IOException {
        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(engine);

        TemplateException e = assertThrows(TemplateException.class, () -> catalog.blueprint("empty", Set.of("x")));
        assertEquals("The template \"empty\" has no option \"x\".", e.getMessage());
    }

    @Test
    void twoFilesThatWouldShareAPathAreRefused() throws IOException {
        // The template's own copy of a library file would collide with the library's (Windows ignores case).
        Path payloadCopy = engine.templatesDir().resolve("empty/project/assets/scripts/lib/Core/array.mt");
        Files.createDirectories(payloadCopy.getParent());
        Files.writeString(payloadCopy, "mine");

        TemplateException e = assertThrows(TemplateException.class,
                () -> TemplateScanner.SYSTEM.scan(engine).blueprint("empty", Set.of()));
        assertTrue(e.getMessage().contains("would both be"), e.getMessage());
    }

    @Test
    void aTemplateScriptThatIsNotUtf8IsRefused() throws IOException {
        Files.write(engine.templatesDir().resolve("empty/project/assets/scripts/game/Main.mt"),
                new byte[]{'"', (byte) 0xE9, '"'});

        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(engine);

        assertTrue(catalog.templates().isEmpty());
        assertTrue(catalog.notes().getFirst().contains("is not valid UTF-8 text"), catalog.notes().toString());
    }

    @Test
    void aLinkInsideALibraryIsLeftOutAndNeverFollowed() throws IOException {
        Path elsewhere = Files.writeString(tempDir.resolve("elsewhere.mt"), "outside");
        try {
            Files.createSymbolicLink(engine.scriptLibDir().resolve("link.mt"), elsewhere);
        } catch (IOException | UnsupportedOperationException | SecurityException e) {
            assumeTrue(false, "this machine cannot create symbolic links: " + e);
        }

        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(engine);

        assertEquals(3, catalog.library().size());
        assertTrue(catalog.notes().getFirst().contains("link.mt is a link"), catalog.notes().toString());
    }

    @Test
    void theRepositorysOwnContentScans() throws Exception {
        Path repo = Path.of(System.getProperty("vertexforge.repoRoot", "..")).toAbsolutePath().normalize();
        EngineContent real = with(repo.resolve("assets/templates"), repo.resolve("assets/scripts/lib"),
                Optional.of(repo.resolve("assets/scripts/game/examples")), Optional.of(repo.resolve("assets/vfx/library")),
                "1.0.0");

        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(real);

        assertEquals(List.of(), catalog.notes());
        assertEquals("empty", catalog.templates().getFirst().id());
        ProjectBlueprint blueprint = catalog.blueprint("empty", Set.of("examples", "vfxLibrary"));
        assertTrue(blueprint.entries().stream().anyMatch(e -> e.target().equals("assets/scripts/lib/engine/oop/Behaviour.mt")));
        assertTrue(blueprint.entries().stream().anyMatch(
                e -> e.target().equals("assets/scripts/game/examples/PlayerMovement.mt")));
    }
}
