package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.dialog.CopyNamePrompt;
import com.vertexforge.launcher.dialog.VersionPrompt;
import com.vertexforge.launcher.dialog.VersionPrompt.Choice;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.service.FileJob;
import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.ProjectBackupService.BackupOutcome;
import com.vertexforge.launcher.service.ProjectBackupService.BackupRequest;
import com.vertexforge.launcher.service.ProjectCopier.CopyOutcome;
import com.vertexforge.launcher.service.ProjectCopier.CopyRequest;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Optional;
import java.util.OptionalLong;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.function.Function;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The open flow without the JavaFX toolkit: every answer, every job and the FX thread are driven by the test. */
class OpenProjectFlowTest {

    private static final Path ROOT = Path.of(TestSupport.isWindows() ? "C:\\projects" : "/projects");
    private static final Path FOLDER = ROOT.resolve("Game");
    private static final Path FILE = FOLDER.resolve("Game.vfproj");
    private static final UUID OLD_ID = UUID.fromString("00000000-0000-0000-0000-000000000100");
    private static final UUID NEW_ID = UUID.fromString("00000000-0000-0000-0000-000000000110");
    private static final UUID NEWEST_ID = UUID.fromString("00000000-0000-0000-0000-000000000120");

    private final QueueExecutor fx = new QueueExecutor();
    private final QueueExecutor jobThread = new QueueExecutor();
    private final FakeServices services = new FakeServices();
    private final OpenProjectFlow flow = new OpenProjectFlow(services, fx);

    /** Runs nothing until told to, like answers waiting for the FX thread or a job waiting for its thread. */
    private static final class QueueExecutor implements Executor {
        private final List<Runnable> queued = new ArrayList<>();

        @Override
        public void execute(Runnable command) {
            queued.add(command);
        }

        void runAll() {
            while (!queued.isEmpty()) {
                queued.removeFirst().run();
            }
        }
    }

    /** One call to {@link OpenProjectFlow.Services#start}. */
    private record Started(EngineOption engine, Path projectFile, String name, McpSetting mcp, UUID id,
                           Optional<UUID> bindTo) {
    }

    private final class FakeServices implements OpenProjectFlow.Services {
        CompletableFuture<EngineOptions> engines = new CompletableFuture<>();
        ProjectFileCheck check;
        FolderEditor folderEditor = FolderEditor.NONE;
        boolean readOnly;
        Function<OpenProjectFlow.EnginePrompt, Optional<OpenProjectFlow.Picked>> chooser = prompt -> Optional.empty();
        final Deque<Choice> answers = new ArrayDeque<>();
        final Deque<Optional<String>> copyNames = new ArrayDeque<>();
        BackupOutcome backupOutcome;
        CopyOutcome copyOutcome;
        CompletableFuture<AddedProject> listing = new CompletableFuture<>();

        final List<Boolean> reads = new ArrayList<>();
        final List<OpenProjectFlow.EnginePrompt> enginePrompts = new ArrayList<>();
        final List<VersionPrompt> prompts = new ArrayList<>();
        final List<CopyNamePrompt> copyPrompts = new ArrayList<>();
        final List<String> jobs = new ArrayList<>();
        final List<Boolean> jobClosed = new ArrayList<>();
        final List<BackupRequest> backups = new ArrayList<>();
        final List<CopyRequest> copies = new ArrayList<>();
        final List<FileJob<?>> started = new ArrayList<>();
        final List<String> listedCopies = new ArrayList<>();
        final List<AddedProject> listed = new ArrayList<>();
        final List<UUID> warned = new ArrayList<>();
        final List<Started> starts = new ArrayList<>();
        int releases;
        final List<String> errors = new ArrayList<>();
        final List<String> notices = new ArrayList<>();
        final List<Throwable> saveFailures = new ArrayList<>();

        @Override
        public CompletableFuture<EngineOptions> engines() {
            return engines;
        }

        @Override
        public CompletableFuture<ProjectFileCheck> read(Path projectFile, boolean probeLock) {
            reads.add(probeLock);
            return CompletableFuture.completedFuture(check);
        }

