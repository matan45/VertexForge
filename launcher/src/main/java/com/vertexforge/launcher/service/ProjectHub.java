package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.service.EditorLauncher.LaunchResult;
import com.vertexforge.launcher.util.LauncherLog;

import java.nio.file.Path;
import java.time.Clock;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.function.IntPredicate;

/**
 * The launcher's project operations, free of JavaFX so that they run in plain unit tests (VK-1677).
 * {@code MainController} only turns these into UI.
 */
public class ProjectHub {

    private final RecentProjectsRepository repository;
    private final ProjectCreator projectCreator;
    private final EditorLocator editorLocator;
    private final EditorLauncher editorLauncher;
    private final Clock clock;

    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EditorLocator editorLocator, EditorLauncher editorLauncher, Clock clock) {
        this.repository = Objects.requireNonNull(repository, "repository");
        this.projectCreator = Objects.requireNonNull(projectCreator, "projectCreator");
        this.editorLocator = Objects.requireNonNull(editorLocator, "editorLocator");
        this.editorLauncher = Objects.requireNonNull(editorLauncher, "editorLauncher");
        this.clock = Objects.requireNonNull(clock, "clock");
    }

    /** Wires the hub for an environment: the recent list lives in {@code configDir/recent-projects.json}. */
    public static ProjectHub create(LauncherEnvironment environment, LauncherLog log) {
        return new ProjectHub(
                new RecentProjectsRepository(environment.configDir().resolve(RecentProjectsRepository.FILE_NAME), log),
                new ProjectCreator(environment.clock(), log),
                new EditorLocator(environment.env(), environment.workingDir()),
                new EditorLauncher(environment.processStarter()),
                environment.clock());
    }

    /** The recent projects, most recently opened first. */
    public List<ProjectInfo> listProjects() {
        return repository.load().getAll();
    }

    public void removeProject(Path projectPath) {
        RecentProjects recentProjects = repository.load();
        recentProjects.remove(projectPath);
        repository.save(recentProjects);
    }

    /**
     * Counts the projects whose file is missing and, when there are any and {@code confirmRemoval} accepts that
     * count, removes them from the list. One load serves both the count and the removal.
     */
    public void refreshInvalid(IntPredicate confirmRemoval) {
        RecentProjects recentProjects = repository.load();
        int invalidCount = recentProjects.countInvalid();
        if (invalidCount > 0 && confirmRemoval.test(invalidCount)) {
            recentProjects.removeInvalid();
            repository.save(recentProjects);
        }
    }

    /** Creates a project and, when that succeeds, adds it to the recent list. */
    public ProjectCreationResult createProject(String name, Path location) {
        ProjectCreationResult result = projectCreator.createProject(name, location);
        if (!result.success()) {
            return result;
        }

        ProjectInfo newProject = new ProjectInfo(name, result.projectPath(), clock.instant());
        RecentProjects recentProjects = repository.load();
        recentProjects.add(newProject);
        repository.save(recentProjects);
        return result;
    }

    public Optional<Path> findEditor() {
        return editorLocator.findEditorExecutable();
    }

    public CompletableFuture<LaunchResult> launchEditor(Path editorPath, Path projectPath) {
        return editorLauncher.launchAsync(editorPath, projectPath);
    }

    /** Records that a project was just opened, which moves it to the top of the list. */
    public void markOpened(Path projectPath) {
        RecentProjects recentProjects = repository.load();
        recentProjects.touch(projectPath, clock.instant());
        repository.save(recentProjects);
    }
}
