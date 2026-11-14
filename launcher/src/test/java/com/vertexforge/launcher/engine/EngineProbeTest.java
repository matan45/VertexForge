package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineHealth.Kind;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/** The engine check against fake folders (VK-1681): the packaged layout, the dev layout and installs without a manifest. */
class EngineProbeTest {

    @TempDir
    Path tempDir;

    private Path root;
    private final EngineProbe probe = EngineProbe.system();

    @BeforeEach
    void setUp() {
        root = TestSupport.real(tempDir);
    }

    @Test
    void thePackagedLayoutStartsInTheExeFolder() {
        TestSupport.packagedLauncher(root.resolve("VertexForge"));
        Path exeDir = root.resolve("VertexForge/bin/Editor/Release/x64");

        EngineHealth health = probe.probe(exeDir.resolve("Editor.exe"));

        assertEquals(Kind.OK, health.kind(), health::reason);
        assertEquals(exeDir, health.workingDirectory());
        assertEquals("1.0.0 Release", health.manifest().describe());
    }

    @Test
    void theDevLayoutStartsInTheWorkingDirectoryItsManifestNames() {
        Path editor = TestSupport.devEngine(root.resolve("repo"));

        EngineHealth health = probe.probe(editor);

        assertEquals(Kind.OK, health.kind(), health::reason);
        assertEquals(root.resolve("repo/VFEngine/editor"), health.workingDirectory());
        assertEquals("Development", health.manifest().configuration());
    }

    @Test
    void anInstallWithoutAManifestHasAnUnknownVersionAndStartsInItsOwnFolder() {
        Path editor = TestSupport.fakeEditor(root.resolve("custom/bin"));

        EngineHealth health = probe.probe(editor);

        assertEquals(Kind.OK, health.kind(), health::reason);
        assertNull(health.manifest());
        assertEquals(editor.getParent(), health.workingDirectory());
        assertTrue(health.reason().contains("unknown"), health.reason());
    }

    @Test
    void aDeletedEditorIsBroken() throws IOException {
        Path editor = TestSupport.devEngine(root.resolve("repo"));
        Files.delete(editor);

        EngineHealth health = probe.probe(editor);

        assertEquals(Kind.BROKEN, health.kind());
        assertTrue(health.reason().contains("moved or deleted"), health.reason());
    }

    @Test
    void aDeletedEngineFolderIsBroken() {
        EngineHealth health = probe.probe(root.resolve("gone/bin/Editor/Release/x64/Editor.exe"));

        assertEquals(Kind.BROKEN, health.kind(), health::reason);
    }

    @Test
    void anInvalidManifestIsBrokenNotUnknown() throws IOException {
        Path editor = TestSupport.fakeEditor(root.resolve("bin"));
        Files.writeString(editor.resolveSibling(EngineManifest.FILE_NAME), "{ not json", StandardCharsets.UTF_8);

        EngineHealth health = probe.probe(editor);

        assertEquals(Kind.BROKEN, health.kind());
        assertTrue(health.reason().contains(EngineManifest.FILE_NAME), health.reason());
    }

    @Test
    void aManifestOfAnotherEditorIsBroken() throws IOException {
        Path exeDir = root.resolve("bin");
        TestSupport.writeManifest(exeDir, "Release", ".");
        Path other = Files.writeString(exeDir.resolve("Runtime.exe"), "fake");
        other.toFile().setExecutable(true);

        EngineHealth health = probe.probe(other);

        assertEquals(Kind.BROKEN, health.kind());
        assertNotNull(health.manifest(), "the manifest is kept so the version can still be shown");
    }

    @Test
    void theEditorNameIsComparedWithoutRegardToCase() throws IOException {
        Path exeDir = root.resolve("bin");
        TestSupport.writeManifest(exeDir, "Release", ".");
        Path editor = Files.writeString(exeDir.resolve("editor.EXE"), "fake");
        editor.toFile().setExecutable(true);

        assertEquals(Kind.OK, probe.probe(editor).kind());
    }

    @Test
    void aMissingWorkingDirectoryIsBroken() {
        Path exeDir = root.resolve("repo/bin/Editor/Debug/x64");
        Path editor = TestSupport.fakeEditor(exeDir);
        TestSupport.writeManifest(exeDir, "Debug", "../../../../VFEngine/editor");

        EngineHealth health = probe.probe(editor);

        assertEquals(Kind.BROKEN, health.kind());
        assertTrue(health.reason().contains("working directory"), health.reason());
        assertEquals("1.0.0 Debug", health.manifest().describe());
    }

