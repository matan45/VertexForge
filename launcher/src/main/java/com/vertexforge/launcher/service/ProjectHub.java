package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.service.EditorLauncher.LaunchResult;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettingsRepository;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;

import java.io.IOException;
import java.nio.file.Path;
import java.time.Clock;
import java.time.ZoneId;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;

/**
 * The launcher's project operations, free of JavaFX so that they run in plain unit tests (VK-1677).
 * {@code MainController} only turns these into UI.
 * <p>
 * Every method here may block (file locks, project creation), so the UI calls them off the FX thread (VK-1678).
 * Listing never checks the projects themselves; {@link #checkStatus} does that, in the background, with a timeout.
 */
public class ProjectHub implements AutoCloseable {

    private final RecentProjectsRepository repository;
    private final ProjectCreator projectCreator;
    private final EditorLocator editorLocator;
    private final EditorLauncher editorLauncher;
    private final ProjectStatusChecker statusChecker;
    private final int listLimit;
    private final Clock clock;

    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EditorLocator editorLocator, EditorLauncher editorLauncher,
                      ProjectStatusChecker statusChecker, int listLimit, Clock clock) {
        this.repository = Objects.requireNonNull(repository, "repository");
        this.projectCreator = Objects.requireNonNull(projectCreator, "projectCreator");
        this.editorLocator = Objects.requireNonNull(editorLocator, "editorLocator");
        this.editorLauncher = Objects.requireNonNull(editorLauncher, "editorLauncher");
        this.statusChecker = Objects.requireNonNull(statusChecker, "statusChecker");
        this.clock = Objects.requireNonNull(clock, "clock");
        // Validates the range.
        this.listLimit = new LauncherSettings(listLimit).recentProjectsLimit();
    }

    /**
     * Wires the hub for an environment: the recent list lives in {@code configDir/recent-projects.json} and its cap
     * comes from {@code configDir/launcher-settings.json}. Reads only the (small, local) settings file.
     */
    public static ProjectHub create(LauncherEnvironment environment, LauncherLog log) {
        Path configDir = environment.configDir();
        LauncherSettings settings = new LauncherSettingsRepository(
                configDir.resolve(LauncherSettingsRepository.FILE_NAME), environment.clock(), log).load();
        RecentProjectsRepository repository = new RecentProjectsRepository(
                RecentProjectsRepository.openStore(configDir.resolve(RecentProjectsRepository.FILE_NAME),
                        environment.clock(), log),
                UUID::randomUUID, environment.clock(), log);
        return new ProjectHub(
                repository,
                new ProjectCreator(environment.clock(), log),
                new EditorLocator(environment.env(), environment.workingDir()),
                new EditorLauncher(environment.processStarter()),
                new ProjectStatusChecker(PathProbe.system(), ProjectStatusChecker.DEFAULT_TIMEOUT),
                settings.recentProjectsLimit(),
                environment.clock());
    }

    /** The recent projects, most recently opened first, with the state of the list's file. Never probes a project. */
    public RecentProjectsView listProjects() {
        return repository.load();
    }

    /** Whether the entry's project file can be opened; completes within the checker's timeout. */
    public CompletableFuture<ProjectStatus> checkStatus(RecentEntry entry) {
        return statusChecker.check(entry);
    }

    /** Removes the entry from the list; the project on disk is never touched. */
    public RecentProjectsView removeProject(UUID id) throws StoreWriteException {
        return repository.remove(id);
    }

    /** Records that a project was just opened, which moves it to the top of the list. */
    public RecentProjectsView markOpened(UUID id) throws StoreWriteException {
        return repository.markOpened(id);
    }

    /**
     * Creates a project and, when that succeeds, adds it to the recent list under its canonical path. A project that
     * was created but could not be listed is reported through {@link CreateOutcome#listError()}.
     */
    public CreateOutcome createProject(String name, Path location) {
        ProjectCreationResult result = projectCreator.createProject(name, location);
        if (!result.success()) {
            return CreateOutcome.failed(result);
        }

        Path projectPath = canonical(result.projectPath());
        try {
            return CreateOutcome.listed(result, projectPath, repository.add(projectPath, name, listLimit));
        } catch (StoreWriteException e) {
            return CreateOutcome.notListed(result, projectPath, e);
        }
    }

    /** The real path of a file that was just created, so it is listed under one spelling; else absolute + normalized. */
    static Path canonical(Path path) {
        try {
            return path.toRealPath();
        } catch (IOException e) {
            return path.toAbsolutePath().normalize();
        }
    }

    public Optional<Path> findEditor() {
        return editorLocator.findEditorExecutable();
    }

    public CompletableFuture<LaunchResult> launchEditor(Path editorPath, Path projectPath) {
        return editorLauncher.launchAsync(editorPath, projectPath);
    }

    /** How many projects the list keeps. */
    public int listLimit() {
        return listLimit;
    }

    /** The time zone the UI shows times in. */
    public ZoneId zone() {
        return clock.getZone();
    }

    /** Stops the status checks' threads. */
    @Override
    public void close() {
        statusChecker.close();
    }
}
