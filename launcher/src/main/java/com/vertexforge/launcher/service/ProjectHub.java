package com.vertexforge.launcher.service;

import com.vertexforge.launcher.engine.EngineCatalog;
import com.vertexforge.launcher.engine.EngineContent;
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
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.ProjectInspector;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.repository.RelinkedProject;
import com.vertexforge.launcher.repository.RemovedProject;
import com.vertexforge.launcher.session.EditorSession;
import com.vertexforge.launcher.session.LaunchRequest;
import com.vertexforge.launcher.session.SessionLogs;
import com.vertexforge.launcher.session.SessionScheduler;
import com.vertexforge.launcher.session.SessionStart;
import com.vertexforge.launcher.session.StartResult;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettingsRepository;
import com.vertexforge.launcher.settings.LauncherState;
import com.vertexforge.launcher.settings.LauncherStateRepository;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.template.ProjectBlueprint;
import com.vertexforge.launcher.template.TemplateCatalogs;
import com.vertexforge.launcher.template.TemplateScanner;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.util.SingleFlightChecks;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Duration;
import java.time.ZoneId;
import java.util.List;
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
 * Listing never checks the projects themselves; {@link #checkProject} does that, in the background, with a timeout.
 */
public class ProjectHub implements AutoCloseable {

    private final RecentProjectsRepository repository;
    private final ProjectCreator projectCreator;
    private final EngineCatalog engines;
    private final EditorLauncher editorLauncher;
    private final ProjectHealthService health;
    private final LocationChecker locationChecker;
    /** Reads what each engine offers for new projects, with a timeout (VK-1689). */
    private final TemplateCatalogs templates;
    /** Writes the Settings dialog's choices; {@code null} for a hub that cannot save them (tests). */
    private final LauncherSettingsRepository settingsRepository;
    private final ShellOpener shell;
    /** "Back up and open" (VK-1690). */
    private final ProjectBackupService backups;
    /** "Open a copy" (VK-1690). */
    private final ProjectCopier copier;
    /** "Update Script Library…" (VK-1691). */
    private final ScriptLibraryUpdater libraryUpdater;
    /** The project list's "Library outdated" tags (VK-1691). */
    private final ScriptLibraryStatusService libraryStatus;
    /** Where the window and the list's sort are remembered (VK-1694); {@code null} for a hub that keeps nothing. */
    private final LauncherStateRepository stateRepository;
    private final Clock clock;
    /** Reads picked, dropped and command-line projects, each with a timeout (VK-1685). */
    private final SingleFlightChecks<Path, PickAnswer> pickChecks;
    /**
     * The settings as last read or saved; written on the store thread, read on any. The list's cap is read from here
     * on every add, so a cap changed in Settings applies at once (VK-1694).
     */
    private volatile LauncherSettings settings;
    /** What was remembered at startup (VK-1694). */
    private final LauncherState state;

    /** A hub whose location checks show no warnings (they need the environment; see {@link #create}). */
    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EngineCatalog engines, EditorLauncher editorLauncher,
                      ProjectHealthService health, int listLimit, Clock clock) {
        this(repository, projectCreator, engines, editorLauncher, health,
                new LocationChecker(LocationWarnings.none()), listLimit, clock);
    }

    /** A hub with the default settings for everything but the list's cap, which it cannot save. */
    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EngineCatalog engines, EditorLauncher editorLauncher,
                      ProjectHealthService health, LocationChecker locationChecker, int listLimit,
                      Clock clock) {
        this(repository, projectCreator, engines, editorLauncher, health, locationChecker, null,
                new LauncherSettings(listLimit), clock);
    }

    /**
     * @param settingsRepository where {@link #saveSettings} writes, or {@code null} when it may not
     * @param settings           the settings as read at startup
     */
    public ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
                      EngineCatalog engines, EditorLauncher editorLauncher,
                      ProjectHealthService health, LocationChecker locationChecker,
                      LauncherSettingsRepository settingsRepository, LauncherSettings settings, Clock clock) {
        this(repository, projectCreator, engines, editorLauncher, health, locationChecker, settingsRepository,
                settings, null, clock, ProjectInspector.system(), ProjectHealthService.DEFAULT_TIMEOUT);
    }

    /**
     * @param stateRepository where the window and the list's sort are remembered (VK-1694), or {@code null} to
     *                        remember nothing; it is read here, once
     * @param inspector       reads the projects picked with "Add Existing…", dropped or given on the command line
     *                        (VK-1685)
     * @param pickTimeout     how long one such read may take before it is refused
     */
    ProjectHub(RecentProjectsRepository repository, ProjectCreator projectCreator,
               EngineCatalog engines, EditorLauncher editorLauncher,
               ProjectHealthService health, LocationChecker locationChecker,
               LauncherSettingsRepository settingsRepository, LauncherSettings settings,
               LauncherStateRepository stateRepository, Clock clock,
               ProjectInspector inspector, Duration pickTimeout) {
        this.repository = Objects.requireNonNull(repository, "repository");
        this.projectCreator = Objects.requireNonNull(projectCreator, "projectCreator");
        this.engines = Objects.requireNonNull(engines, "engines");
        this.editorLauncher = Objects.requireNonNull(editorLauncher, "editorLauncher");
        this.health = Objects.requireNonNull(health, "health");
        this.locationChecker = Objects.requireNonNull(locationChecker, "locationChecker");
        this.templates = new TemplateCatalogs(TemplateScanner.SYSTEM, TemplateCatalogs.DEFAULT_TIMEOUT);
        this.settingsRepository = settingsRepository;
        this.settings = Objects.requireNonNull(settings, "settings");
        this.shell = new ShellOpener(editorLauncher.processStarter());
        this.clock = Objects.requireNonNull(clock, "clock");
        this.backups = new ProjectBackupService(clock, SpaceProbe.SYSTEM);
        this.copier = new ProjectCopier(SpaceProbe.SYSTEM);
        this.libraryUpdater = new ScriptLibraryUpdater(clock, SpaceProbe.SYSTEM, EditorLockProbe.system());
        this.libraryStatus = new ScriptLibraryStatusService(ScriptLibraryStatusService.DEFAULT_TIMEOUT);
        this.stateRepository = stateRepository;
        this.state = stateRepository == null ? LauncherState.EMPTY : stateRepository.load();
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
     * (the list's cap, the launch behaviour, VK-1694's preferences) in {@code configDir/launcher-settings.json}, what
     * the launcher remembers by itself in {@code configDir/launcher-state.json} (VK-1694), the engines in
     * {@code configDir/engines.json} and the per-launch logs in {@code configDir/logs/} (VK-1683). Reads only the
     * (small, local) settings and state files.
     */
    public static ProjectHub create(LauncherEnvironment environment, LauncherLog log) {
        return create(environment, log, ProjectInspector.system(), ProjectHealthService.DEFAULT_TIMEOUT);
    }

    /** As {@link #create(LauncherEnvironment, LauncherLog)}, reading picked projects with {@code inspector}. */
    static ProjectHub create(LauncherEnvironment environment, LauncherLog log, ProjectInspector inspector,
                             Duration pickTimeout) {
        Path configDir = environment.configDir();
        LauncherSettingsRepository settingsRepository = new LauncherSettingsRepository(
                configDir.resolve(LauncherSettingsRepository.FILE_NAME), environment.clock(), log);
        LauncherSettings settings = settingsRepository.load();
        LauncherStateRepository stateRepository = new LauncherStateRepository(
                configDir.resolve(LauncherStateRepository.FILE_NAME), environment.clock(), log);
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
                new ProjectHealthService(PathProbe.system(), EditorLockProbe.system()::probe,
                        ProjectHealthService.DEFAULT_TIMEOUT),
                new LocationChecker(LocationWarnings.forEnvironment(environment)),
                settingsRepository,
                settings,
                stateRepository,
                environment.clock(),
                inspector,
                pickTimeout);
    }

    /** The recent projects, most recently opened first, with the state of the list's file. Never probes a project. */
    public RecentProjectsView listProjects() {
        return repository.load();
    }

    /**
     * What the entry's project file holds and whether another editor has its folder open (VK-1686); completes within
     * the health check's timeout and never exceptionally. {@link ProjectHealthService#evaluate} turns the answer into
     * the row's status.
     *
     * @param probeLock whether to look at the folder's editor lock (not while an editor of this launcher has it open)
     */
    public CompletableFuture<ProjectFileCheck> checkProject(RecentEntry entry, boolean probeLock) {
        return health.check(entry, probeLock);
    }

    /**
     * As {@link #checkProject}, for any project file: Open reads the file again right before it decides whether the
     * engine reads the project (VK-1690), since the row's answer may be old. Never completes exceptionally.
     */
    public CompletableFuture<ProjectFileCheck> checkProjectFile(Path projectFile, boolean probeLock) {
        return health.check(projectFile, probeLock);
    }

    /**
     * Removes the entry from the list; the project on disk is never touched. The removal is what
     * {@link #restoreProject} needs to undo it (VK-1686). Store thread.
     */
    public RemovedProject removeProject(UUID id) throws StoreWriteException {
        return repository.remove(id);
    }

    /** Puts a removed entry back as it was (Undo, VK-1686); nothing when it is listed again meanwhile. Store thread. */
    public RecentProjectsView restoreProject(RemovedProject.Removal removal) throws StoreWriteException {
        return repository.restore(removal);
    }

    /**
     * Points the entry at the project file {@link #inspectProject} found where it was moved to (Locate…, VK-1686),
     * keeping its history (id, pin, time, engine binding, MCP setting). Store thread.
     */
    public RelinkedProject relinkProject(UUID id, PickedProject.Found found) throws StoreWriteException {
        Objects.requireNonNull(found, "found");
        return repository.relink(id, found.file(), found.project().projectName());
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

    /** Pins a project to the top of the list or unpins it (VK-1694). Store thread. */
    public RecentProjectsView setPinned(UUID id, boolean pinned) throws StoreWriteException {
        return repository.setPinned(id, pinned);
    }

    /**
     * Removes the oldest unpinned entries beyond the list's cap at once (a cap lowered in Settings, VK-1694); pinned
     * entries are always kept and no project on disk is touched. Store thread.
     */
    public RecentProjectsView trimProjects() throws StoreWriteException {
        return repository.trim(listLimit());
    }

    /** Sets a project's "Open with MCP" (VK-1683). Store thread. */
    public RecentProjectsView setMcp(UUID id, McpSetting setting) throws StoreWriteException {
        return repository.setMcp(id, setting);
    }

    /** Records that the project's "unknown version" warning was shown, so it is shown once (VK-1690). Store thread. */
    public RecentProjectsView setUnknownVersionWarned(UUID id) throws StoreWriteException {
        return repository.setUnknownVersionWarned(id);
    }

    /**
     * Zips the project beside its folder (VK-1690), leaving out the editor's state and the compiled scripts. Blocks
     * for as long as the backup takes: the window runs it as a {@link FileJob} on its creation thread. Never throws.
     */
    public ProjectBackupService.BackupOutcome backUpProject(ProjectBackupService.BackupRequest request,
                                                           JobMonitor monitor) {
        return backups.backUp(request, monitor);
    }

    /**
     * Copies the project folder beside it under a new name (VK-1690); the original is only read. Blocks for as long as
     * the copy takes: the window runs it as a {@link FileJob} on its creation thread. Never throws.
     */
    public ProjectCopier.CopyOutcome copyProject(ProjectCopier.CopyRequest request, JobMonitor monitor) {
        return copier.copy(request, monitor);
    }

    /**
     * What updating the project's script library to an engine's would do (VK-1691). Only reads. Blocks: a job's
     * thread.
     */
    public ScriptLibraryUpdater.PreviewOutcome previewScriptLibrary(ScriptLibraryUpdater.Request request,
                                                                    JobMonitor monitor) {
        return libraryUpdater.preview(request, monitor);
    }

    /**
     * Updates the project's script library as previewed with {@code fingerprint} (VK-1691). Blocks: a job's thread.
     *
     * @param keepMine whether files the user and the engine both changed keep the user's copy
     */
    public ScriptLibraryUpdater.UpdateOutcome updateScriptLibrary(ScriptLibraryUpdater.Request request,
                                                                  String fingerprint, boolean keepMine,
                                                                  JobMonitor monitor) {
        return libraryUpdater.update(request, fingerprint, keepMine, monitor);
    }

    /** Finishes or undoes an interrupted update of the project's script library (VK-1691). Blocks: a job's thread. */
    public ScriptLibraryUpdater.RecoveryOutcome recoverScriptLibrary(Path projectFile, String workingDirectory) {
        return libraryUpdater.recover(projectFile, workingDirectory);
    }

    /**
     * Whether the project's script library is behind {@code engine}'s, for the list's tag (VK-1691); completes within
     * a timeout and never exceptionally.
     */
    public CompletableFuture<ScriptLibraryStatusService.LibraryState> checkScriptLibrary(
            Path projectFile, String workingDirectory, ScriptLibraryStatusService.EngineLibrary engine) {
        return libraryStatus.check(projectFile, workingDirectory, engine);
    }

    /**
     * Whether an update of the project's script library was interrupted, for Open to finish it first (VK-1691);
     * {@code false} when that cannot be told in time. Never completes exceptionally.
     */
    public CompletableFuture<Boolean> scriptLibraryUpdateInterrupted(Path projectFile, String workingDirectory) {
        return libraryStatus.interrupted(projectFile, workingDirectory);
    }

    /**
     * Lists a copy made by "Open a copy" (VK-1690) under its real path, bound to {@code engineId} when given, in one
     * write; the original's entry is never touched. Store thread.
     */
    public AddedProject listCopy(Path projectFile, String name, Optional<UUID> engineId) throws StoreWriteException {
        Objects.requireNonNull(projectFile, "projectFile");
        return repository.addBound(canonical(projectFile), name, listLimit(), engineId);
    }

    /** The settings as last read or saved. Any thread. */
    public LauncherSettings settings() {
        return settings;
    }

    /**
     * Saves the settings (VK-1683, VK-1694) and returns them as written; they apply at once. Fewer logs to keep prunes
     * the logs folder now rather than at the next launch (a running editor's log is never deleted). The list is not
     * trimmed to a lowered cap here: that is {@link #trimProjects}. Store thread.
     *
     * @throws IllegalStateException when this hub cannot save settings
     */
    public LauncherSettings saveSettings(LauncherSettings wanted) throws StoreWriteException {
        Objects.requireNonNull(wanted, "wanted");
        if (settingsRepository == null) {
            throw new IllegalStateException("This launcher cannot save its settings");
        }
        int keptBefore = settings.logRetention();
        LauncherSettings saved = settingsRepository.save(wanted);
        settings = saved;
        // Before the first launch there is no logs folder, and nothing to prune.
        if (saved.logRetention() < keptBefore && Files.isDirectory(editorLauncher.logs().directory())) {
            editorLauncher.logs().prune(saved.logRetention());
        }
        return saved;
    }

    /**
     * Whether the Settings dialog may save: this hub has a settings file, and that file was not written by a newer
     * launcher (which this version never overwrites, VK-1694). Never touches the disk.
     */
    public boolean canSaveSettings() {
        return settingsRepository != null && !settingsRepository.newerFile();
    }

    /** What the launcher remembered at startup: the window and the list's sort (VK-1694). Any thread. */
    public LauncherState launcherState() {
        return state;
    }

    /** Remembers the main window's bounds (VK-1694); a failure is only a warning. Store thread. */
    public void saveWindowState(LauncherState.SavedWindow window) {
        if (stateRepository != null) {
            stateRepository.saveWindow(window);
        }
    }

    /** Remembers the project list's sort (VK-1694); a failure is only a warning. Store thread. */
    public void saveProjectSort(List<LauncherState.SortKey> sort) {
        if (stateRepository != null) {
            stateRepository.saveProjectSort(sort);
        }
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
     * Creates a project and, when that succeeds, adds it to the recent list under its canonical path, bound to the
     * request's engine. A project that was created but could not be listed is reported through
     * {@link CreateOutcome#listError()}.
     * <p>
     * Both steps run on the calling thread. The window runs them on two threads instead ({@link CreationJob}), so that
     * a slow creation never holds up the list.
     */
    public CreateOutcome createProject(CreateRequest request) {
        return listCreated(createProjectFiles(request, CreationMonitor.NONE), request);
    }

    /** Creates the project's files (VK-1679); never touches the recent list. Never throws. */
    public ProjectCreationResult createProjectFiles(CreateRequest request, CreationMonitor monitor) {
        return projectCreator.create(request, monitor);
    }

    /**
     * Adds a project {@link #createProjectFiles} just created for {@code request} to the recent list, under its
     * canonical path, and binds it to the engine it was created from (VK-1689) in the same write, so it opens with the
     * engine whose library it carries. An engine without an id (the override, an unsaved bundled engine) binds
     * nothing. A failed or cancelled creation adds nothing.
     */
    public CreateOutcome listCreated(ProjectCreationResult result, CreateRequest request) {
        if (!result.success()) {
            return CreateOutcome.failed(result);
        }

        Path projectPath = canonical(result.projectPath());
        try {
            return CreateOutcome.listed(result, projectPath,
                    repository.add(projectPath, request.name(), listLimit(), request.engine().engineId()));
        } catch (StoreWriteException e) {
            return CreateOutcome.notListed(result, projectPath, e);
        }
    }

    /**
     * What {@code engine} offers for new projects: its templates, script library and option folders, read in the
     * background with a timeout. Never completes exceptionally.
     */
    public CompletableFuture<TemplateCatalogs.Answer> listTemplates(EngineContent engine) {
        return templates.list(engine);
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
        return repository.addExisting(found.file(), found.project().projectName(), listLimit());
    }

    /**
     * Whether a project called {@code name}, holding the files of {@code blueprint}, can be created in
     * {@code location}, checked in the background.
     */
    public CompletableFuture<LocationChecker.LocationCheck> checkLocation(Path location, String name,
                                                                          ProjectBlueprint blueprint) {
        return locationChecker.check(location, name, blueprint);
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

    /** How many projects the list keeps, as the settings say now (VK-1694). Any thread. */
    public int listLimit() {
        return settings.recentProjectsLimit();
    }

    /** The time zone the UI shows times in. */
    public ZoneId zone() {
        return clock.getZone();
    }

    /** The launcher's clock (the focus re-check's throttle reads it, VK-1686). */
    public Clock clock() {
        return clock;
    }

    /** Stops the status, location, template, pick, engine and session threads. Running editors are never touched. */
    @Override
    public void close() {
        pickChecks.close();
        health.close();
        libraryStatus.close();
        locationChecker.close();
        templates.close();
        engines.close();
        editorLauncher.close();
    }
}