    @Test
    void aFolderNamedEditorExeIsBroken() throws IOException {
        Path folder = Files.createDirectories(root.resolve("bin/Editor.exe"));

        assertEquals(Kind.BROKEN, probe.probe(folder).kind());
    }

    @Test
    void anEngineOnADriveThatIsNotThereIsMissing() {
        Optional<Path> absent = TestSupport.absentDriveRoot();
        assumeTrue(absent.isPresent(), "needs Windows and a free drive letter");

        EngineHealth health = probe.probe(absent.get().resolve("VertexForge/bin/Editor/Release/x64/Editor.exe"));

        assertEquals(Kind.MISSING, health.kind(), health::reason);
        assertTrue(health.reason().contains("is not available"), health.reason());
    }

    @Test
    void aRelativePathIsBroken() {
        assertEquals(Kind.BROKEN, probe.probe(Path.of("Editor.exe")).kind());
        assertEquals(Kind.BROKEN, probe.probe(null).kind());
    }

    // -----------------------------------------------------------------------------------------------------------
    // Add Engine…
    // -----------------------------------------------------------------------------------------------------------

    @Test
    void pickingTheManifestAddsTheEditorBesideIt() {
        Path editor = TestSupport.devEngine(root.resolve("repo"));

        PickedEngine picked = EngineProbe.inspectPicked(editor.resolveSibling(EngineManifest.FILE_NAME), probe);

        PickedEngine.Usable usable = assertInstanceOf(PickedEngine.Usable.class, picked, picked::toString);
        assertEquals(editor, usable.realExecutable());
        assertEquals(root.resolve("repo/VFEngine/editor"), usable.health().workingDirectory());
    }

    @Test
    void pickingTheEditorWorksWithOrWithoutAManifest() {
        Path dev = TestSupport.devEngine(root.resolve("repo"));
        Path plain = TestSupport.fakeEditor(root.resolve("plain"));

        assertInstanceOf(PickedEngine.Usable.class, EngineProbe.inspectPicked(dev, probe));
        PickedEngine.Usable usable = assertInstanceOf(PickedEngine.Usable.class,
                EngineProbe.inspectPicked(plain, probe));
        assertNull(usable.health().manifest());
    }

    @Test
    void theRealPathIsWhatGetsRegistered() {
        Path editor = TestSupport.devEngine(root.resolve("repo"));
        Path roundabout = root.resolve("repo/bin/Editor/../Editor/Development/x64/Editor.exe");

        PickedEngine.Usable usable = assertInstanceOf(PickedEngine.Usable.class,
                EngineProbe.inspectPicked(roundabout, probe));
        assertEquals(editor, usable.realExecutable());
    }

    @Test
    void aManifestWhoseEditorIsNotBesideItIsRefused() {
        Path exeDir = root.resolve("bin");
        Path manifest = TestSupport.writeManifest(exeDir, "Release", ".");

        PickedEngine.Refused refused = assertInstanceOf(PickedEngine.Refused.class,
                EngineProbe.inspectPicked(manifest, probe));
        assertTrue(refused.reason().contains("was not found"), refused.reason());
    }

    @Test
    void aBrokenEngineOrAnotherFileIsRefused() throws IOException {
        Path exeDir = root.resolve("repo/bin/Editor/Debug/x64");
        Path editor = TestSupport.fakeEditor(exeDir);
        TestSupport.writeManifest(exeDir, "Debug", "../../../../VFEngine/editor");
        Path text = Files.writeString(root.resolve("notes.txt"), "x");
        Path otherJson = Files.writeString(root.resolve("manifest.json"), "{}");

        assertInstanceOf(PickedEngine.Refused.class, EngineProbe.inspectPicked(editor, probe));
        assertInstanceOf(PickedEngine.Refused.class, EngineProbe.inspectPicked(text, probe));
        assertInstanceOf(PickedEngine.Refused.class, EngineProbe.inspectPicked(root.resolve("gone.exe"), probe));
        assertInstanceOf(PickedEngine.Refused.class, EngineProbe.inspectPicked(otherJson, probe));
        Path badManifest = Files.writeString(Files.createDirectories(root.resolve("bad")).resolve("engine.json"), "{}");
        PickedEngine.Refused refused = assertInstanceOf(PickedEngine.Refused.class,
                EngineProbe.inspectPicked(badManifest, probe));
        assertTrue(refused.reason().contains("not a valid engine manifest"), refused.reason());
    }
}
