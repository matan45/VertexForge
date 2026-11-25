package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.engine.EngineCatalog;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.ProjectStatus.Kind;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import com.vertexforge.launcher.service.EditorLauncherTest.RecordingStarter;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.session.ManualScheduler;
import com.vertexforge.launcher.session.SessionLogs;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalLong;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The background checks (VK-1678) and the status they turn into (VK-1686). */
class ProjectHealthServiceTest {

    private static final Duration TIMEOUT = Duration.ofMillis(200);

    /** A lock probe for checks that are not about the lock: no editor holds any folder. */
    private static final Function<Path, EditorLockState> NO_LOCK = folder -> EditorLockState.FREE;

    private static final Path ROOT = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath();
    private static final UUID A = UUID.fromString("5b0c9d1e-7f0a-4d3b-9a51-2f6f1c0e8a42");
    private static final UUID B = UUID.fromString("c3a8e1f2-0b6d-4e5a-8f7c-1d2e3f4a5b6c");

    @TempDir
    Path tempDir;

    /** Never released during a test: stands for a network share that does not answer. */
    private final CountDownLatch shareAnswers = new CountDownLatch(1);
    private final AtomicInteger probeCalls = new AtomicInteger();
    private ProjectHealthService health;

    /** A probe that blocks like a file-system call on an unreachable share. */
    private ProjectFileCheck blockingProbe(Path path) {
        probeCalls.incrementAndGet();
        try {
            shareAnswers.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        return readable("1.0", "VertexForge 1.0.0", EditorLockState.FREE);
    }

    @AfterEach
    void tearDown() {
        shareAnswers.countDown();
        if (health != null) {
            health.close();
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The background checks
    // ---------------------------------------------------------------------------------------------------------------

    @Test
    void twentyUnreachableEntriesListInstantlyAndEndUnreadable() throws Exception {
        health = new ProjectHealthService(this::blockingProbe, NO_LOCK, TIMEOUT);
        Path root = TestSupport.real(tempDir);
        CapturedLog log = TestSupport.log(root.resolve("logs"));
        RecentProjectsRepository repository = new RecentProjectsRepository(
                RecentProjectsRepository.openStore(root.resolve("config/recent-projects.json"), TestSupport.fixedClock(),
                        log.log), UUID::randomUUID, TestSupport.fixedClock(), log.log);
        for (int i = 0; i < 20; i++) {
            repository.add(root.resolve("share").resolve("P" + i).resolve("P" + i + ".vfproj"), "P" + i, 50);
        }
        ProjectHub hub = new ProjectHub(repository, new ProjectCreator(TestSupport.fixedClock(), log.log),
                EngineCatalog.create(TestSupport.environment(root.resolve("config"), TestSupport.fixedClock(),
                        Map.of(), new RecordingStarter(FakeProcess.running(), null)), log.log),
                new EditorLauncher(new RecordingStarter(FakeProcess.running(), null),
                        new SessionLogs(root.resolve("config/logs"), message -> {
                        }), StandardCharsets.UTF_8, TestSupport.fixedClock(), new ManualScheduler()),
                health, 50, TestSupport.fixedClock());

        // The window lists the projects without touching any of them...
        RecentProjectsView view = assertTimeoutPreemptively(Duration.ofSeconds(1), hub::listProjects);
        assertEquals(20, view.entries().size());
        assertEquals(0, probeCalls.get(), "listing must never probe a project");

        // ...and starting the checks returns at once, however long the share takes.
        List<CompletableFuture<ProjectFileCheck>> checks = assertTimeoutPreemptively(Duration.ofSeconds(1),
                () -> view.entries().stream().map(entry -> hub.checkProject(entry, true)).toList());

        CompletableFuture.allOf(checks.toArray(CompletableFuture[]::new)).get(10, TimeUnit.SECONDS);
        for (CompletableFuture<ProjectFileCheck> check : checks) {
            ProjectFileCheck.Unreadable unreadable = assertInstanceOf(ProjectFileCheck.Unreadable.class, check.join());
            assertTrue(unreadable.reason().contains("No response after 200 ms"), unreadable.reason());
        }
        assertEquals(20, probeCalls.get());
    }

    @Test
    void aPathStillBeingCheckedIsNotProbedAgain() throws Exception {
        health = new ProjectHealthService(this::blockingProbe, NO_LOCK, TIMEOUT);
        Path project = tempDir.resolve("Stuck.vfproj");

        ProjectFileCheck first = health.check(project, true).get(10, TimeUnit.SECONDS);
        ProjectFileCheck second = health.check(project, true).get(10, TimeUnit.SECONDS);

        assertInstanceOf(ProjectFileCheck.Unreadable.class, first);
        assertInstanceOf(ProjectFileCheck.Unreadable.class, second);
        assertEquals(1, probeCalls.get(), "a Refresh while the share is down must reuse the stuck probe");
        assertEquals(1, health.inFlightCount());
    }

    @Test
    void aProbeThatAnswersLeavesNothingBehindSoTheNextCheckProbesAgain() throws Exception {
        AtomicInteger calls = new AtomicInteger();
        health = new ProjectHealthService(path -> {
            calls.incrementAndGet();
            return new ProjectFileCheck.Missing("gone");
        }, NO_LOCK, Duration.ofSeconds(5));
        Path project = tempDir.resolve("Gone.vfproj");

        assertInstanceOf(ProjectFileCheck.Missing.class, health.check(project, true).get(10, TimeUnit.SECONDS));
        assertInstanceOf(ProjectFileCheck.Missing.class, health.check(project, true).get(10, TimeUnit.SECONDS));

        assertEquals(2, calls.get());
        assertEquals(0, health.inFlightCount());
    }

    @Test
    void theLockIsProbedOnlyWhenAskedAndOnlyForAFileThatWasRead() throws Exception {
        List<Path> lockProbes = new CopyOnWriteArrayList<>();
        Path game = tempDir.resolve("Game").resolve("Game.vfproj");
        Path gone = tempDir.resolve("Gone").resolve("Gone.vfproj");
        health = new ProjectHealthService(
                path -> path.equals(gone) ? new ProjectFileCheck.Missing("gone")
                        : readable("1.0", "VertexForge 1.0.0", EditorLockState.SKIPPED),
                folder -> {
                    lockProbes.add(folder);
                    return new EditorLockState.Held(OptionalLong.of(9));
                }, Duration.ofSeconds(5));

        ProjectFileCheck withLock = health.check(game, true).get(10, TimeUnit.SECONDS);
        ProjectFileCheck withoutLock = health.check(game, false).get(10, TimeUnit.SECONDS);
        health.check(gone, true).get(10, TimeUnit.SECONDS);

        assertEquals(new EditorLockState.Held(OptionalLong.of(9)),
                assertInstanceOf(ProjectFileCheck.Readable.class, withLock).lock());
        assertEquals(EditorLockState.SKIPPED, assertInstanceOf(ProjectFileCheck.Readable.class, withoutLock).lock());
        assertEquals(List.of(game.getParent()), lockProbes, "only the read file's folder, only when asked");
    }

    /** A lock check that does not answer in time never makes a readable project Unreadable (Open stays enabled). */
    @Test
    void aLockCheckThatDoesNotAnswerInTimeLeavesTheProjectOpenable() throws Exception {
        health = new ProjectHealthService(path -> readable("1.0", "VertexForge 1.0.0", EditorLockState.SKIPPED),
                folder -> {
                    try {
                        shareAnswers.await();
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                    return EditorLockState.FREE;
                }, TIMEOUT);

        ProjectFileCheck check = health.check(tempDir.resolve("Slow").resolve("Slow.vfproj"), true)
                .get(10, TimeUnit.SECONDS);

        ProjectFileCheck.Readable readable = assertInstanceOf(ProjectFileCheck.Readable.class, check);
        assertEquals(new EditorLockState.Unknown("no response after 200 ms"), readable.lock());
        ProjectStatus status = evaluate(check, null);
        assertEquals(Kind.OK, status.kind());
        assertTrue(status.reason().endsWith("Could not check whether another editor has it open: no response after"
                + " 200 ms."), status.reason());
    }

    @Test
    void aLockProbeThatThrowsCannotBeChecked() throws Exception {
        health = new ProjectHealthService(path -> readable("1.0", "VertexForge 1.0.0", EditorLockState.SKIPPED),
                folder -> {
                    throw new IllegalStateException("boom");
                }, Duration.ofSeconds(5));

        ProjectFileCheck check = health.check(tempDir.resolve("Game").resolve("Game.vfproj"), true)
                .get(10, TimeUnit.SECONDS);

        EditorLockState lock = assertInstanceOf(ProjectFileCheck.Readable.class, check).lock();
        assertTrue(assertInstanceOf(EditorLockState.Unknown.class, lock).reason().contains("boom"), lock::toString);
    }

    @Test
    void aLateAnswerDoesNotChangeATimedOutResult() throws Exception {
        health = new ProjectHealthService(this::blockingProbe, NO_LOCK, TIMEOUT);
        CompletableFuture<ProjectFileCheck> check = health.check(tempDir.resolve("Slow.vfproj"), true);
        assertInstanceOf(ProjectFileCheck.Unreadable.class, check.get(10, TimeUnit.SECONDS));

        shareAnswers.countDown();

        assertInstanceOf(ProjectFileCheck.Unreadable.class, check.join());
    }

    @Test
    void anEntryWithoutAUsablePathIsUnreadableAtOnce() throws Exception {
        health = new ProjectHealthService(this::blockingProbe, NO_LOCK, TIMEOUT);
        RecentEntry relative = RecentEntry.fromJson(UUID.randomUUID(),
                com.google.gson.JsonParser.parseString("{\"name\": \"R\", \"path\": \"relative/R.vfproj\"}")
                        .getAsJsonObject());

        ProjectFileCheck check = health.check(relative, true).get(1, TimeUnit.SECONDS);

        assertInstanceOf(ProjectFileCheck.Unreadable.class, check);
        assertEquals(0, probeCalls.get());
    }

    @Test
    void aProbeThatThrowsIsUnreadable() throws Exception {
        health = new ProjectHealthService(path -> {
            throw new IllegalStateException("boom");
        }, NO_LOCK, Duration.ofSeconds(5));

        ProjectFileCheck check = health.check(tempDir.resolve("Bad.vfproj"), true).get(10, TimeUnit.SECONDS);

        ProjectFileCheck.Unreadable unreadable = assertInstanceOf(ProjectFileCheck.Unreadable.class, check);
        assertTrue(unreadable.reason().contains("boom"), unreadable.reason());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // evaluate: the row's status
    // ---------------------------------------------------------------------------------------------------------------

    private static ProjectFileCheck.Readable readable(String schema, String engineVersion, EditorLockState lock) {
        return new ProjectFileCheck.Readable(new ProjectFile("Game", schema, engineVersion), lock);
    }

    /** A project naming its asset format too (VK-1690); {@code null} leaves it out, as before the editor stamped it. */
    private static ProjectFileCheck.Readable readable(String schema, String engineVersion, String assetFormatVersion,
                                                      EditorLockState lock) {
        return new ProjectFileCheck.Readable(new ProjectFile("Game", schema, engineVersion, null, ".",
                assetFormatVersion), lock);
    }

    private static ProjectFileCheck.Readable readable(String schema) {
        return readable(schema, "VertexForge 2.1.0", EditorLockState.FREE);
    }

    /** An OK engine 1.0.0 whose engine.json says it reads {@code projectSchemaVersion}; {@code null} leaves it out. */
    private static EngineOption engine(UUID id, String projectSchemaVersion, boolean isDefault) throws Exception {
        return engine(id, "1.0.0", projectSchemaVersion, null, isDefault);
    }

    /** An OK engine whose engine.json names these versions; {@code null} leaves a member out. */
    private static EngineOption engine(UUID id, String engineVersion, String projectSchemaVersion,
                                       String assetFormatVersion, boolean isDefault) throws Exception {
        String schema = projectSchemaVersion == null ? "" : ", \"projectSchemaVersion\": \"" + projectSchemaVersion
                + "\"";
        String assets = assetFormatVersion == null ? "" : ", \"assetFormatVersion\": \"" + assetFormatVersion + "\"";
        EngineManifest manifest = EngineManifest.parse("{\"manifestVersion\": 1, \"engineVersion\": \""
                + engineVersion + "\", \"configuration\": \"Development\", \"editorExe\": \"Editor.exe\","
                + " \"workingDirectory\": \".\"" + schema + assets + "}");
        Path exe = ROOT.resolve(id.toString()).resolve("Editor.exe");
        return new EngineOption(Optional.of(id), exe, exe.toString(), EngineHealth.ok(manifest, ROOT), false,
                isDefault, false);
    }

    /** The working {@code VERTEXFORGE_EDITOR_PATH} override, with an engine.json naming these versions. */
    private static EngineOption override(String engineVersion, String projectSchemaVersion,
                                         String assetFormatVersion) throws Exception {
        EngineOption engine = engine(UUID.randomUUID(), engineVersion, projectSchemaVersion, assetFormatVersion,
                false);
        Path exe = ROOT.resolve("override").resolve("Editor.exe");
        return new EngineOption(Optional.empty(), exe, exe.toString(), engine.health(), false, false, true);
    }

    /** An OK engine without an engine.json (an engine built before VK-1680). */
    private static EngineOption engineWithoutManifest(UUID id) {
        Path exe = ROOT.resolve(id.toString()).resolve("Editor.exe");
        return new EngineOption(Optional.of(id), exe, exe.toString(), EngineHealth.ok(null, ROOT), false, false,
                false);
    }

    private static EngineOptions options(UUID defaultId, EngineOption... engines) {
        return optionsWithOverride(defaultId, null, engines);
    }

    private static EngineOptions optionsWithOverride(UUID defaultId, EngineOption override, EngineOption... engines) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.ofNullable(defaultId), StoreStatus.OK,
                Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.ofNullable(override), Arrays.asList(engines));
    }

    private static ProjectStatus evaluate(ProjectFileCheck check, EngineOptions options) {
        return ProjectHealthService.evaluate(check, options, null, FolderEditor.NONE);
    }

    /** AC: "A project saved by a newer engine shows 'Needs VertexForge' and the required version." */
    @Test
    void aProjectSavedByANewerEngineNeedsThatVersion() throws Exception {
        EngineOptions options = options(A, engine(A, "1.0", true));

        ProjectStatus status = evaluate(readable("2.0"), options);

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertEquals("Needs VertexForge 2.1.0", status.label());
        assertFalse(status.isOk(), "Open is disabled");
        assertEquals("Saved by VertexForge 2.1.0 (project format 2.0, asset format 1.0.0). The only engine this"
                + " launcher knows (1.0.0 Development) cannot open it: it reads project format 1.0. Add VertexForge"
                + " 2.1.0 or later with Engines….", status.reason());
    }

    /** VK-1690 AC2: a project 1.1.0 in asset format 1.1.0, and only a 1.0.0 engine registered. */
    @Test
    void aProjectInANewerAssetFormatNeedsTheVersionThatSavedIt() throws Exception {
        EngineOptions options = options(A, engine(A, "1.0.0", "1.0", "1.0.0", true));

        ProjectStatus status = evaluate(readable("1.0", "VertexForge 1.1.0", "1.1.0", EditorLockState.FREE), options);

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertEquals("Needs VertexForge 1.1.0", status.label());
        assertEquals("Saved by VertexForge 1.1.0 (project format 1.0, asset format 1.1.0). The only engine this"
                + " launcher knows (1.0.0 Development) cannot open it: it reads project format 1.0 and asset format"
                + " 1.0.0. Add VertexForge 1.1.0 or later with Engines….", status.reason());
    }

    /**
     * A newer asset format saved by the engine's own version (a development build that changed the format) needs a
     * newer VertexForge, never "VertexForge 1.0.0" next to a 1.0.0 engine.
     */
    @Test
    void aNewerAssetFormatFromTheSameVersionNeedsANewerVertexForge() throws Exception {
        EngineOptions options = options(A, engine(A, "1.0.0", "1.0", "1.0.0", true));

        ProjectStatus status = evaluate(readable("1.0", "VertexForge 1.0.0", "1.1.0", EditorLockState.FREE), options);

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertEquals("Needs newer VertexForge", status.label());
    }

    /** The product version blocks too, even with the same formats (VK-1690: a newer major.minor). */
    @Test
    void aNewerProductMajorOrMinorNeedsANewerEngine() throws Exception {
        EngineOptions options = options(A, engine(A, "1.0.0", "1.0", "1.0.0", true));

        ProjectStatus status = evaluate(readable("1.0", "VertexForge 1.1.0", "1.0.0", EditorLockState.FREE), options);

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertEquals("Needs VertexForge 1.1.0", status.label());
        // Only the patch: it opens.
        assertEquals(Kind.OK,
                evaluate(readable("1.0", "VertexForge 1.0.9", "1.0.0", EditorLockState.FREE), options).kind());
    }

    @Test
    void theLabelNamesTheVersionWhenTheProjectSaysIt() throws Exception {
        EngineOptions options = options(A, engine(A, "1.0", true));

        assertEquals("Needs VertexForge 3.0.1", evaluate(readable("2.0", "3.0.1", EditorLockState.FREE), options)
                .label());
        assertEquals("Needs newer VertexForge", evaluate(readable("2.0", "VertexForge two", EditorLockState.FREE),
                options).label());
        ProjectStatus unknown = evaluate(readable("2.0", null, EditorLockState.FREE), options);
        assertEquals("Needs newer VertexForge", unknown.label());
        assertTrue(unknown.reason().startsWith("Saved by an unknown VertexForge (project format 2.0, asset format"
                + " 1.0.0)."), unknown.reason());
        assertTrue(unknown.reason().endsWith(" Add a newer VertexForge with Engines…."), unknown.reason());
    }

    @Test
    void aNewerMinorOpensAndAnOlderMajorIsInvalidInTheEditorsWords() throws Exception {
        EngineOptions options = options(A, engine(A, "1.0", true));

        assertEquals(Kind.OK, evaluate(readable("1.9", "VertexForge 1.0.0", EditorLockState.FREE), options).kind());
        // The editor refuses an older major (exit code 3) and no migration exists.
        ProjectStatus older = evaluate(readable("0.5", "VertexForge 1.0.0", EditorLockState.FREE), options);
        assertEquals(Kind.INVALID, older.kind());
        assertEquals("Project schema version 0.5 is older than this engine supports (1.0)", older.reason());
    }

    @Test
    void whenEveryEngineRefusesAnOlderSchemaTheFirstOnesWordsShow() throws Exception {
        EngineOptions options = options(null, engine(A, "1.0.0", "1.0", "1.0.0", false),
                engine(B, "2.0.0", "2.0", "1.0.0", false));

        ProjectStatus status = evaluate(readable("0.5", "VertexForge 1.0.0", EditorLockState.FREE), options);

        assertEquals(ProjectStatus.invalid("Project schema version 0.5 is older than this engine supports (1.0)"),
                status);
    }

    /** As in the editor, a member it refuses is reported before an older schema. */
    @Test
    void aMemberTheEditorRefusesComesBeforeAnOlderSchema() throws Exception {
        ProjectFileCheck check = new ProjectFileCheck.Readable(new ProjectFile("Game", "0.5", "VertexForge 1.0.0",
                "Invalid project file: 'startupScene' cannot be empty"), EditorLockState.FREE);

        assertEquals(ProjectStatus.invalid("Invalid project file: 'startupScene' cannot be empty"),
                evaluate(check, options(A, engine(A, "1.0", true))));
    }

    @Test
    void anEngineThatFindsItNewerOutweighsOneThatRefusesItsOlderSchema() throws Exception {
        EngineOptions options = options(null, engine(A, "1.0.0", "1.0", "1.0.0", false),
                engine(B, "3.0.0", "3.0", "1.0.0", false));

        ProjectStatus status = evaluate(readable("2.0"), options);

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertEquals("Saved by VertexForge 2.1.0 (project format 2.0, asset format 1.0.0). None of the engines this"
                + " launcher knows can open it: 1.0.0 Development reads project format 1.0 and asset format 1.0.0;"
                + " 3.0.0 Development reads project format 3.0 and asset format 1.0.0. Add VertexForge 2.1.0 or later"
                + " with Engines….", status.reason());
    }

    @Test
    void thereIsNoVerdictBeforeTheEnginesAnswerOrWithoutAnEngine() throws Exception {
        assertEquals(Kind.OK, evaluate(readable("2.0"), null).kind());
        assertEquals(Kind.OK, evaluate(readable("2.0"), options(null)).kind());
    }

    @Test
    void anEngineThatDoesNotSayWhatItReadsGivesNoVerdict() throws Exception {
        ProjectFileCheck check = readable("2.0", "VertexForge 1.0.0", EditorLockState.FREE);

        assertEquals(ProjectStatus.ok(), evaluate(check, options(A, engine(A, null, true))));
        assertEquals(ProjectStatus.ok(), evaluate(check, options(A, engine(A, "garbled", true))));
        assertEquals(ProjectStatus.ok(), evaluate(check, options(null, engineWithoutManifest(A))));
    }

    /** An engine that cannot tell counts as able to open the project, as VK-1686 decided. */
    @Test
    void anEngineThatCannotTellCountsAsAbleToOpenIt() throws Exception {
        EngineOptions options = options(null, engine(A, "1.0", false), engineWithoutManifest(B));

        assertEquals(ProjectStatus.ok(), evaluate(readable("2.0"), options));
    }

    @Test
    void aProjectNeedsANewerEngineOnlyWhenNoEngineCanOpenIt() throws Exception {
        // No default and two engines: Open would ask; one of them can open it.
        EngineOptions oneReadsIt = options(null, engine(A, "1.0", false),
                engine(B, "2.1.0", "2.0", null, false));
        EngineOptions noneReadsIt = options(null, engine(A, "1.0", false), engine(B, "1.3", false));

        assertEquals(ProjectStatus.ok(), evaluate(readable("2.0"), oneReadsIt));
        ProjectStatus status = evaluate(readable("2.0"), noneReadsIt);
        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertTrue(status.reason().contains(" None of the engines this launcher knows can open it: 1.0.0 Development"
                + " reads project format 1.0; 1.0.0 Development reads project format 1.3. Add VertexForge 2.1.0 or"
                + " later with Engines…."), status.reason());
    }

    /** D2: Open stays enabled when another engine can open the project, and the row says what Open will do. */
    @Test
    void aBoundProjectWhoseEngineCannotOpenItIsOkWithANote() throws Exception {
        EngineOptions options = options(B, engine(A, "1.0", false), engine(B, "2.1.0", "2.0", null, true));

        ProjectStatus boundToOld = ProjectHealthService.evaluate(readable("2.0"), options, A.toString(),
                FolderEditor.NONE);
        assertEquals(ProjectStatus.ok("Saved by VertexForge 2.1.0. The engine it opens with (1.0.0 Development) cannot"
                + " open it, so Open offers one that can."), boundToOld);
        assertEquals(ProjectStatus.ok(),
                ProjectHealthService.evaluate(readable("2.0"), options, B.toString(), FolderEditor.NONE));
        // Unbound, it opens with the default, which can open it.
        assertEquals(ProjectStatus.ok(), evaluate(readable("2.0"), options));
    }

    @Test
    void theNoteKeepsTheLockThatCouldNotBeChecked() throws Exception {
        EngineOptions options = options(B, engine(A, "1.0", false), engine(B, "2.1.0", "2.0", null, true));

        ProjectStatus status = ProjectHealthService.evaluate(readable("2.0", "VertexForge 2.1.0",
                new EditorLockState.Unknown("Access denied")), options, A.toString(), FolderEditor.NONE);

        assertEquals(ProjectStatus.ok("Saved by VertexForge 2.1.0. The engine it opens with (1.0.0 Development) cannot"
                + " open it, so Open offers one that can. Could not check whether another editor has it open: Access"
                + " denied."), status);
    }

    @Test
    void aProjectThatIsAlreadyOpenSaysSoRatherThanTheNote() throws Exception {
        EngineOptions options = options(B, engine(A, "1.0", false), engine(B, "2.1.0", "2.0", null, true));

        ProjectStatus status = ProjectHealthService.evaluate(readable("2.0", "VertexForge 2.1.0",
                new EditorLockState.Held(OptionalLong.of(5))), options, A.toString(), FolderEditor.NONE);

        assertEquals(Kind.ALREADY_OPEN, status.kind());
    }

    /** The override opens every project, so it is the engine the note names, and it is offered like the others. */
    @Test
    void theOverrideIsOneOfTheEnginesOpenCouldOffer() throws Exception {
        EngineOption oldOverride = override("1.0.0", "1.0", "1.0.0");
        EngineOption current = engine(A, "1.1.0", "1.0", "1.1.0", true);
        ProjectFileCheck project = readable("1.0", "VertexForge 1.1.0", "1.1.0", EditorLockState.FREE);

        assertEquals(ProjectStatus.ok("Saved by VertexForge 1.1.0. The engine it opens with (1.0.0 Development) cannot"
                + " open it, so Open offers one that can."), evaluate(project, optionsWithOverride(A, oldOverride,
                current)));
        ProjectStatus alone = evaluate(project, optionsWithOverride(null, oldOverride));
        assertEquals(Kind.NEEDS_NEWER_ENGINE, alone.kind());
        assertTrue(alone.reason().contains(" The only engine this launcher knows (1.0.0 Development) cannot open it:"),
                alone.reason());
        // A newer override opens what the registered engines cannot.
        assertEquals(ProjectStatus.ok(), evaluate(project, optionsWithOverride(null,
                override("1.1.0", "1.0", "1.1.0"), engine(A, "1.0.0", "1.0", "1.0.0", true))));
    }

    /** A broken engine cannot be started, so it never counts as able to open the project. */
    @Test
    void aBrokenEngineIsNotOffered() throws Exception {
        EngineOption current = engine(B, "1.1.0", "1.0", "1.1.0", false);
        Path exe = ROOT.resolve(B.toString()).resolve("Editor.exe");
        EngineOption broken = new EngineOption(Optional.of(B), exe, exe.toString(),
                EngineHealth.broken("gone", current.health().manifest()), false, false, false);

        ProjectStatus status = evaluate(readable("1.0", "VertexForge 1.1.0", "1.1.0", EditorLockState.FREE),
                options(A, engine(A, "1.0.0", "1.0", "1.0.0", true), broken));

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
    }

    /** VK-1690 AC1: an older project opens; the version dialog comes with Open. */
    @Test
    void anOlderProjectOpens() throws Exception {
        EngineOptions options = options(A, engine(A, "1.1.0", "1.0", "1.1.0", true));

        assertEquals(ProjectStatus.ok(), evaluate(readable("1.0", "VertexForge 1.0.0", EditorLockState.FREE), options));
    }

    @Test
    void aSchemaTooLargeFor32BitsIsStillNewer() throws Exception {
        assertEquals(Kind.NEEDS_NEWER_ENGINE,
                evaluate(readable("4294967296.0"), options(A, engine(A, "1.0", true))).kind());
    }

    @Test
    void aNewerSchemaWinsOverAMemberTheOlderEditorWouldRefuse() throws Exception {
        ProjectFileCheck check = new ProjectFileCheck.Readable(new ProjectFile("Game", "2.0", "VertexForge 2.1.0",
                "Invalid project file: missing or invalid 'startupScene' field"), EditorLockState.FREE);

        assertEquals(Kind.NEEDS_NEWER_ENGINE, evaluate(check, options(A, engine(A, "1.0", true))).kind());
        // Without engines there is no verdict on the version, so the editor's rule shows.
        assertEquals(Kind.INVALID, evaluate(check, null).kind());
    }

    @Test
    void whatTheEditorWouldRefuseIsInvalidInItsWords() {
        ProjectFileCheck structure = new ProjectFileCheck.Readable(new ProjectFile("Game", null, null,
                "Invalid project file: missing or invalid 'schemaVersion' field"), EditorLockState.FREE);
        ProjectFileCheck format = readable("1.0.0");

        ProjectStatus structureStatus = evaluate(structure, null);
        assertEquals(Kind.INVALID, structureStatus.kind());
        assertEquals("Invalid project file: missing or invalid 'schemaVersion' field", structureStatus.reason());
        ProjectStatus formatStatus = evaluate(format, null);
        assertEquals(Kind.INVALID, formatStatus.kind());
        assertEquals("Invalid schema version format: '1.0.0' (expected MAJOR.MINOR)", formatStatus.reason());
    }

    @Test
    void aHeldLockIsAlreadyOpenWithTheHoldersPid() {
        ProjectStatus status = evaluate(readable("1.0", "VertexForge 1.0.0",
                new EditorLockState.Held(OptionalLong.of(1234))), null);

        assertEquals(Kind.ALREADY_OPEN, status.kind());
        assertEquals("Already open", status.label());
        assertFalse(status.isOk(), "Open is disabled");
        assertTrue(status.reason().contains("already open in another editor (pid 1234)"), status.reason());
    }

    @Test
    void theLockOfAnEditorOfThisLauncherIsItsOwn() {
        ProjectFileCheck held = readable("1.0", "VertexForge 1.0.0", new EditorLockState.Held(OptionalLong.empty()));

        // The row shows that editor's phase, and a second Open says it already runs.
        assertEquals(Kind.OK, ProjectHealthService.evaluate(held, null, null, FolderEditor.THIS_PROJECT).kind());
        // Another project file of the same folder cannot be opened beside it.
        ProjectStatus sibling = ProjectHealthService.evaluate(readable("1.0", "VertexForge 1.0.0",
                EditorLockState.SKIPPED), null, null, FolderEditor.OTHER_PROJECT);
        assertEquals(Kind.ALREADY_OPEN, sibling.kind());
        assertTrue(sibling.reason().contains("an editor this launcher started"), sibling.reason());
    }

    @Test
    void aNewerProjectThatIsAlsoOpenSaysBoth() throws Exception {
        ProjectStatus status = evaluate(readable("2.0", "VertexForge 2.1.0",
                new EditorLockState.Held(OptionalLong.of(7))), options(A, engine(A, "1.0", true)));

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertTrue(status.reason().endsWith(" It is also already open in another editor (pid 7)."), status.reason());
    }

    @Test
    void aLockThatCouldNotBeCheckedStillOpensWithANote() {
        ProjectStatus status = evaluate(readable("1.0", "VertexForge 1.0.0",
                new EditorLockState.Unknown("Access denied")), null);

        assertEquals(Kind.OK, status.kind());
        assertTrue(status.reason().endsWith("Could not check whether another editor has it open: Access denied."),
                status.reason());
    }

    @Test
    void theFilesOwnProblemsPassThrough() {
        ProjectStatus missing = evaluate(new ProjectFileCheck.Missing("The project file was not found"), null);
        assertEquals(Kind.MISSING, missing.kind());
        assertEquals("The project file was not found. " + ProjectHealthService.LOCATE_HINT, missing.reason());

        assertEquals(ProjectStatus.unreadable("Access denied"),
                evaluate(new ProjectFileCheck.Unreadable("Access denied"), null));
        assertEquals(ProjectStatus.invalid("Not a file"), evaluate(new ProjectFileCheck.Invalid("Not a file"), null));
    }

    @Test
    void theLabelHelperReadsBothVersionForms() {
        assertEquals("Needs VertexForge 2.1.0", ProjectHealthService.label("VertexForge 2.1.0"));
        assertEquals("Needs VertexForge 2.1.0", ProjectHealthService.label(" 2.1.0 "));
        assertEquals("Needs newer VertexForge", ProjectHealthService.label("2.1"));
        assertEquals("Needs newer VertexForge", ProjectHealthService.label(null));
    }
}
