package com.vertexforge.launcher.service;

import com.vertexforge.launcher.engine.EngineCatalog;
import com.vertexforge.launcher.engine.EngineListing;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryRepository;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.engine.PickedEngine;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.project.ProjectInspector;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.session.EditorSession;
import com.vertexforge.launcher.session.LaunchRequest;
import com.vertexforge.launcher.session.SessionLogs;
import com.vertexforge.launcher.session.SessionScheduler;
import com.vertexforge.launcher.session.SessionStart;
import com.vertexforge.launcher.session.StartResult;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettingsRepository;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.util.SingleFlightChecks;

import java.io.IOException;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Duration;
import java.time.ZoneId;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;

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
    private final EngineCatalog engines;
    private final EditorLauncher editorLauncher;
    private final ProjectStatusChecker statusChecker;
    private final LocationChecker locationChecker;
    /** Writes the Settings dialog's choices; {@code null} for a hub that cannot save them (tests). */
    private final LauncherSettingsRepository settingsRepository;
    private final ShellOpener shell;
    private final int listLimit;
    private final Clock clock;
    /** Reads picked, dropped and command-line projects, each with a timeout (VK-1685). */
    private final SingleFlightChecks<Path, PickAnswer> pickChecks;
    /** The settings as last read or saved; written on the store thread, read on any. */
    private volatile LauncherSettings settings;

    /** A hub whose location checks show no warnings (they need the environment; see {@link #create}). */
    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EngineCatalog engines, EditorLauncher editorLauncher,
                      ProjectStatusChecker statusChecker, int listLimit, Clock clock) {
        this(repository, projectCreator, engines, editorLauncher, statusChecker,
                new LocationChecker(LocationWarnings.none(), ProjectTemplate.BUNDLED), listLimit, clock);
    }

    /** A hub with the default settings for everything but the list's cap, which it cannot save. */
    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EngineCatalog engines, EditorLauncher editorLauncher,
                      ProjectStatusChecker statusChecker, LocationChecker locationChecker, int listLimit,
                      Clock clock) {
        this(repository, projectCreator, engines, editorLauncher, statusChecker, locationChecker, null,
                new LauncherSettings(listLimit), clock);
    }

    /**
     * @param settingsRepository where {@link #saveSettings} writes, or {@code null} when it may not
     * @param settings           the settings as read at startup
     */
    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EngineCatalog engines, EditorLauncher editorLauncher,
                      ProjectStatusChecker statusChecker, LocationChecker locationChecker,
                      LauncherSettingsRepository settingsRepository, LauncherSettings settings, Clock clock) {
        this(repository, projectCreator, engines, editorLauncher, statusChecker, locationChecker, settingsRepository,
                settings, clock, ProjectInspector.system(), ProjectStatusChecker.DEFAULT_TIMEOUT);
    }

    /**
     * @param inspector   reads the projects picked with "Add Existing…", dropped or given on the command line (VK-1685)
     * @param pickTimeout how long one such read may take before it is refused
     */
    ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
               EngineCatalog engines, EditorLauncher editorLauncher,
               ProjectStatusChecker statusChecker, LocationChecker locationChecker,
               LauncherSettingsRepository settingsRepository, LauncherSettings settings, Clock clock,
               ProjectInspector inspector, Duration pickTimeout) {
        this.repository = Objects.requireNonNull(repository, "repository");
        this.projectCreator = Objects.requireNonNull(projectCreator, "projectCreator");
        this.engines = Objects.requireNonNull(engines, "engines");
        this.editorLauncher = Objects.requireNonNull(editorLauncher, "editorLauncher");
        this.statusChecker = Objects.requireNonNull(statusChecker, "statusChecker");
        this.locationChecker = Objects.requireNonNull(locationChecker, "locationChecker");
        this.settingsRepository = settingsRepository;
        this.settings = Objects.requireNonNull(settings, "settings");
        this.shell = new ShellOpener(editorLauncher.processStarter());
        this.clock = Objects.requireNonNull(clock, "clock");
        this.listLimit = settings.recentProjectsLimit();
        ProjectInspector reader = Objects.requireNonNull(inspector, "inspector");
        Objects.requireNonNull(pickTimeout, "pickTimeout");
        this.pickChecks = new SingleFlightChecks<>("vf-project-pick-", pickTimeout,
                picked -> {
                    PickedProject result = reader.inspect(picked);
                    return result == null ? null : new PickAnswer(result, null);
                },
                new PickAnswer(null, "The project could not be checked"),
                e -> new PickAnswer(null, "The project could not be checked: " + e),
                new PickAnswer(null, "No response after " + SingleFlightChecks.describe(pickTimeout)
                        + ": it may be in an unreachable network folder"),
                new PickAnswer(null, "The launcher is closing"));
    }

    /** A pick check's answer: what the inspector found, or why there is nothing. */
    private record PickAnswer(PickedProject result, String problem) {
    }

    /**
     * Wires the hub for an environment: the recent list lives in {@code configDir/recent-projects.json}, the settings
     * (the list's cap, the launch behaviour) in {@code configDir/launcher-settings.json}, the engines in
     * {@code configDir/engines.json} and the per-launch logs in {@code configDir/logs/} (VK-1683). Reads only the
     * (small, local) settings file.
     */
    public static ProjectHub create(LauncherEnvironment environment, LauncherLog log) {
        return create(environment, log, ProjectInspector.system(), ProjectStatusChecker.DEFAULT_TIMEOUT);
    }

    /** As {@link #create(LauncherEnvironment, LauncherLog)}, reading picked projects with {@code inspector}. */
    static ProjectHub create(LauncherEnvironment environment, LauncherLog log, ProjectInspector inspector,
                             Duration pickTimeout) {
        Path configDir = environment.configDir();
        LauncherSettingsRepository settingsRepository = new LauncherSettingsRepository(
                configDir.resolve(LauncherSettingsRepository.FILE_NAME), environment.clock(), log);
        LauncherSettings settings = settingsRepository.load();
        RecentProjectsRepository repository = new RecentProjectsRepository(
                RecentProjectsRepository.openStore(configDir.resolve(RecentProjectsRepository.FILE_NAME),
                        environment.clock(), log),
                UUID::randomUUID, environment.clock(), log);
        EditorLauncher editorLauncher = new EditorLauncher(environment.processStarter(),
                new SessionLogs(configDir.resolve(SessionLogs.DIRECTORY_NAME), log::warn),
                environment.nativeCharset(), environment.clock(), SessionScheduler.daemon());
        return new ProjectHub(
                repository,
                new ProjectCreator(environment.clock(), log),
                EngineCatalog.create(environment, log),
                editorLauncher,
                new ProjectStatusChecker(PathProbe.system(), ProjectStatusChecker.DEFAULT_TIMEOUT),
                new LocationChecker(LocationWarnings.forEnvironment(environment), ProjectTemplate.BUNDLED),
                settingsRepository,
                settings,
                environment.clock(),
                inspector,
                pickTimeout);
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
     * Records that a project was just opened and, when {@code bindEngine} is given, binds it to that engine, in one
     * write (an unbound project's first open, or a choice the user asked to remember; VK-1681).
     */
    public RecentProjectsView markOpened(UUID id, Optional<UUID> bindEngine) throws StoreWriteException {
        return repository.markOpened(id, bindEngine);
    }

    /** Binds a project to an engine; {@code null} makes it follow the default engine again (VK-1681). */
    public RecentProjectsView bindEngine(UUID id, UUID engineId) throws StoreWriteException {
        return repository.bindEngine(id, engineId);
    }

    /** Sets a project's "Open with MCP" (VK-1683). Store thread. */
    public RecentProjectsView setMcp(UUID id, McpSetting setting) throws StoreWriteException {
        return repository.setMcp(id, setting);
    }

    /** The settings as last read or saved. Any thread. */
    public LauncherSettings settings() {
        return settings;
    }

    /**
     * Saves the launch behaviour (VK-1683) and returns the settings as written. Store thread.
     *
     * @throws IllegalStateException when this hub cannot save settings
     */
    public LauncherSettings saveSettings(LauncherSettings.AfterLaunch afterLaunch, boolean showEditorConsole)
            throws StoreWriteException {
        if (settingsRepository == null) {
            throw new IllegalStateException("This launcher cannot save its settings");
        }
        LauncherSettings saved = settingsRepository.save(afterLaunch, showEditorConsole);
        // The list's cap is read once at startup; keep it as the list uses it.
        settings = new LauncherSettings(listLimit, saved.afterLaunch(), saved.showEditorConsole());
        return settings;
    }

    /** Whether {@link #saveSettings} may be called. */
    public boolean canSaveSettings() {
        return settingsRepository != null;
    }

    /** Where the per-launch logs are kept ({@code ~/.vertexforge/logs}). */
    public Path sessionLogsDirectory() {
        return editorLauncher.logs().directory();
    }

    /** Opens files and folders in Windows (Open log, Open crash folder). */
    public ShellOpener shell() {
        return shell;
    }

    /**
     * Creates a project and, when that succeeds, adds it to the recent list under its canonical path. A project that
     * was created but could not be listed is reported through {@link CreateOutcome#listError()}.
     * <p>
     * Both steps run on the calling thread. The window runs them on two threads instead ({@link CreationJob}), so that
     * a slow creation never holds up the list.
     */
    public CreateOutcome createProject(String name, Path location) {
        return listCreated(createProjectFiles(new CreateRequest(name, location), CreationMonitor.NONE), name);
    }

    /** Creates the project's files (VK-1679); never touches the recent list. Never throws. */
    public ProjectCreationResult createProjectFiles(CreateRequest request, CreationMonitor monitor) {
        return projectCreator.create(request, monitor);
    }

    /**
     * Adds a project {@link #createProjectFiles} just created to the recent list, under its canonical path. A failed or
     * cancelled creation adds nothing.
     */
    public CreateOutcome listCreated(ProjectCreationResult result, String name) {
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

    // ---------------------------------------------------------------------------------------------------------------
    // Existing projects (VK-1685)
    // ---------------------------------------------------------------------------------------------------------------

    /**
     * Reads a project file, or the project folder holding it, that was picked, dropped or given on the command line.
     * Runs in the background, one read per path at a time, and never takes longer than the pick timeout: a read that
     * has not answered by then is refused. Never completes exceptionally; registers nothing.
     */
    public CompletableFuture<PickedProject> inspectProject(Path picked) {
        Objects.requireNonNull(picked, "picked");
        return pickChecks.check(picked.toAbsolutePath().normalize()).thenApply(answer -> answer.result() != null
                ? answer.result()
                : new PickedProject.Refused(picked, answer.problem()));
    }

    /**
     * Lists a project {@link #inspectProject} found, under its real path, so that every spelling of its path gives
     * one entry; a listed project only gets its name refreshed from the file. Store thread.
     */
    public AddedProject addExisting(PickedProject.Found found) throws StoreWriteException {
        Objects.requireNonNull(found, "found");
        return repository.addExisting(found.file(), found.project().projectName(), listLimit);
    }

    /** Whether a project called {@code name} can be created in {@code location}, checked in the background. */
    public CompletableFuture<LocationChecker.LocationCheck> checkLocation(Path location, String name) {
        return locationChecker.check(location, name);
    }

    /** The real path of a file that was just created, so it is listed under one spelling; else absolute + normalized. */
    static Path canonical(Path path) {
        try {
            return path.toRealPath();
        } catch (IOException e) {
            return path.toAbsolutePath().normalize();
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Engines (VK-1681)
    // ---------------------------------------------------------------------------------------------------------------

    /** Reads the engines file, registering the running launcher's bundled engine first if needed. Store thread. */
    public EngineListing listEngines() {
        return engines.load();
    }

    /** Checks every listed engine in the background, each with its own timeout. Never completes exceptionally. */
    public CompletableFuture<EngineOptions> checkEngines(EngineListing listing) {
        return engines.check(listing);
    }

    /** Which engine opens a project bound to {@code engineId} ({@code null}: unbound). Pure; any thread. */
    public LaunchPlan planLaunch(EngineOptions options, String engineId) {
        return EngineResolver.resolve(options, engineId);
    }

    /** Checks a file picked with "Add Engine…" in the background; registers nothing. */
    public CompletableFuture<PickedEngine> inspectEngine(Path picked) {
        return engines.inspect(picked);
    }

    /** Registers an engine {@link #inspectEngine} found usable. Store thread. */
    public EngineRegistryRepository.Added addEngine(PickedEngine.Usable engine) throws StoreWriteException {
        return engines.add(engine);
    }

    /** Unregisters an engine; its files are never touched, and projects bound to it ask on their next open. */
    public EngineRegistryView removeEngine(UUID id) throws StoreWriteException {
        return engines.remove(id);
    }

    /** Makes {@code id} the default engine; {@code null} clears the default. Store thread. */
    public EngineRegistryView setDefaultEngine(UUID id) throws StoreWriteException {
        return engines.setDefault(id);
    }

    /**
     * Starts the request's editor on its project, in the engine's working directory, with its output going to a new
     * per-launch log (VK-1683). Completes on a background thread; nothing watches the editor until
     * {@link #newSession}'s session begins.
     */
    public CompletableFuture<StartResult> launch(LaunchRequest request) {
        return editorLauncher.start(request);
    }

    /** The session that watches an editor {@link #launch} started; call {@link EditorSession#begin()} once listed. */
    public EditorSession newSession(SessionStart start, Executor events, EditorSession.Listener listener) {
        return editorLauncher.newSession(start, events, listener);
    }

    /** How many projects the list keeps. */
    public int listLimit() {
        return listLimit;
    }

    /** The time zone the UI shows times in. */
    public ZoneId zone() {
        return clock.getZone();
    }

    /** Stops the status, location, pick, engine and session threads. Running editors are never touched. */
    @Override
    public void close() {
        pickChecks.close();
        statusChecker.close();
        locationChecker.close();
        engines.close();
        editorLauncher.close();
    }
}
