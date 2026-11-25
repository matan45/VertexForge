package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Which engines the Create Project dialog offers and preselects (VK-1689), and {@link EngineContent#of}. */
class EngineChoiceTest {

    private static final Path ROOT = Path.of("C:/engines").toAbsolutePath();
    private static final UUID A = UUID.fromString("00000000-0000-0000-0000-00000000000a");
    private static final UUID B = UUID.fromString("00000000-0000-0000-0000-00000000000b");

    private static EngineManifest manifest(String text) {
        try {
            return EngineManifest.parse(text);
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    /** A dev manifest: templatesDir, scriptLibDir, examplesDir and vfxLibraryDir all set. */
    private static final EngineManifest WITH_CONTENT = manifest("""
            {"manifestVersion": 1, "engineVersion": "1.0.0", "configuration": "Development",
             "editorExe": "Editor.exe", "workingDirectory": "../../../../VFEngine/editor",
             "templatesDir": "../../../../assets/templates", "scriptLibDir": "../../../../assets/scripts/lib",
             "examplesDir": "../../../../assets/scripts/game/examples", "vfxLibraryDir": "../../../../assets/vfx/library"}
            """);
    /** An engine.json from before VK-1689: no content folders. */
    private static final EngineManifest WITHOUT_CONTENT = manifest("""
            {"manifestVersion": 1, "engineVersion": "0.9.0", "configuration": "Release",
             "editorExe": "Editor.exe", "workingDirectory": "."}
            """);

    private static EngineOption engine(UUID id, EngineHealth health, boolean bundled, boolean isDefault) {
        Path exe = ROOT.resolve(id == null ? "unsaved" : id.toString()).resolve("bin/Editor/Development/x64/Editor.exe");
        return new EngineOption(Optional.ofNullable(id), exe, exe.toString(), health, bundled, isDefault, false);
    }

    private static EngineOption override(EngineHealth health) {
        Path exe = ROOT.resolve("override/bin/Editor/Development/x64/Editor.exe");
        return new EngineOption(Optional.empty(), exe, exe.toString(), health, false, false, true);
    }

    private static EngineHealth ok(EngineManifest manifest) {
        return EngineHealth.ok(manifest, ROOT);
    }

    private static EngineOptions options(UUID defaultId, EngineOption override, EngineOption... engines) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.ofNullable(defaultId), StoreStatus.OK,
                Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.ofNullable(override), Arrays.asList(engines));
    }

    @Test
    void theContentFoldersResolveFromTheManifestsFolder() {
        EngineOption engine = engine(A, ok(WITH_CONTENT), false, false);

        EngineContent content = EngineContent.of(engine).orElseThrow();

        Path repo = ROOT.resolve(A.toString());
        assertEquals(Optional.of(A), content.engineId());
        assertEquals("1.0.0", content.engineVersion());
        assertEquals("VertexForge 1.0.0", content.displayVersion());
        assertEquals(repo.resolve("assets/templates"), content.templatesDir());
        assertEquals(repo.resolve("assets/scripts/lib"), content.scriptLibDir());
        assertEquals(Optional.of(repo.resolve("assets/scripts/game/examples")), content.examplesDir());
        assertEquals(Optional.of(repo.resolve("assets/vfx/library")), content.vfxLibraryDir());
    }

    @Test
    void anEngineWithoutContentOrThatDoesNotWorkOffersNone() {
        EngineOption old = engine(A, ok(WITHOUT_CONTENT), false, false);
        EngineOption noManifest = engine(A, ok(null), false, false);
        EngineOption broken = engine(A, EngineHealth.broken("gone", WITH_CONTENT), false, false);

        assertEquals(Optional.empty(), EngineContent.of(old));
        assertTrue(EngineContent.unavailableReason(old).orElseThrow().contains("names no templates folder"));
        assertEquals(Optional.empty(), EngineContent.of(noManifest));
        assertTrue(EngineContent.unavailableReason(noManifest).orElseThrow().contains("has no engine.json"));
        assertEquals(Optional.empty(), EngineContent.of(broken));
        assertTrue(EngineContent.unavailableReason(broken).orElseThrow().startsWith("it is broken: gone"));
    }

    @Test
    void onlyWorkingEnginesWithContentAreOfferedTheOverrideFirst() {
        EngineOption withContent = engine(A, ok(WITH_CONTENT), false, false);
        EngineOption old = engine(B, ok(WITHOUT_CONTENT), false, false);
        EngineOption override = override(ok(WITH_CONTENT));

        List<EngineChoice> choices = EngineChoice.from(options(null, override, withContent, old));

        assertEquals(List.of(override, withContent), choices.stream().map(EngineChoice::option).toList());
        assertTrue(choices.getFirst().content().override());
        assertEquals(Optional.empty(), choices.getFirst().content().engineId(), "the override is never bound");
    }

    @Test
    void theEngineANewProjectWouldOpenWithIsPreselected() {
        EngineOption a = engine(A, ok(WITH_CONTENT), false, false);
        EngineOption b = engine(B, ok(WITH_CONTENT), false, true);
        EngineOptions withDefault = options(B, null, a, b);

        assertEquals(b, EngineChoice.preselect(withDefault, EngineChoice.from(withDefault)).orElseThrow().option());

        EngineOptions noDefault = options(null, null, a, b);
        assertEquals(Optional.empty(), EngineChoice.preselect(noDefault, EngineChoice.from(noDefault)),
                "two engines and no default: the user chooses");

        EngineOption old = engine(B, ok(WITHOUT_CONTENT), false, true);
        EngineOptions defaultWithoutContent = options(B, null, a, old);
        assertEquals(a, EngineChoice.preselect(defaultWithoutContent, EngineChoice.from(defaultWithoutContent))
                .orElseThrow().option(), "the only engine that can create projects");
    }

    @Test
    void theReasonNamesWhatIsMissing() {
        assertEquals(EngineResolver.NO_ENGINE, EngineChoice.noChoiceReason(options(null, null)));

        String reason = EngineChoice.noChoiceReason(options(null, null, engine(A, ok(WITHOUT_CONTENT), false, false)));
        assertTrue(reason.startsWith("None of the working engines can create projects:"), reason);
        assertTrue(reason.contains("0.9.0 Release"), reason);
        assertTrue(reason.contains("names no templates folder"), reason);
    }
}
