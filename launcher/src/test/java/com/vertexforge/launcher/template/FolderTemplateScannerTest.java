package com.vertexforge.launcher.template;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineContent;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * VK-1691's change to {@link FolderTemplateScanner} (through {@link TemplateScanner#SYSTEM}): the editor's sidecars in
 * an engine's script library belong to the project they were made in, so a new project never copies them, and the
 * library record never lists them.
 */
class FolderTemplateScannerTest {

    @TempDir
    Path tempDir;

    private static List<String> relative(List<TemplateCatalog.FileEntry> files) {
        return files.stream().map(TemplateCatalog.FileEntry::relative).toList();
    }

    @Test
    void theEnginesSidecarsAreLeftOutOfTheLibraryWithANote() throws IOException {
        EngineContent engine = TestSupport.engineContent(tempDir);
        Files.writeString(engine.scriptLibDir().resolve("engine/Log.mt.vfmeta"), "{\"guid\": \"engine's\"}");
        Files.writeString(engine.scriptLibDir().resolve("core/Array.MT.VFMETA"), "{}");

        TemplateCatalog catalog = TemplateScanner.SYSTEM.scan(engine);

        assertEquals(List.of("core/Array.mt", "engine/Log.mt", "engine/oop/Behaviour.mt"), relative(catalog.library()));
        assertEquals(List.of("The script library's editor files (.vfmeta) were left out."), catalog.notes());
    }

    @Test
    void aLibraryOfNothingButSidecarsHasNoFiles() throws IOException {
        Path library = Files.createDirectories(tempDir.resolve("lib-of-sidecars"));
        Files.writeString(library.resolve("Log.mt.vfmeta"), "{}");
        EngineContent base = TestSupport.engineContent(tempDir);
        EngineContent engine = new EngineContent(base.engineId(), base.engineVersion(), base.templatesDir(), library,
                base.examplesDir(), base.vfxLibraryDir(), "engine", "", false);

        IOException e = assertThrows(IOException.class, () -> TemplateScanner.SYSTEM.scan(engine));

        assertTrue(e.getMessage().endsWith("has no files."), e.getMessage());
    }

    @Test
    void theBlueprintOfANewProjectCopiesNoSidecar() throws Exception {
        EngineContent engine = TestSupport.engineContent(tempDir);
        Files.writeString(engine.scriptLibDir().resolve("engine/Log.mt.vfmeta"), "{}");

        ProjectBlueprint blueprint = TemplateScanner.SYSTEM.scan(engine).blueprint("empty", java.util.Set.of());

        assertTrue(blueprint.entries().stream().noneMatch(entry -> LibraryManifest.isSidecar(entry.target())),
                blueprint.entries().toString());
    }
}
