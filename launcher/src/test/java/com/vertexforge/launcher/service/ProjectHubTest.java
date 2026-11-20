package com.vertexforge.launcher.service;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.TickingClock;
import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.engine.PickedEngine;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.ProjectStatus.Kind;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.repository.RelinkedProject;
import com.vertexforge.launcher.repository.RemovedProject;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import com.vertexforge.launcher.service.EditorLauncherTest.RecordingStarter;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.session.LaunchRequest;
import com.vertexforge.launcher.session.StartResult;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettingsRepository;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.store.StoreWriteException.Reason;
import com.vertexforge.launcher.template.TemplateCatalog;
import com.vertexforge.launcher.template.TemplateCatalogs;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeFalse;

class ProjectHubTest {

    @TempDir
    Path tempDir;

    private Path root;
    private Path configDir;
    private Path projectsDir;
    private RecordingStarter starter;
    private final List<ProjectHub> hubs = new ArrayList<>();
    private ProjectHub hub;
    /**
     * The content every project here is created from (VK-1689), with no engine id: like a project made from the
     * override, it stays unbound until its first open, which is what the VK-1681 binding tests below need.
     */
    private EngineContent engine;

    @BeforeEach
    void setUp() throws IOException {
        root = TestSupport.real(tempDir);
        configDir = root.resolve("config");
        projectsDir = Files.createDirectories(root.resolve("projects"));
        starter = new RecordingStarter(FakeProcess.running(), null);
        EngineContent registered = TestSupport.engineContent(root.resolve("engine-content"));
        engine = new EngineContent(Optional.empty(), registered.engineVersion(), registered.templatesDir(),
                registered.scriptLibDir(), registered.examplesDir(), registered.vfxLibraryDir(), "unbound", "", false);
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
        CreateOutcome outcome = hub.createProject(TestSupport.request("Game", projectsDir, engine));

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
    void aCreatedProjectIsBoundToTheEngineItWasCreatedFrom() {
        EngineContent bound = TestSupport.engineContent(root.resolve("engine-content"));

        CreateOutcome outcome = hub.createProject(TestSupport.request("Game", projectsDir, bound));

        assertTrue(outcome.listed(), () -> outcome.result().errorMessage());
        assertEquals(TestSupport.FAKE_ENGINE_ID.toString(), listed(hub, "Game").engineId());
    }

    @Test
    void aProjectCreatedFromAnEngineWithoutAnIdIsLeftUnbound() {
        EngineContent unregistered = new EngineContent(Optional.empty(), engine.engineVersion(),
                engine.templatesDir(), engine.scriptLibDir(), engine.examplesDir(), engine.vfxLibraryDir(),
                "override", "", true);

        CreateOutcome outcome = hub.createProject(TestSupport.request("Game", projectsDir, unregistered));

        assertTrue(outcome.listed(), () -> outcome.result().errorMessage());
        assertNull(listed(hub, "Game").engineId());
    }

    @Test
    void theTemplatesOfAnEngineAreReadInTheBackground() throws Exception {
        TemplateCatalogs.Answer answer = hub.listTemplates(engine).get(10, TimeUnit.SECONDS);

        assertNull(answer.problem());
        assertEquals(List.of(TestSupport.TEMPLATE), answer.catalog().templates().stream()
                .map(TemplateCatalog.Template::id).toList());
        assertEquals(TestSupport.FAKE_LIBRARY.size(), answer.catalog().library().size());
    }

    @Test
    void anEngineWhoseTemplatesAreGoneIsAnsweredWithTheReason() throws Exception {
        EngineContent gone = new EngineContent(engine.engineId(), engine.engineVersion(),
                root.resolve("no-templates"), engine.scriptLibDir(), Optional.empty(), Optional.empty(), "gone", "",
                false);

        TemplateCatalogs.Answer answer = hub.listTemplates(gone).get(10, TimeUnit.SECONDS);

        assertNull(answer.catalog());
        assertTrue(answer.problem().startsWith("The templates of gone could not be read: The templates folder "),
                answer.problem());
    }

    @Test
    void aFailedCreationAddsNothing() throws IOException {
        Path blocker = Files.writeString(root.resolve("blocker"), "file");

        CreateOutcome outcome = hub.createProject(TestSupport.request("Game", blocker, engine));

        assertFalse(outcome.created());
        assertFalse(outcome.listed());
        assertNull(outcome.listError());
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void creatingTheFilesNeverTouchesTheList() {
        ProjectCreationResult result = hub.createProjectFiles(TestSupport.request("Game", projectsDir, engine),
                CreationMonitor.NONE);

        assertTrue(result.success(), result::errorMessage);
        assertTrue(hub.listProjects().isEmpty());
        assertFalse(Files.exists(configDir), "the store was not written");
    }

    @Test
    void listingACreatedProjectUsesItsRealPath() {
        ProjectCreationResult result = hub.createProjectFiles(TestSupport.request("Game", projectsDir, engine),
                CreationMonitor.NONE);

        CreateOutcome outcome = hub.listCreated(result, TestSupport.request("Game", projectsDir, engine));

        assertTrue(outcome.listed());
        assertEquals(projectsDir.resolve("Game/Game.vfproj"), outcome.projectPath());
        assertEquals(List.of("Game"), names(hub.listProjects()));
    }

    @Test
    void aFailedOrCancelledCreationIsNeverListed() {
        CreateOutcome failed = hub.listCreated(ProjectCreationResult.failure("no"),
                TestSupport.request("Game", projectsDir, engine));
        CreateOutcome cancelled = hub.listCreated(ProjectCreationResult.cancelled(null),
                TestSupport.request("Game", projectsDir, engine));

        assertFalse(failed.created() || failed.listed());
        assertFalse(cancelled.created() || cancelled.listed());
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void aRelativeLocationCreatesAndListsNothing() {
        Path relative = Path.of("vk1679-hub-relative-" + UUID.randomUUID());

        CreateOutcome outcome = hub.createProject(TestSupport.request("Game", relative, engine));

        assertFalse(outcome.created());
        assertFalse(Files.exists(relative));
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void aLocationCheckAnswersInTheBackground() throws Exception {
        assertTrue(hub.checkLocation(projectsDir, "Game", TestSupport.blueprint("a.mt")).get(5, TimeUnit.SECONDS).ok());
        assertFalse(hub.checkLocation(projectsDir, "CON", TestSupport.blueprint("a.mt")).get(5, TimeUnit.SECONDS).ok());
    }

    @Test
    void aProjectCreatedWhileTheListIsReadOnlyIsReportedNotLost() throws IOException {
        Files.createDirectories(configDir);
        Path store = configDir.resolve("recent-projects.json");
        Files.writeString(store, "{\"version\": 3, \"projects\": []}");
        byte[] newer = Files.readAllBytes(store);

        CreateOutcome outcome = hub.createProject(TestSupport.request("Game", projectsDir, engine));

        assertTrue(outcome.created());
        assertFalse(outcome.listed());
        assertEquals(Reason.NEWER_VERSION, outcome.listError().reason());
        assertTrue(Files.isRegularFile(outcome.projectPath()), "the project itself exists");
        assertArrayEquals(newer, Files.readAllBytes(store));
    }

    @Test
    void theNewestProjectComesFirstAndOpeningMovesAProjectToTheTop() throws StoreWriteException {
        hub.createProject(TestSupport.request("First", projectsDir, engine));
        hub.createProject(TestSupport.request("Second", projectsDir, engine));
        assertEquals(List.of("Second", "First"), names(hub.listProjects()));

        RecentProjectsView view = hub.markOpened(listed(hub, "First").id());

        assertEquals(List.of("First", "Second"), names(view));
        assertEquals(List.of("First", "Second"), names(hub.listProjects()));
    }

    @Test
    void removeDropsTheProjectFromTheListButNotFromTheDisk() throws StoreWriteException {
        hub.createProject(TestSupport.request("Keep", projectsDir, engine));
        hub.createProject(TestSupport.request("Drop", projectsDir, engine));

        hub.removeProject(listed(hub, "Drop").id());

        assertEquals(List.of("Keep"), names(hub.listProjects()));
        assertTrue(Files.isRegularFile(projectsDir.resolve("Drop/Drop.vfproj")));
    }

    /** Replaces the VK-1677 characterization: a missing project now stays listed, with a Missing status. */
    @Test
    void aMissingProjectStaysListedAndShowsAsMissing() throws Exception {
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        hub.createProject(TestSupport.request("Gone", projectsDir, engine));
        Files.delete(projectsDir.resolve("Gone/Gone.vfproj"));

        RecentProjectsView view = hub.listProjects();
        hub.createProject(TestSupport.request("Later", projectsDir, engine));
        RecentProjectsView afterASave = hub(Map.of()).listProjects();

        assertEquals(List.of("Gone", "Game"), names(view));
        assertEquals(Kind.MISSING, status(hub, listed(hub, "Gone")).kind());
        assertEquals(Kind.OK, status(hub, listed(hub, "Game")).kind());
        assertEquals(List.of("Later", "Gone", "Game"), names(afterASave));
    }

    @Test
    void twoHubsOnOneStoreKeepEachOthersProjects() {
        ProjectHub other = hub(Map.of());

        hub.createProject(TestSupport.request("Mine", projectsDir, engine));
        other.createProject(TestSupport.request("Theirs", projectsDir, engine));

        // Each hub has its own clock, so the two may tie on time: compare as sets.
        assertEquals(Set.of("Mine", "Theirs"), Set.copyOf(names(hub.listProjects())));
        assertEquals(Set.of("Mine", "Theirs"), Set.copyOf(names(other.listProjects())));
    }

    @Test
    void aRemoveIsNotUndoneByAHubThatHadAStaleList() throws StoreWriteException {
        ProjectHub other = hub(Map.of());
        hub.createProject(TestSupport.request("Shared", projectsDir, engine));
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

        limited.createProject(TestSupport.request("A", projectsDir, engine));
        limited.createProject(TestSupport.request("B", projectsDir, engine));
        limited.createProject(TestSupport.request("C", projectsDir, engine));

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
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        RecentEntry game = listed(hub, "Game");

        EngineOptions options = checked(hub);
        LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, hub.planLaunch(options, game.engineId()));
        StartResult result = hub.launch(new LaunchRequest(ready.engine(), game.path(), "Game", McpSetting.DEFAULT,
                hub.settings().showEditorConsole())).join();

        assertInstanceOf(StartResult.Started.class, result, result::toString);
        ProcessBuilder started = starter.started.getFirst();
        // This manifest has no launchContract, so no --console, although the setting is on by default.
        assertEquals(List.of(editor.toString(), game.path().toString(), "--launch-status"),
                started.command().subList(0, 3));
        assertEquals(4, started.command().size());
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
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
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
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        Path editor = TestSupport.fakeEditor(root.resolve("custom"));
        addEngine(hub, editor);
        Path project = projectsDir.resolve("Game/Game.vfproj");

        EngineOption engine = checked(hub).usable().getFirst();
        StartResult result = hub.launch(new LaunchRequest(engine, project, "Game", McpSetting.DEFAULT, true)).join();

        StartResult.Started started = assertInstanceOf(StartResult.Started.class, result, result::toString);
        assertEquals(List.of(editor.toString(), project.toString(), "--launch-status",
                started.start().statusFile().orElseThrow().toString()), starter.started.getFirst().command());
        // An install without engine.json starts in its own folder.
        assertEquals(editor.getParent().toFile(), starter.started.getFirst().directory());
        assertEquals(EngineOption.UNKNOWN_VERSION, engine.versionText());
        // The per-launch log lives in the launcher's config folder (VK-1683).
        assertEquals(configDir.resolve("logs"), started.start().log().getParent());
    }

    /** VK-1683: an engine whose manifest advertises the launch contract gets --console while the setting is on. */
    @Test
    void anEngineWithTheLaunchContractGetsTheConsoleFlagByDefault() throws Exception {
        Path repo = root.resolve("repo");
        Path editor = TestSupport.devEngine(repo);
        TestSupport.writeManifest(editor.getParent(), "Development", "../../../../VFEngine/editor", 1);
        addEngine(hub, editor.resolveSibling("engine.json"));
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        RecentEntry game = listed(hub, "Game");
        LaunchPlan.Ready ready = assertInstanceOf(LaunchPlan.Ready.class, hub.planLaunch(checked(hub), null));

        assertTrue(hub.settings().showEditorConsole(), "on by default");
        hub.launch(new LaunchRequest(ready.engine(), game.path(), game.displayName(), game.mcp(),
                hub.settings().showEditorConsole())).join();

        assertEquals("--console", starter.started.getFirst().command().getLast());
    }

    /** VK-1683: the Settings dialog's choices are saved, and the hub uses them at once. */
    @Test
    void savedSettingsAreKeptAndUsed() throws Exception {
        assertTrue(hub.canSaveSettings());

        hub.saveSettings(LauncherSettings.AfterLaunch.STAY, false);

        assertEquals(LauncherSettings.AfterLaunch.STAY, hub.settings().afterLaunch());
        assertFalse(hub.settings().showEditorConsole());
        LauncherSettings reread = hub(Map.of()).settings();
        assertEquals(LauncherSettings.AfterLaunch.STAY, reread.afterLaunch());
        assertFalse(reread.showEditorConsole());
    }

    /** VK-1683: "Open with MCP" is stored on the entry and survives an open. */
    @Test
    void anEntrysMcpSettingIsSavedAndKept() throws Exception {
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        RecentEntry game = listed(hub, "Game");

        hub.setMcp(game.id(), new McpSetting(true, 7979));
        hub.markOpened(game.id(), Optional.empty());

        assertEquals(new McpSetting(true, 7979), listed(hub(Map.of()), "Game").mcp());
    }

    // --- existing projects (VK-1685) ---------------------------------------------------------------------------------

    /** A project made somewhere else: its file name and its {@code projectName} differ. */
    private Path existingProject(String folder, String fileName, String projectName) throws IOException {
        Path dir = Files.createDirectories(root.resolve("elsewhere").resolve(folder));
        return Files.writeString(dir.resolve(fileName), """
                {"schemaVersion": "1.0", "projectName": %s, "version": "0.1.0", "workingDirectory": ".",
                 "startupScene": "scenes/Main.vfScene", "engineVersion": "VertexForge 1.0.0"}
                """.formatted(new JsonPrimitive(projectName)));
    }

    private static PickedProject.Found found(ProjectHub on, Path picked) throws Exception {
        PickedProject result = on.inspectProject(picked).get(10, TimeUnit.SECONDS);
        return assertInstanceOf(PickedProject.Found.class, result, result::toString);
    }

    @Test
    void anExistingProjectIsListedUnderItsRealPathWithTheNameFromItsFile() throws Exception {
        Path file = existingProject("Cloned", "Cloned.vfproj", "My Cloned Game");

        AddedProject added = hub.addExisting(found(hub, file.getParent()));

        assertFalse(added.existed());
        assertEquals(file, added.entry().path());
        assertEquals(file.toString(), added.entry().rawPath());
        assertEquals("My Cloned Game", added.entry().displayName());
        assertEquals(List.of("My Cloned Game"), names(hub.listProjects()));
    }

    @Test
    void aDotDotSpellingOfAListedProjectGivesTheSameEntry() throws Exception {
        Path file = existingProject("Game", "Game.vfproj", "Game");
        Path dotDot = file.getParent().resolve("..").resolve("Game").resolve("Game.vfproj");

        AddedProject first = hub.addExisting(found(hub, file));
        AddedProject second = hub.addExisting(found(hub, dotDot));

        assertTrue(second.existed());
        assertEquals(first.entry().id(), second.entry().id());
        assertEquals(1, hub.listProjects().entries().size());
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void anotherLetterCaseOfAProjectIsStoredUnderItsRealSpellingOnce() throws Exception {
        Path file = existingProject("Game", "Game.vfproj", "Game");
        Path otherCase = Path.of(file.toString().toUpperCase(Locale.ROOT));

        AddedProject first = hub.addExisting(found(hub, otherCase));
        AddedProject second = hub.addExisting(found(hub, file));

        assertEquals(file.toString(), first.entry().rawPath(), "stored under the real spelling");
        assertTrue(second.existed());
        assertEquals(1, hub.listProjects().entries().size());
    }

    @Test
    void theShortNameSpellingOfAProjectGivesTheSameEntry() throws Exception {
        // The CI runner's temp folder is an 8.3 short path (RUNNER~1); most machines have none and skip this.
        assumeFalse(tempDir.toString().equals(root.toString()), "the temp folder has no other spelling here");
        Path file = existingProject("Game", "Game.vfproj", "Game");
        Path shortSpelling = tempDir.resolve(root.relativize(file));

        AddedProject first = hub.addExisting(found(hub, shortSpelling));
        AddedProject second = hub.addExisting(found(hub, file));

        assertEquals(file, first.entry().path());
        assertTrue(second.existed());
        assertEquals(1, hub.listProjects().entries().size());
    }

    @Test
    void aCreatedProjectAddedAgainIsAlreadyListed() throws Exception {
        CreateOutcome created = hub.createProject(TestSupport.request("Game", projectsDir, engine));

        AddedProject added = hub.addExisting(found(hub, created.projectPath().getParent()));

        assertTrue(added.existed(), "creation and adding store the same real path");
        assertEquals(1, hub.listProjects().entries().size());
    }

    @Test
    void anExistingProjectIsNotAddedToANewerList() throws Exception {
        Path file = existingProject("Game", "Game.vfproj", "Game");
        Files.createDirectories(configDir);
        Path store = configDir.resolve("recent-projects.json");
        Files.writeString(store, "{\"version\": 3, \"projects\": []}");
        byte[] newer = Files.readAllBytes(store);
        PickedProject.Found picked = found(hub, file);

        assertEquals(Reason.NEWER_VERSION,
                assertThrows(StoreWriteException.class, () -> hub.addExisting(picked)).reason());
        assertArrayEquals(newer, Files.readAllBytes(store));
    }

    @Test
    void aProjectFileThatIsNotValidJsonIsRefusedWithItsLocation() throws Exception {
        Path dir = Files.createDirectories(root.resolve("elsewhere/Broken"));
        Path file = Files.writeString(dir.resolve("Broken.vfproj"), "{\n  \"projectName\" \"Broken\"\n}\n");

        PickedProject result = hub.inspectProject(file).get(10, TimeUnit.SECONDS);

        PickedProject.Refused refused = assertInstanceOf(PickedProject.Refused.class, result, result::toString);
        assertTrue(refused.reason().contains("line 2"), refused.reason());
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void aProjectThatDoesNotAnswerInTimeIsRefused() throws Exception {
        CountDownLatch release = new CountDownLatch(1);
        LauncherEnvironment environment = TestSupport.environment(configDir, Optional.empty(),
                new TickingClock(TestSupport.T0), Map.of(), starter);
        ProjectHub slow = ProjectHub.create(environment, TestSupport.log(root.resolve("logs")).log, path -> {
            try {
                release.await();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            return new PickedProject.Refused(path, "too late");
        }, Duration.ofMillis(100));
        hubs.add(slow);
        Path picked = root.resolve("Slow.vfproj");

        try {
            PickedProject result = slow.inspectProject(picked).get(10, TimeUnit.SECONDS);

            PickedProject.Refused refused = assertInstanceOf(PickedProject.Refused.class, result, result::toString);
            assertEquals(picked, refused.picked());
            assertTrue(refused.reason().startsWith("No response after 100 ms"), refused.reason());
        } finally {
            release.countDown();
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Project health, Locate… and Undo (VK-1686)
    // ---------------------------------------------------------------------------------------------------------------

    /** The row's status as the window works it out: the file's check, then the engines as checked now. */
    private static ProjectStatus status(ProjectHub on, RecentEntry entry) throws Exception {
        ProjectFileCheck check = on.checkProject(entry, true).get(10, TimeUnit.SECONDS);
        return ProjectHealthService.evaluate(check, checked(on), entry.engineId(), FolderEditor.NONE);
    }

    /** Pins an entry the way a newer launcher would (VK-1694 owns the UI): straight in the list's file. */
    private void pinInTheFile(UUID id) throws IOException {
        Path file = configDir.resolve(RecentProjectsRepository.FILE_NAME);
        JsonObject root = JsonParser.parseString(Files.readString(file)).getAsJsonObject();
        for (var element : root.getAsJsonArray("projects")) {
            JsonObject entry = element.getAsJsonObject();
            if (id.toString().equals(entry.get(RecentEntry.ID).getAsString())) {
                entry.addProperty(RecentEntry.PINNED, true);
            }
        }
        Files.writeString(file, root.toString());
    }

    /** AC: "A renamed project folder shows Missing; after Locate… it shows OK with its history kept." */
    @Test
    void aRenamedProjectFolderIsMissingAndLocateBringsItBackWithItsHistory() throws Exception {
        UUID registered = addEngine(hub, TestSupport.devEngine(root.resolve("repo")));
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        UUID id = listed(hub, "Game").id();
        hub.bindEngine(id, registered);
        hub.setMcp(id, new McpSetting(true, 7901));
        hub.markOpened(id);
        pinInTheFile(id);
        RecentEntry before = listed(hub, "Game");
        assertEquals(Kind.OK, status(hub, before).kind());

        Files.move(projectsDir.resolve("Game"), projectsDir.resolve("Game Moved"));

        ProjectStatus missing = status(hub, before);
        assertEquals(Kind.MISSING, missing.kind());
        assertTrue(missing.reason().contains("Locate…"), missing.reason());

        PickedProject picked = hub.inspectProject(projectsDir.resolve("Game Moved/Game.vfproj"))
                .get(10, TimeUnit.SECONDS);
        RelinkedProject relinked = hub.relinkProject(id, assertInstanceOf(PickedProject.Found.class, picked));

        assertEquals(RecentProjects.Relink.RELINKED, relinked.outcome());
        RecentEntry after = listed(hub(Map.of()), "Game");
        assertEquals(before.id(), after.id());
        assertEquals(TestSupport.real(projectsDir.resolve("Game Moved/Game.vfproj")), after.path());
        assertTrue(after.pinned());
        assertEquals(before.lastOpened(), after.lastOpened());
        assertEquals(registered.toString(), after.engineId());
        assertEquals(new McpSetting(true, 7901), after.mcp());
        assertEquals(Kind.OK, status(hub, after).kind());
    }

    /** AC: "Remove followed by Undo restores the entry, and no file is ever deleted (tested on a temp project)." */
    @Test
    void removeThenUndoRestoresTheEntryAndNeverTouchesTheProject() throws Exception {
        hub.createProject(TestSupport.request("Keep", projectsDir, engine));
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        RecentEntry before = listed(hub, "Game");
        Map<String, String> files = TestSupport.treeSnapshot(projectsDir);

        RemovedProject removed = hub.removeProject(before.id());

        assertEquals(List.of("Keep"), names(removed.view()));
        assertEquals(files, TestSupport.treeSnapshot(projectsDir), "Remove from List never touches the disk");

        RecentProjectsView restored = hub.restoreProject(removed.removal().orElseThrow());

        assertEquals(before, restored.find(before.id()).orElseThrow(), "the same entry, every member included");
        assertEquals(Set.of("Keep", "Game"), Set.copyOf(names(restored)));
        assertEquals(files, TestSupport.treeSnapshot(projectsDir), "nor does Undo");
    }

    /** AC: "A project saved by a newer engine shows 'Needs VertexForge' and the required version." */
    @Test
    void aProjectSavedByANewerEngineNeedsThatVersion() throws Exception {
        addEngine(hub, TestSupport.devEngine(root.resolve("repo")));
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        Files.writeString(projectsDir.resolve("Game/Game.vfproj"),
                TestSupport.projectJson("Game", "2.0", "VertexForge 2.1.0"));

        ProjectStatus status = status(hub, listed(hub, "Game"));

        assertEquals(Kind.NEEDS_NEWER_ENGINE, status.kind());
        assertEquals("Needs VertexForge 2.1.0", status.label());
    }

    @Test
    void aProjectTheEditorWouldRefuseIsInvalidWithTheEditorsReason() throws Exception {
        hub.createProject(TestSupport.request("Game", projectsDir, engine));
        Path file = projectsDir.resolve("Game/Game.vfproj");
        JsonObject project = JsonParser.parseString(Files.readString(file)).getAsJsonObject();
        project.remove("startupScene");
        Files.writeString(file, project.toString());

        ProjectStatus status = status(hub, listed(hub, "Game"));

        assertEquals(Kind.INVALID, status.kind());
        assertEquals("Invalid project file: missing or invalid 'startupScene' field", status.reason());
    }
}