        @Override
        public FolderEditor folderEditor(Path projectFile) {
            return folderEditor;
        }

        @Override
        public boolean readOnly() {
            return readOnly;
        }

        @Override
        public Optional<OpenProjectFlow.Picked> chooseEngine(OpenProjectFlow.EnginePrompt prompt) {
            enginePrompts.add(prompt);
            return chooser.apply(prompt);
        }

        @Override
        public Choice ask(VersionPrompt prompt) {
            prompts.add(prompt);
            return answers.isEmpty() ? Choice.CANCEL : answers.removeFirst();
        }

        @Override
        public Optional<String> askCopyName(CopyNamePrompt prompt) {
            copyPrompts.add(prompt);
            return copyNames.isEmpty() ? Optional.empty() : copyNames.removeFirst();
        }

        @Override
        public OpenProjectFlow.JobView showJob(String title, String header, Runnable cancel) {
            jobs.add(title);
            int index = jobClosed.size();
            jobClosed.add(false);
            return new OpenProjectFlow.JobView() {
                @Override
                public void progress(JobMonitor.Phase phase, long done, long total) {
                    // Not shown in tests.
                }

                @Override
                public void close() {
                    jobClosed.set(index, true);
                }
            };
        }

        @Override
        public FileJob<BackupOutcome> startBackup(BackupRequest request, FileJob.ProgressListener listener) {
            backups.add(request);
            FileJob<BackupOutcome> job = FileJob.start(jobThread,
                    monitor -> monitor.cancelRequested() ? new BackupOutcome.Cancelled() : backupOutcome, listener,
                    BackupOutcome.Failed::new);
            started.add(job);
            return job;
        }

        @Override
        public FileJob<CopyOutcome> startCopy(CopyRequest request, FileJob.ProgressListener listener) {
            copies.add(request);
            FileJob<CopyOutcome> job = FileJob.start(jobThread,
                    monitor -> monitor.cancelRequested() ? new CopyOutcome.Cancelled() : copyOutcome, listener,
                    CopyOutcome.Failed::new);
            started.add(job);
            return job;
        }

        @Override
        public CompletableFuture<AddedProject> listCopy(Path projectFile, String name, Optional<UUID> engineId) {
            listedCopies.add(projectFile + "|" + name + "|" + engineId.map(UUID::toString).orElse("-"));
            return listing;
        }

        @Override
        public void listed(AddedProject added) {
            listed.add(added);
        }

        @Override
        public void saveFailed(Throwable error) {
            saveFailures.add(error);
        }

        @Override
        public void rememberWarned(UUID id) {
            warned.add(id);
        }

        @Override
        public void start(EngineOption engine, Path projectFile, String name, McpSetting mcp, UUID id,
                          Optional<UUID> bindTo) {
            starts.add(new Started(engine, projectFile, name, mcp, id, bindTo));
        }

        @Override
        public void release() {
            releases++;
        }

        @Override
        public void error(String title, String message) {
            errors.add(title + ": " + message);
        }

        @Override
        public void notice(String message, Path reveal) {
            notices.add(message);
        }
    }

    /** Every open must end in exactly one start or one release, or the window stays disabled (or opens twice). */
    @AfterEach
    void everyOpenEndedExactlyOnce() {
        fx.runAll();
        jobThread.runAll();
        fx.runAll();
        assertEquals(opens, services.starts.size() + services.releases,
                "starts " + services.starts + ", releases " + services.releases);
    }

    private int opens;

    // ---------------------------------------------------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------------------------------------------------

    private static EngineManifest manifest(String engineVersion, String assetFormat, String schema) throws Exception {
        return EngineManifest.parse("{\"manifestVersion\": 1, \"engineVersion\": \"" + engineVersion + "\","
                + (assetFormat == null ? "" : " \"assetFormatVersion\": \"" + assetFormat + "\",")
                + (schema == null ? "" : " \"projectSchemaVersion\": \"" + schema + "\",")
                + " \"configuration\": \"Development\", \"editorExe\": \"Editor.exe\", \"workingDirectory\": \".\"}");
    }

