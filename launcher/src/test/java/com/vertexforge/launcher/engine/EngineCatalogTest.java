package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class EngineCatalogTest {

    @TempDir
    Path tempDir;

    private Path root;
    private Path configDir;
    private CapturedLog log;
    private final List<EngineCatalog> catalogs = new ArrayList<>();

    @BeforeEach
    void setUp() {
        root = TestSupport.real(tempDir);
        configDir = root.resolve("config");
        log = TestSupport.log(root.resolve("logs"));
    }

    @AfterEach
    void tearDown() {
        catalogs.forEach(EngineCatalog::close);
    }

    private EngineCatalog catalog(Optional<Path> launcherExecutable, Map<String, String> variables) {
        EngineCatalog catalog = EngineCatalog.create(TestSupport.environment(configDir, launcherExecutable,
                TestSupport.fixedClock(), variables, builder -> {
                    throw new IOException("no processes in this test");
                }), log.log);
        catalogs.add(catalog);
        return catalog;
    }

    private static EngineOptions check(EngineCatalog catalog) throws Exception {
        return catalog.check(catalog.load()).get(10, TimeUnit.SECONDS);
    }

    @Test
    void withoutALauncherExeOnlyTheRegistryCounts() throws Exception {
        EngineOptions options = check(catalog(Optional.empty(), Map.of()));

        assertTrue(options.engines().isEmpty());
        assertFalse(options.anyUsable());
        assertFalse(Files.exists(configDir.resolve(EngineRegistryRepository.FILE_NAME)), "nothing to register");
    }

    @Test
    void thePackagedLaunchersEngineIsRegisteredOnceAndMarkedBundled() throws Exception {
        Path launcher = TestSupport.packagedLauncher(root.resolve("dist/VertexForge"));
        EngineCatalog catalog = catalog(Optional.of(launcher), Map.of());

        EngineOptions first = check(catalog);
        byte[] written = Files.readAllBytes(catalog.file());
        EngineOptions second = check(catalog(Optional.of(launcher), Map.of()));

        EngineOption bundled = first.bundled().orElseThrow();
        assertTrue(bundled.id().isPresent(), "registered, so projects can be bound to it");
        assertTrue(bundled.isOk(), bundled::toString);
        assertEquals(second.bundled().orElseThrow().id(), bundled.id());
        assertEquals(1, second.engines().size());
        assertArrayEquals(written, Files.readAllBytes(catalog.file()), "a registered bundled engine is not rewritten");
    }

    @Test
    void aPackagedImageWithoutAnEngineBesideItRegistersNothing() throws Exception {
        Path image = Files.createDirectories(root.resolve("launcher/target/dist/VertexForge Launcher"));
        Path launcher = Files.writeString(image.resolve("VertexForge Launcher.exe"), "fake");

        EngineOptions options = check(catalog(Optional.of(launcher), Map.of()));

        assertEquals(Optional.empty(), options.bundled());
        assertFalse(Files.exists(configDir.resolve(EngineRegistryRepository.FILE_NAME)));
    }

    @Test
    void aReadOnlyRegistryStillOffersTheBundledEngineUnsaved() throws Exception {
        Path launcher = TestSupport.packagedLauncher(root.resolve("dist/VertexForge"));
        Files.createDirectories(configDir);
        Path file = Files.writeString(configDir.resolve(EngineRegistryRepository.FILE_NAME),
                "{\"version\": 9, \"engines\": []}");
        byte[] newer = Files.readAllBytes(file);

        EngineOptions options = check(catalog(Optional.of(launcher), Map.of()));

        assertEquals(StoreStatus.NEWER_READ_ONLY, options.registry().status());
        EngineOption bundled = options.bundled().orElseThrow();
        assertEquals(Optional.empty(), bundled.id());
        assertTrue(bundled.isOk());
        assertArrayEquals(newer, Files.readAllBytes(file));
        LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, EngineResolver.resolve(options, null));
        assertEquals(Optional.empty(), ready.bindTo(), "an unsaved engine cannot be bound");
    }

    /** Review finding: registering the bundled engine again must not hide that the damaged file was set aside. */
    @Test
    void aCorruptRegistryIsReportedEvenWhenTheBundledEngineIsRegisteredAgain() throws Exception {
        Path launcher = TestSupport.packagedLauncher(root.resolve("dist/VertexForge"));
        Files.createDirectories(configDir);
        Files.writeString(configDir.resolve(EngineRegistryRepository.FILE_NAME),
                "{\"version\": 1, \"engines\": {\"not\": \"a list\"}}");
        EngineCatalog catalog = catalog(Optional.of(launcher), Map.of());

        EngineListing listing = catalog.load();

        assertEquals(StoreStatus.CORRUPT_QUARANTINED, listing.registry().status());
        Path movedTo = listing.registry().quarantinedTo().orElseThrow();
        assertTrue(Files.isRegularFile(movedTo), movedTo::toString);
        assertEquals(1, listing.registry().engines().size(), "the bundled engine was registered in the new list");
        assertEquals(listing.bundledExecutable(), Optional.of(listing.registry().engines().getFirst().executable()));
        assertFalse(listing.registry().readOnly());
    }

    @Test
    void anAddedEngineIsCheckedWithTheOthers() throws Exception {
        EngineCatalog catalog = catalog(Optional.empty(), Map.of());
        Path editor = TestSupport.devEngine(root.resolve("repo"));
        PickedEngine.Usable usable = assertInstanceOf(PickedEngine.Usable.class,
                catalog.inspect(editor.resolveSibling(EngineManifest.FILE_NAME)).get(10, TimeUnit.SECONDS));

        EngineRegistryRepository.Added added = catalog.add(usable);
        EngineOptions options = check(catalog);

        EngineOption engine = options.find(added.engine().id()).orElseThrow();
        assertEquals(editor, engine.executable());
        assertEquals(root.resolve("repo/VFEngine/editor"), engine.health().workingDirectory());
        assertFalse(engine.bundled());
    }

    @Test
    void theDefaultIsMarked() throws Exception {
        EngineCatalog catalog = catalog(Optional.empty(), Map.of());
        PickedEngine.Usable usable = assertInstanceOf(PickedEngine.Usable.class,
                catalog.inspect(TestSupport.fakeEditor(root.resolve("a"))).get(10, TimeUnit.SECONDS));
        EngineRegistryRepository.Added added = catalog.add(usable);

        catalog.setDefault(added.engine().id());

        assertTrue(check(catalog).find(added.engine().id()).orElseThrow().isDefault());
    }

    @Test
    void anOverrideIsCheckedButNeverListedAsAnEngine() throws Exception {
        Path editor = TestSupport.devEngine(root.resolve("repo"));

        EngineOptions options = check(catalog(Optional.empty(), Map.of(EditorOverride.ENV_VAR, editor.toString())));

        EngineOption override = options.activeOverride().orElseThrow();
        assertEquals(root.resolve("repo/VFEngine/editor"), override.health().workingDirectory(),
                "the override starts in its manifest's working directory too");
        assertTrue(options.engines().isEmpty());
        assertTrue(options.anyUsable());
    }

    @Test
    void anOverrideThatIsNotAnAbsolutePathIsBroken() throws Exception {
        EngineOptions options = check(catalog(Optional.empty(), Map.of(EditorOverride.ENV_VAR, "Editor.exe")));

        EngineOption override = options.override().orElseThrow();
        assertEquals(EngineHealth.Kind.BROKEN, override.health().kind());
        assertTrue(override.health().reason().contains(EditorOverride.ENV_VAR), override.health().reason());
        assertEquals("Editor.exe", override.rawPath());
        assertEquals(Optional.empty(), options.activeOverride());
    }
}
