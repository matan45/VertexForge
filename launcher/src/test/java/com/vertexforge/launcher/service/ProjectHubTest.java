package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.TickingClock;
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
        LauncherEnvironment environment = TestSupport.environment(configDir, root, new TickingClock(TestSupport.T0),
                variables, starter);
        ProjectHub created = ProjectHub.create(environment, TestSupport.log(root.resolve("logs")).log);
        hubs.add(created);
        return created;
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
    void findEditorUsesTheInjectedEnvironment() throws IOException {
        Path editor = Files.createDirectories(root.resolve("custom")).resolve("Editor.exe");
        Files.writeString(editor, "fake");
        editor.toFile().setExecutable(true);

        assertEquals(Optional.of(editor), hub(Map.of(EditorLocator.EDITOR_ENV_VAR, editor.toString())).findEditor());
        assertEquals(Optional.empty(), hub.findEditor());
    }

    @Test
    void launchEditorGoesThroughTheInjectedProcessStarter() throws IOException {
        hub.createProject("Game", projectsDir);
        Path editor = Files.createDirectories(root.resolve("custom")).resolve("Editor.exe");
        Files.writeString(editor, "fake");
        editor.toFile().setExecutable(true);
        Path project = projectsDir.resolve("Game/Game.vfproj");

        LaunchResult result = hub.launchEditor(editor, project).join();

        assertEquals(LaunchResult.ok(), result);
        assertEquals(List.of(editor.toString(), project.toString()), starter.started.getFirst().command());
    }
}