    private static EngineOption engine(UUID id, String engineVersion, String assetFormat) throws Exception {
        return engine(id, manifest(engineVersion, assetFormat, "1.0"));
    }

    private static EngineOption engine(UUID id, EngineManifest manifest) {
        Path exe = ROOT.resolve("engines").resolve(id.toString()).resolve("Editor.exe");
        return new EngineOption(Optional.of(id), exe, exe.toString(), EngineHealth.ok(manifest, exe.getParent()),
                false, false, false);
    }

    private static EngineOptions options(UUID defaultId, EngineOption... engines) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.ofNullable(defaultId),
                StoreStatus.OK, Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.empty(), List.of(engines));
    }

    private static ProjectFile project(String engineVersion, String assetFormat) {
        return project(engineVersion, assetFormat, "assets", null);
    }

    private static ProjectFile project(String engineVersion, String assetFormat, String workingDirectory,
                                       String structureProblem) {
        return new ProjectFile("Game", "1.0", engineVersion, structureProblem, workingDirectory, assetFormat);
    }

    private static RecentEntry entry(UUID engineId) {
        RecentEntry entry = RecentEntry.create(UUID.fromString("00000000-0000-0000-0000-000000000001"), FILE, "Game",
                TestSupport.T0);
        return engineId == null ? entry : entry.withEngineId(engineId.toString());
    }

    private void open(RecentEntry entry, EngineOptions options, ProjectFile project) {
        services.engines.complete(options);
        services.check = new ProjectFileCheck.Readable(project, EditorLockState.FREE);
        opens++;
        flow.open(new OpenProjectFlow.Request(FILE, entry, "Game", McpSetting.DEFAULT));
        fx.runAll();
    }

    /** Runs the job thread, then the FX continuations. */
    private void finishJob() {
        jobThread.runAll();
        fx.runAll();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Compatible
    // ---------------------------------------------------------------------------------------------------------------

    /** AC4: a patch-only difference opens without a dialog. */
    @Test
    void aPatchOnlyDifferenceOpensWithoutAsking() throws Exception {
        EngineOption engine = engine(NEW_ID, "1.1.3", "1.1.0");

        open(entry(NEW_ID), options(null, engine), project("VertexForge 1.1.0", "1.1.0"));

        assertTrue(services.prompts.isEmpty());
        assertTrue(services.enginePrompts.isEmpty());
        assertEquals(1, services.starts.size());
        Started started = services.starts.getFirst();
        assertEquals(engine, started.engine());
        assertEquals(FILE, started.projectFile());
        assertEquals(Optional.empty(), started.bindTo());
        assertEquals(List.of(true), services.reads, "the lock is probed when no editor of this window has the folder");
    }

    @Test
    void anUnboundProjectIsBoundToTheEngineItFirstOpensWith() throws Exception {
        EngineOption engine = engine(NEW_ID, "1.1.0", "1.1.0");

        open(entry(null), options(NEW_ID, engine), project("VertexForge 1.1.0", "1.1.0"));

        assertEquals(Optional.of(NEW_ID), services.starts.getFirst().bindTo());
    }

    @Test
    void theEnginesAreCheckedAndTheFileReadBeforeAnythingElse() throws Exception {
        services.check = new ProjectFileCheck.Readable(project("VertexForge 1.1.0", "1.1.0"), EditorLockState.FREE);
        opens++;
        flow.open(new OpenProjectFlow.Request(FILE, entry(NEW_ID), "Game", McpSetting.DEFAULT));
        fx.runAll();
        assertTrue(services.starts.isEmpty(), "nothing starts while the engines are being checked");

        services.engines.complete(options(null, engine(NEW_ID, "1.1.0", "1.1.0")));
        fx.runAll();

        assertEquals(1, services.starts.size());
    }

    @Test
    void anEngineCheckThatFailsSaysSoAndReleases() {
        services.engines.completeExceptionally(new IllegalStateException("registry broken"));
        services.check = new ProjectFileCheck.Readable(project("VertexForge 1.1.0", "1.1.0"), EditorLockState.FREE);
        opens++;
        flow.open(new OpenProjectFlow.Request(FILE, entry(NEW_ID), "Game", McpSetting.DEFAULT));
        fx.runAll();

        assertEquals(1, services.releases);
        assertTrue(services.errors.getFirst().contains("Could not check the engines: registry broken"),
                services.errors.toString());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Older project
    // ---------------------------------------------------------------------------------------------------------------

    /** AC1: a 1.0.0 project opened with a 1.1.0 engine whose asset format changed asks, backing up by default. */
    @Test
    void anOlderProjectAsksWithBackUpAsTheDefault() throws Exception {
        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));

        VersionPrompt prompt = services.prompts.getFirst();
        assertEquals(List.of(Choice.BACK_UP_AND_OPEN, Choice.OPEN_COPY, Choice.OPEN_ANYWAY, Choice.CANCEL),
                prompt.choices());
        assertEquals(Choice.BACK_UP_AND_OPEN, prompt.defaultChoice());
        assertNull(prompt.unavailable());
        assertTrue(prompt.text().contains("re-import"), prompt.text());
        assertEquals(1, services.releases, "Cancel (the fake's default answer) starts nothing");
    }

    @Test
    void backUpAndOpenStartsTheEditorOnlyOnceTheBackupIsDone() throws Exception {
        services.answers.add(Choice.BACK_UP_AND_OPEN);
        Path zip = ROOT.resolve("Game-backup-1.0.0-20260102-030405Z.zip");
        services.backupOutcome = new BackupOutcome.Done(zip, 10, 20, 2, List.of());

        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));

        assertEquals(List.of("Back Up Project"), services.jobs);
        BackupRequest request = services.backups.getFirst();
        assertEquals(FILE, request.projectFile());
        assertEquals("assets", request.workingDirectory());
        assertEquals("1.0.0", request.savedVersion());
        assertTrue(services.starts.isEmpty(), "the editor must not start before the backup exists");
        assertTrue(flow.jobRunning());

        finishJob();

        assertFalse(flow.jobRunning());
        assertEquals(List.of(true), services.jobClosed);
        assertEquals(1, services.starts.size());
        assertTrue(services.notices.getFirst().contains(zip.toString()), services.notices.toString());
    }

    @Test
    void aCancelledBackupOpensNothing() throws Exception {
        services.answers.add(Choice.BACK_UP_AND_OPEN);
        services.backupOutcome = new BackupOutcome.Done(ROOT.resolve("x.zip"), 1, 1, 1, List.of());
        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));

        flow.cancelJob();
        finishJob();

        assertTrue(services.starts.isEmpty());
        assertEquals(1, services.releases);
        assertTrue(services.notices.getFirst().contains("cancelled"), services.notices.toString());
    }

    @Test
    void aRefusedOrFailedBackupOpensNothingAndSaysWhy() throws Exception {
        services.answers.add(Choice.BACK_UP_AND_OPEN);
        services.backupOutcome = new BackupOutcome.Refused("Not enough free space on D:.");
        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));
        finishJob();

        assertTrue(services.starts.isEmpty());
        assertTrue(services.errors.getFirst().contains("Not enough free space on D:."), services.errors.toString());
        assertTrue(services.errors.getFirst().contains("was not opened"), services.errors.toString());
    }

    @Test
    void openAnywayOpensAtOnce() throws Exception {
        services.answers.add(Choice.OPEN_ANYWAY);

        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));

        assertEquals(1, services.starts.size());
        assertTrue(services.backups.isEmpty());
    }

    @Test
    void aWorkingDirectoryOutsideTheFolderDisablesBackUpAndCopyAndCancelsByDefault() throws Exception {
        String outside = TestSupport.isWindows() ? "D:\\Elsewhere\\Assets" : "/elsewhere/assets";
        services.answers.add(Choice.BACK_UP_AND_OPEN);

        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")),
                project("VertexForge 1.0.0", null, outside, null));

        VersionPrompt prompt = services.prompts.getFirst();
        assertNotNull(prompt.unavailable());
        assertFalse(prompt.enabled(Choice.BACK_UP_AND_OPEN));
        assertFalse(prompt.enabled(Choice.OPEN_COPY));
        assertTrue(prompt.enabled(Choice.OPEN_ANYWAY));
        assertEquals(Choice.CANCEL, prompt.defaultChoice());
        // A disabled answer counts as Cancel.
        assertTrue(services.backups.isEmpty());
        assertEquals(1, services.releases);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Open a copy
    // ---------------------------------------------------------------------------------------------------------------

    @Test
    void openACopyListsTheCopyBoundToTheEngineAndOpensIt() throws Exception {
        services.answers.add(Choice.OPEN_COPY);
        services.copyNames.add(Optional.of("Game-1.1.0"));
        Path copyFolder = ROOT.resolve("Game-1.1.0");
        Path copyFile = copyFolder.resolve("Game.vfproj");
        services.copyOutcome = new CopyOutcome.Done(copyFile, copyFolder, List.of());
        RecentEntry original = entry(NEW_ID);

        open(original, options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));

        CopyNamePrompt namePrompt = services.copyPrompts.getFirst();
        assertEquals("Game-1.1.0", namePrompt.suggestedName());
        assertEquals(ROOT, namePrompt.location());
        assertNull(namePrompt.problem());
        assertEquals(new CopyRequest(FILE, "assets", "Game-1.1.0"), services.copies.getFirst());

        finishJob();
        assertEquals(List.of(copyFile + "|Game-1.1.0|" + NEW_ID), services.listedCopies);
        assertTrue(services.starts.isEmpty(), "the copy opens once it is listed");

        RecentEntry copyEntry = RecentEntry.create(UUID.randomUUID(), copyFile, "Game-1.1.0", TestSupport.T0)
                .withEngineId(NEW_ID.toString());
        services.listing.complete(new AddedProject(view(copyEntry), copyEntry, false));
        fx.runAll();

        Started started = services.starts.getFirst();
        assertEquals(copyFile, started.projectFile());
        assertEquals("Game-1.1.0", started.name());
        assertEquals(copyEntry.id(), started.id());
        assertEquals(McpSetting.DEFAULT, started.mcp());
        assertEquals(Optional.empty(), started.bindTo());
        assertEquals(1, services.listed.size());
    }

    @Test
    void aRefusedCopyNameIsAskedForAgainWithTheReason() throws Exception {
        services.answers.add(Choice.OPEN_COPY);
        services.copyNames.add(Optional.of("Game-1.1.0"));
        services.copyNames.add(Optional.empty());
        services.copyOutcome = new CopyOutcome.Refused("A folder called \"Game-1.1.0\" exists already.");

        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));
        finishJob();

        assertEquals(2, services.copyPrompts.size());
        CopyNamePrompt again = services.copyPrompts.get(1);
        assertEquals("Game-1.1.0", again.suggestedName());
        assertEquals("A folder called \"Game-1.1.0\" exists already.", again.problem());
        assertEquals(1, services.releases, "cancelling the second question starts nothing");
    }

    @Test
    void aCopyWithAReadOnlyListOpensUnlisted() throws Exception {
        services.readOnly = true;
        services.answers.add(Choice.OPEN_COPY);
        services.copyNames.add(Optional.of("Copy"));
        Path copyFile = ROOT.resolve("Copy").resolve("Game.vfproj");
        services.copyOutcome = new CopyOutcome.Done(copyFile, copyFile.getParent(), List.of());

        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));
        finishJob();

        assertTrue(services.listedCopies.isEmpty());
        assertEquals(copyFile, services.starts.getFirst().projectFile());
        assertNull(services.starts.getFirst().id());
        assertTrue(services.notices.getFirst().contains("read-only"), services.notices.toString());
    }

    @Test
    void aCopyThatCannotBeListedStillOpens() throws Exception {
        services.answers.add(Choice.OPEN_COPY);
        services.copyNames.add(Optional.of("Copy"));
        Path copyFile = ROOT.resolve("Copy").resolve("Game.vfproj");
        services.copyOutcome = new CopyOutcome.Done(copyFile, copyFile.getParent(), List.of());
        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.0.0", null));
        finishJob();

        services.listing.completeExceptionally(new StoreWriteException(StoreWriteException.Reason.IO, "locked",
                null));
        fx.runAll();

        assertEquals(1, services.saveFailures.size());
        assertEquals(copyFile, services.starts.getFirst().projectFile());
        assertNull(services.starts.getFirst().id());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Newer project
    // ---------------------------------------------------------------------------------------------------------------

    /** AC2: the engine it would open with cannot; the dialog names the version and offers only engines that can. */
    @Test
    void aNewerProjectOffersOnlyTheEnginesThatCanOpenIt() throws Exception {
        EngineOption old = engine(OLD_ID, "1.0.0", "1.0.0");
        EngineOption newer = engine(NEW_ID, "1.1.0", "1.1.0");
        EngineOption newest = engine(NEWEST_ID, "1.2.0", "1.1.0");
        services.chooser = prompt -> Optional.of(new OpenProjectFlow.Picked(newer, true));

        open(entry(OLD_ID), options(null, old, newer, newest), project("VertexForge 1.1.0", "1.1.0"));

        OpenProjectFlow.EnginePrompt prompt = services.enginePrompts.getFirst();
        assertEquals("\"Game\" needs VertexForge 1.1.0", prompt.header());
        assertEquals(List.of(newer, newest), prompt.candidates(), "the engine that cannot open it is not offered");
        assertTrue(prompt.rememberByDefault());
        assertFalse(prompt.offered().test(old));
        // 1.2.0 would upgrade it, so the user is asked first; 1.1.0 opens it as it is.
        Started started = services.starts.getFirst();
        assertEquals(newer, started.engine());
        assertEquals(Optional.of(NEW_ID), started.bindTo(), "remember binds the project to the picked engine");
    }

    @Test
    void aNewerProjectWithNoEngineThatCanOpenItOffersNone() throws Exception {
        EngineOption old = engine(OLD_ID, "1.0.0", "1.0.0");

        open(entry(OLD_ID), options(null, old), project("VertexForge 1.1.0", "1.1.0"));

        OpenProjectFlow.EnginePrompt prompt = services.enginePrompts.getFirst();
        assertTrue(prompt.candidates().isEmpty());
        assertTrue(prompt.problem().contains("Add Engine"), prompt.problem());
        assertEquals(1, services.releases);
    }

    @Test
    void theResolversChoiceOnlyListsEnginesThatCanOpenTheProject() throws Exception {
        EngineOption old = engine(OLD_ID, "1.0.0", "1.0.0");
        EngineOption newer = engine(NEW_ID, "1.1.0", "1.1.0");

        // Unbound, two engines and no default: the resolver asks.
        open(entry(null), options(null, old, newer), project("VertexForge 1.1.0", "1.1.0"));

        OpenProjectFlow.EnginePrompt prompt = services.enginePrompts.getFirst();
        assertEquals(List.of(newer), prompt.candidates());
        assertTrue(prompt.problem().contains("not listed"), prompt.problem());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Unknown version
    // ---------------------------------------------------------------------------------------------------------------

    @Test
    void anEngineWithoutAManifestWarnsOnceAndRecordsIt() throws Exception {
        services.answers.add(Choice.OPEN);
        EngineOption noManifest = engine(NEW_ID, (EngineManifest) null);
        RecentEntry entry = entry(NEW_ID);

        open(entry, options(null, noManifest), project("VertexForge 1.0.0", "1.0.0"));

        VersionPrompt prompt = services.prompts.getFirst();
        assertEquals(List.of(Choice.OPEN, Choice.BACK_UP_AND_OPEN, Choice.CANCEL), prompt.choices());
        assertEquals(Choice.OPEN, prompt.defaultChoice());
        assertEquals(List.of(entry.id()), services.warned);
        assertEquals(1, services.starts.size());

        // The row still holds the old entry: the session's own record keeps the warning from coming back.
        services.engines = new CompletableFuture<>();
        open(entry, options(null, noManifest), project("VertexForge 1.0.0", "1.0.0"));
        assertEquals(1, services.prompts.size());
        assertEquals(2, services.starts.size());
    }

    @Test
    void aProjectWhoseWarningWasRecordedIsNotWarnedAgain() throws Exception {
        open(entry(NEW_ID).withUnknownVersionWarned(), options(null, engine(NEW_ID, (EngineManifest) null)),
                project("VertexForge 1.0.0", "1.0.0"));

        assertTrue(services.prompts.isEmpty());
        assertEquals(1, services.starts.size());
    }

    @Test
    void anUnlistedProjectIsWarnedOncePerSessionWithoutWritingTheList() throws Exception {
        services.answers.add(Choice.OPEN);
        EngineOption noManifest = engine(NEW_ID, (EngineManifest) null);

        open(null, options(NEW_ID, noManifest), project("VertexForge 1.0.0", "1.0.0"));
        services.engines = new CompletableFuture<>();
        open(null, options(NEW_ID, noManifest), project("VertexForge 1.0.0", "1.0.0"));

        assertEquals(1, services.prompts.size());
        assertTrue(services.warned.isEmpty());
        assertEquals(2, services.starts.size());
        assertNull(services.starts.getFirst().id());
    }

    @Test
    void cancellingTheWarningRecordsNothing() throws Exception {
        services.answers.add(Choice.CANCEL);

        open(entry(NEW_ID), options(null, engine(NEW_ID, (EngineManifest) null)),
                project("VertexForge 1.0.0", "1.0.0"));

        assertTrue(services.warned.isEmpty());
        assertEquals(1, services.releases);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Refusals
    // ---------------------------------------------------------------------------------------------------------------

    @Test
    void aMissingProjectFileIsRefused() throws Exception {
        services.engines.complete(options(null, engine(NEW_ID, "1.1.0", "1.1.0")));
        services.check = new ProjectFileCheck.Missing("The project file does not exist");
        opens++;
        flow.open(new OpenProjectFlow.Request(FILE, entry(NEW_ID), "Game", McpSetting.DEFAULT));
        fx.runAll();

        assertEquals(List.of("Open Project: The project file does not exist."), services.errors);
        assertEquals(1, services.releases);
    }

    @Test
    void aProjectAnotherEditorHasOpenIsRefused() throws Exception {
        services.engines.complete(options(null, engine(NEW_ID, "1.1.0", "1.1.0")));
        services.check = new ProjectFileCheck.Readable(project("VertexForge 1.1.0", "1.1.0"),
                new EditorLockState.Held(OptionalLong.of(42)));
        opens++;
        flow.open(new OpenProjectFlow.Request(FILE, entry(NEW_ID), "Game", McpSetting.DEFAULT));
        fx.runAll();

        assertTrue(services.errors.getFirst().contains("already open in another editor (pid 42)"),
                services.errors.toString());
        assertTrue(services.starts.isEmpty());
    }

    @Test
    void aFolderAnEditorOfThisWindowHasOpenIsRefusedWithoutProbingItsLock() throws Exception {
        services.folderEditor = FolderEditor.OTHER_PROJECT;

        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")), project("VertexForge 1.1.0", "1.1.0"));

        assertEquals(List.of(false), services.reads);
        assertTrue(services.errors.getFirst().contains("an editor of this launcher has open"),
                services.errors.toString());
    }

    @Test
    void aMemberProblemIsReportedOnceTheEngineCanOpenTheProject() throws Exception {
        open(entry(NEW_ID), options(null, engine(NEW_ID, "1.1.0", "1.1.0")),
                project("VertexForge 1.1.0", "1.1.0", "assets", "Missing or invalid field: startupScene"));

        assertTrue(services.errors.getFirst().contains("Missing or invalid field: startupScene"),
                services.errors.toString());
        assertTrue(services.starts.isEmpty());
    }

    @Test
    void theSuggestedCopyNameFollowsTheEngineVersion() throws Exception {
        assertEquals("Game-1.1.0", OpenProjectFlow.suggestedCopyName("Game", manifest("1.1.0", "1.1.0", "1.0")));
        assertEquals("Game-copy", OpenProjectFlow.suggestedCopyName("Game", null));
    }

    private static RecentProjectsView view(RecentEntry... entries) {
        return new RecentProjectsView(List.of(entries), StoreStatus.OK, Optional.empty(), "", 0);
    }
}
