package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.TickingClock;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.service.EditorLauncher.LaunchResult;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import com.vertexforge.launcher.service.EditorLauncherTest.RecordingStarter;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectHubTest {

    @TempDir
    Path tempDir;

    private Path configDir;
    private Path projectsDir;
    private RecordingStarter starter;
    private ProjectHub hub;

    @BeforeEach
    void setUp() throws IOException {
        configDir = tempDir.resolve("config");
        projectsDir = Files.createDirectories(tempDir.resolve("projects"));
        starter = new RecordingStarter(FakeProcess.running(), null);
        hub = hub(Map.of());
    }

    private ProjectHub hub(Map<String, String> variables) {
        LauncherEnvironment environment = TestSupport.environment(configDir, tempDir, new TickingClock(TestSupport.T0),
                variables, starter);
        return ProjectHub.create(environment, TestSupport.log(tempDir.resolve("logs")).log);
    }

    private static List<String> names(List<ProjectInfo> projects) {
        return projects.stream().map(ProjectInfo::name).toList();
    }

    @Test
    void anEmptyStoreListsNothing() {
        assertTrue(hub.listProjects().isEmpty());
        assertFalse(Files.exists(configDir), "listing must not write the store");
    }

    @Test
    void aCreatedProjectIsListedAndStored() {
        ProjectCreationResult result = hub.createProject("Game", projectsDir);

        assertTrue(result.success(), () -> result.errorMessage());
        List<ProjectInfo> projects = hub.listProjects();
        assertEquals(1, projects.size());
        assertEquals("Game", projects.getFirst().name());
        assertEquals(projectsDir.resolve("Game/Game.vfproj"), projects.getFirst().path());
        assertTrue(Files.isRegularFile(configDir.resolve("recent-projects.json")));
    }

    @Test
    void aFailedCreationAddsNothing() throws IOException {
        Path blocker = Files.writeString(tempDir.resolve("blocker"), "file");

        ProjectCreationResult result = hub.createProject("Game", blocker);

        assertFalse(result.success());
        assertTrue(hub.listProjects().isEmpty());
    }

    @Test
    void theNewestProjectComesFirstAndOpeningMovesAProjectToTheTop() {
        hub.createProject("First", projectsDir);
        hub.createProject("Second", projectsDir);
        assertEquals(List.of("Second", "First"), names(hub.listProjects()));

        hub.markOpened(projectsDir.resolve("First/First.vfproj"));

        assertEquals(List.of("First", "Second"), names(hub.listProjects()));
    }

    @Test
    void removeDropsTheProjectFromTheList() {
        hub.createProject("Keep", projectsDir);
        hub.createProject("Drop", projectsDir);

        hub.removeProject(projectsDir.resolve("Drop/Drop.vfproj"));

        assertEquals(List.of("Keep"), names(hub.listProjects()));
    }

    /**
     * Characterization: loading already drops projects whose file is missing, so Refresh never finds an invalid
     * one and never asks. VK-1678 keeps missing projects (with a status) and must replace this test.
     */
    @Test
    void refreshNeverAsksBecauseLoadingAlreadyDropsMissingProjects() throws IOException {
        hub.createProject("Game", projectsDir);
        hub.createProject("Gone", projectsDir);
        Files.delete(projectsDir.resolve("Gone/Gone.vfproj"));
        AtomicInteger asked = new AtomicInteger();

        hub.refreshInvalid(count -> {
            asked.incrementAndGet();
            return true;
        });

        assertEquals(0, asked.get());
        assertEquals(List.of("Game"), names(hub.listProjects()));
    }

    @Test
    void findEditorUsesTheInjectedEnvironment() throws IOException {
        Path editor = Files.createDirectories(tempDir.resolve("custom")).resolve("Editor.exe");
        Files.writeString(editor, "fake");
        editor.toFile().setExecutable(true);

        assertEquals(Optional.of(editor), hub(Map.of(EditorLocator.EDITOR_ENV_VAR, editor.toString())).findEditor());
        assertEquals(Optional.empty(), hub.findEditor());
    }

    @Test
    void launchEditorGoesThroughTheInjectedProcessStarter() throws IOException {
        hub.createProject("Game", projectsDir);
        Path editor = Files.createDirectories(tempDir.resolve("custom")).resolve("Editor.exe");
        Files.writeString(editor, "fake");
        editor.toFile().setExecutable(true);
        Path project = projectsDir.resolve("Game/Game.vfproj");

        LaunchResult result = hub.launchEditor(editor, project).join();

        assertEquals(LaunchResult.ok(), result);
        assertEquals(List.of(editor.toString(), project.toString()), starter.started.getFirst().command());
    }
}
