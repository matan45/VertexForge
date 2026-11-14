package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.TickingClock;
import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.engine.PickedEngine;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectStatus.Kind;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.service.EditorLauncher.LaunchResult;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import com.vertexforge.launcher.service.EditorLauncherTest.RecordingStarter;
import com.vertexforge.launcher.settings.LauncherSettingsRepository;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.store.StoreWriteException.Reason;
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
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectHubTest {

    @TempDir
    Path tempDir;

    private Path root;
    private Path configDir;
    private Path projectsDir;
    private RecordingStarter starter;
    private final List<ProjectHub> hubs = new ArrayList<>();
    private ProjectHub hub;

    @BeforeEach
    void setUp() throws IOException {
        root = TestSupport.real(tempDir);
        configDir = root.resolve("config");
        projectsDir = Files.createDirectories(root.resolve("projects"));
        starter = new RecordingStarter(FakeProcess.running(), null);
        hub = hub(Map.of());
    }

    @AfterEach
    void tearDown() {
        hubs.forEach(ProjectHub::close);
    }

    private ProjectHub hub(Map<String, String> variables) {
        return hub(variables, Optional.empty());
    }

    private ProjectHub hub(Map<String, String> variables, Optional<Path> launcherExecutable) {
        LauncherEnvironment environment = TestSupport.environment(configDir, launcherExecutable,
                new TickingClock(TestSupport.T0), variables, starter);
        ProjectHub created = ProjectHub.create(environment, TestSupport.log(root.resolve("logs")).log);
        hubs.add(created);
        return created;
    }

    private static EngineOptions checked(ProjectHub on) throws Exception {
        return on.checkEngines(on.listEngines()).get(10, TimeUnit.SECONDS);
    }

    /** Registers an engine through "Add Engine…" with this file (an Editor.exe or an engine.json). */
    private static UUID addEngine(ProjectHub on, Path picked) throws Exception {
        PickedEngine inspected = on.inspectEngine(picked).get(10, TimeUnit.SECONDS);
        PickedEngine.Usable usable = assertInstanceOf(PickedEngine.Usable.class, inspected, inspected::toString);
        return on.addEngine(usable).engine().id();
    }

    private static List<String> names(RecentProjectsView view) {
        return view.entries().stream().map(RecentEntry::displayName).toList();
    }

    private RecentEntry listed(ProjectHub on, String name) {
        return on.listProjects().entries().stream()
                .filter(entry -> name.equals(entry.name()))
                .findFirst()
                .orElseThrow();
    }

    @Test
    void anEmptyStoreListsNothing() {
        RecentProjectsView view = hub.listProjects();

        assertTrue(view.isEmpty());
        assertEquals(StoreStatus.ABSENT, view.status());
        assertFalse(Files.exists(configDir), "listing must not write the store");
    }

    @Test
    void aCreatedProjectIsListedAndStoredUnderItsRealPath() {
        CreateOutcome outcome = hub.createProject("Game", projectsDir);

        assertTrue(outcome.created(), () -> outcome.result().errorMessage());
        assertTrue(outcome.listed());
        assertEquals(projectsDir.resolve("Game/Game.vfproj"), outcome.projectPath());
        List<RecentEntry> projects = hub.listProjects().entries();
        assertEquals(1, projects.size());
        assertEquals("Game", projects.getFirst().name());
        assertEquals(projectsDir.resolve("Game/Game.vfproj"), projects.getFirst().path());
        assertTrue(Files.isRegularFile(configDir.resolve("recent-projects.json")));
    }

    @Test
    void aFailedCreationAddsNothing() throws IOException {
        Path blocker = Files.writeString(root.resolve("blocker"), "file");

        CreateOutcome outcome = hub.createProject("Game", blocker);

        assertFalse(outcome.created());
        assertFalse(outcome.listed());
        assertNull(outcome.listError());
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void creatingTheFilesNeverTouchesTheList() {
        ProjectCreationResult result = hub.createProjectFiles(new CreateRequest("Game", projectsDir),
                CreationMonitor.NONE);

        assertTrue(result.success(), result::errorMessage);
        assertTrue(hub.listProjects().isEmpty());
        assertFalse(Files.exists(configDir), "the store was not written");
    }

    @Test
    void listingACreatedProjectUsesItsRealPath() {
        ProjectCreationResult result = hub.createProjectFiles(new CreateRequest("Game", projectsDir),
                CreationMonitor.NONE);

        CreateOutcome outcome = hub.listCreated(result, "Game");

        assertTrue(outcome.listed());
        assertEquals(projectsDir.resolve("Game/Game.vfproj"), outcome.projectPath());
        assertEquals(List.of("Game"), names(hub.listProjects()));
    }

    @Test
    void aFailedOrCancelledCreationIsNeverListed() {
        CreateOutcome failed = hub.listCreated(ProjectCreationResult.failure("no"), "Game");
        CreateOutcome cancelled = hub.listCreated(ProjectCreationResult.cancelled(null), "Game");

        assertFalse(failed.created() || failed.listed());
        assertFalse(cancelled.created() || cancelled.listed());
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void aRelativeLocationCreatesAndListsNothing() {
        Path relative = Path.of("vk1679-hub-relative-" + UUID.randomUUID());

        CreateOutcome outcome = hub.createProject("Game", relative);

        assertFalse(outcome.created());
        assertFalse(Files.exists(relative));
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void aLocationCheckAnswersInTheBackground() throws Exception {
        assertTrue(hub.checkLocation(projectsDir, "Game").get(5, TimeUnit.SECONDS).ok());
        assertFalse(hub.checkLocation(projectsDir, "CON").get(5, TimeUnit.SECONDS).ok());
    }

    @Test
    void aProjectCreatedWhileTheListIsReadOnlyIsReportedNotLost() throws IOException {
        Files.createDirectories(configDir);
        Path store = configDir.resolve("recent-projects.json");
        Files.writeString(store, "{\"version\": 3, \"projects\": []}");
        byte[] newer = Files.readAllBytes(store);

        CreateOutcome outcome = hub.createProject("Game", projectsDir);

        assertTrue(outcome.created());
        assertFalse(outcome.listed());
        assertEquals(Reason.NEWER_VERSION, outcome.listError().reason());
        assertTrue(Files.isRegularFile(outcome.projectPath()), "the project itself exists");
        assertArrayEquals(newer, Files.readAllBytes(store));
    }

    @Test
    void theNewestProjectComesFirstAndOpeningMovesAProjectToTheTop() throws StoreWriteException {
        hub.createProject("First", projectsDir);
        hub.createProject("Second", projectsDir);
        assertEquals(List.of("Second", "First"), names(hub.listProjects()));

        RecentProjectsView view = hub.markOpened(listed(hub, "First").id());

        assertEquals(List.of("First", "Second"), names(view));
        assertEquals(List.of("First", "Second"), names(hub.listProjects()));
    }

    @Test
    void removeDropsTheProjectFromTheListButNotFromTheDisk() throws StoreWriteException {
        hub.createProject("Keep", projectsDir);
        hub.createProject("Drop", projectsDir);

        hub.removeProject(listed(hub, "Drop").id());

        assertEquals(List.of("Keep"), names(hub.listProjects()));
        assertTrue(Files.isRegularFile(projectsDir.resolve("Drop/Drop.vfproj")));
    }

    /** Replaces the VK-1677 characterization: a missing project now stays listed, with a Missing status. */
    @Test
    void aMissingProjectStaysListedAndShowsAsMissing() throws Exception {
        hub.createProject("Game", projectsDir);
        hub.createProject("Gone", projectsDir);
        Files.delete(projectsDir.resolve("Gone/Gone.vfproj"));

        RecentProjectsView view = hub.listProjects();
        hub.createProject("Later", projectsDir);
        RecentProjectsView afterASave = hub(Map.of()).listProjects();

        assertEquals(List.of("Gone", "Game"), names(view));
        assertEquals(Kind.MISSING, hub.checkStatus(listed(hub, "Gone")).get(10, TimeUnit.SECONDS).kind());
        assertEquals(Kind.OK, hub.checkStatus(listed(hub, "Game")).get(10, TimeUnit.SECONDS).kind());
        assertEquals(List.of("Later", "Gone", "Game"), names(afterASave));
    }

    @Test
    void twoHubsOnOneStoreKeepEachOthersProjects() {
        ProjectHub other = hub(Map.of());

        hub.createProject("Mine", projectsDir);
        other.createProject("Theirs", projectsDir);

        // Each hub has its own clock, so the two may tie on time: compare as sets.
        assertEquals(Set.of("Mine", "Theirs"), Set.copyOf(names(hub.listProjects())));
        assertEquals(Set.of("Mine", "Theirs"), Set.copyOf(names(other.listProjects())));
    }

    @Test
    void aRemoveIsNotUndoneByAHubThatHadAStaleList() throws StoreWriteException {
        ProjectHub other = hub(Map.of());
        hub.createProject("Shared", projectsDir);
        UUID shared = listed(other, "Shared").id();

        hub.removeProject(shared);
        other.markOpened(shared);

        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void theListLimitComesFromTheLauncherSettings() throws IOException {
        Files.createDirectories(configDir);
        Files.writeString(configDir.resolve(LauncherSettingsRepository.FILE_NAME), "{\"recentProjectsLimit\": 2}");
        ProjectHub limited = hub(Map.of());

        limited.createProject("A", projectsDir);
        limited.createProject("B", projectsDir);
        limited.createProject("C", projectsDir);

        assertEquals(2, limited.listLimit());
        assertEquals(List.of("C", "B"), names(limited.listProjects()));
        assertEquals(50, hub.listLimit());
    }

    @Test
    void theOverrideOpensEveryProjectAndIsNeverBoundOrSaved() throws Exception {
        Path editor = TestSupport.fakeEditor(root.resolve("custom"));
        ProjectHub overridden = hub(Map.of(EditorOverride.ENV_VAR, editor.toString()));
        UUID registered = addEngine(overridden, TestSupport.devEngine(root.resolve("repo")));

        EngineOptions options = checked(overridden);

        EngineOption override = options.override().orElseThrow();
        assertTrue(override.isOk() && override.override(), override::toString);
        for (String engineId : new String[]{null, registered.toString()}) {
            LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, overridden.planLaunch(options, engineId));
            assertEquals(editor, ready.engine().executable());
            assertEquals(Optional.empty(), ready.bindTo(), "the override is never bound");
        }
        assertEquals(1, options.engines().size(), "the override is never saved");
        assertEquals(Optional.empty(), checked(hub).override());
    }

    @Test
    void anOverrideThatDoesNotWorkIsIgnored() throws Exception {
        ProjectHub overridden = hub(Map.of(EditorOverride.ENV_VAR, root.resolve("nowhere/Editor.exe").toString()));
        UUID registered = addEngine(overridden, TestSupport.devEngine(root.resolve("repo")));

        EngineOptions options = checked(overridden);

        assertEquals(EngineHealth.Kind.BROKEN, options.override().orElseThrow().health().kind());
        LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, overridden.planLaunch(options, null));
        assertEquals(Optional.of(registered), ready.engine().id());
    }

    /** AC2: a registered Development build starts in the working directory its manifest names. */
    @Test
    void aRegisteredDevBuildStartsInItsManifestsWorkingDirectory() throws Exception {
        Path repo = root.resolve("repo");
        Path editor = TestSupport.devEngine(repo);
        UUID id = addEngine(hub, editor.resolveSibling("engine.json"));
        hub.createProject("Game", projectsDir);
        RecentEntry game = listed(hub, "Game");

        EngineOptions options = checked(hub);
        LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, hub.planLaunch(options, game.engineId()));
        LaunchResult result = hub.launch(ready.engine(), game.path()).join();

        assertEquals(LaunchResult.ok(), result);
        ProcessBuilder started = starter.started.getFirst();
        assertEquals(List.of(editor.toString(), game.path().toString()), started.command());
        assertEquals(repo.resolve("VFEngine/editor").toFile(), started.directory());
        assertEquals("1.0.0 Development", ready.engine().versionText());

        // Its first open binds the unbound project to the engine it opened with.
        assertEquals(Optional.of(id), ready.bindTo());
        hub.markOpened(game.id(), ready.bindTo());
        assertEquals(id.toString(), listed(hub, "Game").engineId());
    }

    /** AC3: a deleted engine shows as Broken, and a project bound to it asks which engine to use. */
    @Test
    void aDeletedEngineIsBrokenAndItsProjectAsksForAnother() throws Exception {
        Path deleted = TestSupport.devEngine(root.resolve("old"));
        UUID oldId = addEngine(hub, deleted);
        hub.createProject("Game", projectsDir);
        hub.markOpened(listed(hub, "Game").id(), Optional.of(oldId));
        UUID otherId = addEngine(hub, TestSupport.devEngine(root.resolve("new")));
        Files.delete(deleted);

        EngineOptions options = checked(hub);
        LaunchPlan plan = hub.planLaunch(options, listed(hub, "Game").engineId());

        assertEquals(EngineHealth.Kind.BROKEN, options.find(oldId).orElseThrow().health().kind());
        LaunchPlan.Choose choose = assertInstanceOf(LaunchPlan.Choose.class, plan);
        assertTrue(choose.rememberByDefault());
        assertTrue(choose.problem().contains("broken"), choose.problem());
        assertEquals(List.of(Optional.of(otherId)), choose.candidates().stream().map(EngineOption::id).toList());
        assertTrue(starter.started.isEmpty());

        // Choosing another engine and remembering it rebinds the project.
        hub.bindEngine(listed(hub, "Game").id(), otherId);
        assertEquals(otherId.toString(), listed(hub, "Game").engineId());
    }

    /** AC1: the packaged launcher finds and registers its bundled engine from its own exe, whatever the cwd is. */
    @Test
    void thePackagedLauncherFindsItsBundledEngine() throws Exception {
        Path releaseRoot = root.resolve("dist/VertexForge");
        Path launcher = TestSupport.packagedLauncher(releaseRoot);
        ProjectHub packaged = hub(Map.of(), Optional.of(launcher));

        EngineOptions options = checked(packaged);

        EngineOption bundled = options.bundled().orElseThrow();
        Path exeDir = releaseRoot.resolve("bin/Editor/Release/x64");
        assertEquals(exeDir.resolve("Editor.exe"), bundled.executable());
        assertTrue(bundled.isOk(), bundled::toString);
        assertEquals(exeDir, bundled.health().workingDirectory());
        assertEquals("1.0.0 Release", bundled.versionText());
        LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, packaged.planLaunch(options, null));
        assertEquals(bundled, ready.engine());
        assertEquals(bundled.id(), ready.bindTo(), "the bundled engine is registered, so projects can bind to it");

        Path enginesFile = configDir.resolve("engines.json");
        byte[] registered = Files.readAllBytes(enginesFile);
        checked(packaged);
        assertArrayEquals(registered, Files.readAllBytes(enginesFile), "a registered bundled engine is not rewritten");

        // Another launcher (not packaged) sees the same registered engine, but it is not that launcher's own.
        EngineOptions elsewhere = checked(hub);
        assertEquals(Optional.empty(), elsewhere.bundled());
        assertEquals(List.of(bundled.id()), elsewhere.engines().stream().map(EngineOption::id).toList());
    }

    @Test
    void anUnboundProjectFollowsTheChosenDefault() throws Exception {
        UUID first = addEngine(hub, TestSupport.devEngine(root.resolve("first")));
        UUID second = addEngine(hub, TestSupport.devEngine(root.resolve("second")));
        assertInstanceOf(LaunchPlan.Choose.class, hub.planLaunch(checked(hub), null),
                "two engines and no default: ask");

        hub.setDefaultEngine(second);

        LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, hub.planLaunch(checked(hub), null));
        assertEquals(Optional.of(second), ready.engine().id());
        assertEquals(Optional.of(second), ready.bindTo());
        assertNotEquals(first, second);
    }

    @Test
    void launchGoesThroughTheInjectedProcessStarter() throws Exception {
        hub.createProject("Game", projectsDir);
        Path editor = TestSupport.fakeEditor(root.resolve("custom"));
        addEngine(hub, editor);
        Path project = projectsDir.resolve("Game/Game.vfproj");

        EngineOption engine = checked(hub).usable().getFirst();
        LaunchResult result = hub.launch(engine, project).join();

        assertEquals(LaunchResult.ok(), result);
        assertEquals(List.of(editor.toString(), project.toString()), starter.started.getFirst().command());
        // An install without engine.json starts in its own folder.
        assertEquals(editor.getParent().toFile(), starter.started.getFirst().directory());
        assertEquals(EngineOption.UNKNOWN_VERSION, engine.versionText());
    }
}
