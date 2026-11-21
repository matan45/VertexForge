package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.dialog.CleanCachesPrompt;
import com.vertexforge.launcher.dialog.CopyNamePrompt;
import com.vertexforge.launcher.dialog.DeletePrompt;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.repository.RemovedProject;
import com.vertexforge.launcher.service.DeleteGuards;
import com.vertexforge.launcher.service.FileJob;
import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.ProjectBackupService.BackupOutcome;
import com.vertexforge.launcher.service.ProjectBackupService.BackupRequest;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CacheFolder;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CleanOutcome;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CleanRequest;
import com.vertexforge.launcher.service.ProjectCacheCleaner.PreviewOutcome;
import com.vertexforge.launcher.service.ProjectCopier.CopyOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.CheckOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.DeleteOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.DeletePreflight;
import com.vertexforge.launcher.service.ProjectDeleter.DeleteRequest;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Nested;
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
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Duplicate, Back Up, Clean Caches and Move to Recycle Bin without the JavaFX toolkit (VK-1695). */
class ProjectActionsFlowTest {

    private static final Path ROOT = Path.of(TestSupport.isWindows() ? "C:\\projects" : "/projects");
    private static final Path FOLDER = ROOT.resolve("Game");
    private static final Path FILE = FOLDER.resolve("Game.vfproj");
    private static final UUID ENTRY_ID = UUID.fromString("00000000-0000-0000-0000-000000000001");
    private static final UUID WITH_CACHES = UUID.fromString("00000000-0000-0000-0000-000000000301");
    private static final UUID WITHOUT_CACHES = UUID.fromString("00000000-0000-0000-0000-000000000302");
    private static final UUID NO_MANIFEST = UUID.fromString("00000000-0000-0000-0000-000000000303");

    private final QueueExecutor fx = new QueueExecutor();
    private final QueueExecutor jobThread = new QueueExecutor();
    private final FakeServices services = new FakeServices();
    private final ProjectActionsFlow flow = new ProjectActionsFlow(services, fx);
    private int runs;

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

        boolean idle() {
            return queued.isEmpty();
        }
    }

    private final class FakeServices implements ProjectActionsFlow.Services {
        CompletableFuture<EngineOptions> engines = new CompletableFuture<>();
        ProjectFileCheck check = readable(EditorLockState.FREE);
        FolderEditor folderEditor = FolderEditor.NONE;
        /** What folderEditor answers once the delete dialog has been answered. */
        FolderEditor folderEditorAfterDeletePrompt;
        boolean readOnly;
        String suggestedName = "Game-copy";
        Function<OpenProjectFlow.EnginePrompt, Optional<EngineOption>> chooser = prompt -> Optional.empty();
        final Deque<Optional<String>> copyNames = new ArrayDeque<>();
        boolean confirmClean = true;
        Optional<String> typedName = Optional.of("Game");
        final Deque<CopyOutcome> duplicates = new ArrayDeque<>();
        BackupOutcome backup;
        PreviewOutcome cleanPreview;
        CleanOutcome clean;
        CheckOutcome deleteCheck;
        DeleteOutcome delete;
        CompletableFuture<AddedProject> listing = new CompletableFuture<>();
        CompletableFuture<RemovedProject> unlisting = new CompletableFuture<>();

        int reads;
        int engineChecks;
        final List<OpenProjectFlow.EnginePrompt> enginePrompts = new ArrayList<>();
        final List<CopyNamePrompt> copyPrompts = new ArrayList<>();
        final List<CleanCachesPrompt> cleanPrompts = new ArrayList<>();
        final List<DeletePrompt> deletePrompts = new ArrayList<>();
        final List<String> jobs = new ArrayList<>();
        final List<Boolean> jobClosed = new ArrayList<>();
        final List<String> duplicateRequests = new ArrayList<>();
        final List<BackupRequest> backupRequests = new ArrayList<>();
        final List<CleanRequest> previewRequests = new ArrayList<>();
        final List<CleanRequest> cleanRequests = new ArrayList<>();
        final List<DeleteRequest> checkRequests = new ArrayList<>();
        final List<String> deleteRequests = new ArrayList<>();
        final List<String> listings = new ArrayList<>();
        final List<UUID> unlistings = new ArrayList<>();
        final List<AddedProject> listed = new ArrayList<>();
        final List<RemovedProject> unlisted = new ArrayList<>();
        final List<Path> changed = new ArrayList<>();
        int releases;
        final List<String> errors = new ArrayList<>();
        final List<String> notices = new ArrayList<>();
        final List<Path> reveals = new ArrayList<>();

        @Override
        public CompletableFuture<EngineOptions> engines() {
            engineChecks++;
            return engines;
        }

        @Override
        public CompletableFuture<ProjectFileCheck> read(Path projectFile) {
            reads++;
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
        public DeleteGuards.DeleteContext deleteContext(Path projectFile, EngineOptions options) {
            return DeleteGuards.DeleteContext.none();
        }

        @Override
        public Optional<EngineOption> chooseEngine(OpenProjectFlow.EnginePrompt prompt) {
            enginePrompts.add(prompt);
            return chooser.apply(prompt);
        }

        @Override
        public CompletableFuture<String> suggestDuplicateName(Path projectFile, String projectName) {
            return CompletableFuture.completedFuture(suggestedName);
        }

        @Override
        public Optional<String> askCopyName(CopyNamePrompt prompt) {
            copyPrompts.add(prompt);
            return copyNames.isEmpty() ? Optional.empty() : copyNames.removeFirst();
        }

        @Override
        public boolean confirmClean(CleanCachesPrompt prompt) {
            cleanPrompts.add(prompt);
            return confirmClean;
        }

        @Override
        public Optional<String> askDeleteName(DeletePrompt prompt) {
            deletePrompts.add(prompt);
            if (folderEditorAfterDeletePrompt != null) {
                folderEditor = folderEditorAfterDeletePrompt;
            }
            return typedName;
        }

        @Override
        public OpenProjectFlow.JobView showJob(String title, String header, Function<JobMonitor.Phase, String> texts,
                                               Runnable cancel) {
            jobs.add(title + " | " + header);
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
        public FileJob<CopyOutcome> startDuplicate(Path projectFile, String workingDirectory, String newName,
                                                   FileJob.ProgressListener listener) {
            duplicateRequests.add(projectFile + "|" + workingDirectory + "|" + newName);
            CopyOutcome outcome = duplicates.removeFirst();
            return FileJob.start(jobThread, monitor -> outcome, listener, CopyOutcome.Failed::new);
        }

        @Override
        public FileJob<BackupOutcome> startBackup(BackupRequest request, FileJob.ProgressListener listener) {
            backupRequests.add(request);
            return FileJob.start(jobThread, monitor -> backup, listener, BackupOutcome.Failed::new);
        }

        @Override
        public FileJob<PreviewOutcome> startCleanPreview(CleanRequest request, FileJob.ProgressListener listener) {
            previewRequests.add(request);
            return FileJob.start(jobThread, monitor -> cleanPreview, listener, PreviewOutcome.Failed::new);
        }

        @Override
        public FileJob<CleanOutcome> startClean(CleanRequest request, FileJob.ProgressListener listener) {
            cleanRequests.add(request);
            return FileJob.start(jobThread, monitor -> clean, listener, CleanOutcome.Failed::new);
        }

        @Override
        public FileJob<CheckOutcome> startDeleteCheck(DeleteRequest request, FileJob.ProgressListener listener) {
            checkRequests.add(request);
            return FileJob.start(jobThread, monitor -> deleteCheck, listener, CheckOutcome.Failed::new);
        }

        @Override
        public FileJob<DeleteOutcome> startDelete(DeleteRequest request, String typed,
                                                  FileJob.ProgressListener listener) {
            deleteRequests.add(request.projectName() + "|" + typed);
            return FileJob.start(jobThread, monitor -> delete, listener, DeleteOutcome.Failed::new);
        }

        @Override
        public CompletableFuture<AddedProject> listDuplicate(Path projectFile, String name, String rawEngineId) {
            listings.add(projectFile + "|" + name + "|" + rawEngineId);
            return listing;
        }

        @Override
        public CompletableFuture<RemovedProject> unlist(UUID id) {
            unlistings.add(id);
            return unlisting;
        }

        @Override
        public void listed(AddedProject added) {
            listed.add(added);
        }

        @Override
        public void unlisted(RemovedProject removed) {
            unlisted.add(removed);
        }

        @Override
        public void changed(Path projectFile) {
            changed.add(projectFile);
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
            reveals.add(reveal);
        }
    }

    /** Every run ends in exactly one release, or the window keeps its buttons disabled (or re-enables them early). */
    @AfterEach
    void everyRunEndedInExactlyOneRelease() {
        finish();
        assertEquals(runs, services.releases, "releases " + services.releases + " for " + runs + " runs; errors "
                + services.errors + "; notices " + services.notices);
        assertFalse(flow.jobRunning());
        assertTrue(services.jobClosed.stream().allMatch(closed -> closed), "every job window closed");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Fixtures
    // -----------------------------------------------------------------------------------------------------------------

    private static ProjectFile project() {
        return new ProjectFile("Game", "1.0", "VertexForge 1.0.0", null, "assets", "1.0.0");
    }

    private static ProjectFileCheck readable(EditorLockState lock) {
        return new ProjectFileCheck.Readable(project(), lock);
    }

    private static RecentEntry entry(String engineId) {
        RecentEntry entry = RecentEntry.create(ENTRY_ID, FILE, "Game", TestSupport.T0);
        return engineId == null ? entry : entry.withEngineId(engineId);
    }

    private static ProjectActionsFlow.Request request(RecentEntry entry) {
        return new ProjectActionsFlow.Request(FILE, entry, "Game");
    }

    private static EngineOption engine(UUID id, String cacheDirs) throws Exception {
        Path exe = ROOT.resolve("engines").resolve(id.toString()).resolve("Editor.exe");
        EngineManifest manifest = id.equals(NO_MANIFEST) ? null : EngineManifest.parse("{\"manifestVersion\": 1,"
                + " \"engineVersion\": \"1.0.0\", \"assetFormatVersion\": \"1.0.0\", \"projectSchemaVersion\": \"1.0\","
                + " \"configuration\": \"Development\", \"editorExe\": \"Editor.exe\", \"workingDirectory\": \".\""
                + (cacheDirs == null ? "" : ", \"projectCacheDirs\": " + cacheDirs) + "}");
        return new EngineOption(Optional.of(id), exe, exe.toString(), EngineHealth.ok(manifest, exe.getParent()),
                false, false, false);
    }

    private static EngineOptions options(EngineOption... engines) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.empty(), StoreStatus.OK,
                Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.empty(), List.of(engines));
    }

    private static RecentProjectsView view(RecentEntry... entries) {
        return new RecentProjectsView(List.of(entries), StoreStatus.OK, Optional.empty(), "", 0);
    }

    private static CacheFolder folder(String relative, CacheFolder.State state, long bytes) {
        return new CacheFolder("scripts/compiled", relative, FOLDER.resolve(relative), state, bytes, 3,
                state == CacheFolder.State.SKIPPED ? "it is a link; what it points to is left alone" : "");
    }

    /** Runs every job and every FX step they lead to, until nothing is left. */
    private void finish() {
        while (!jobThread.idle() || !fx.idle()) {
            jobThread.runAll();
            fx.runAll();
        }
    }

    private void start(Runnable action) {
        runs++;
        action.run();
        finish();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Every action: an open editor stops it (D3)
    // -----------------------------------------------------------------------------------------------------------------

    @Nested
    class EveryAction {

        @Test
        void anEditorOfThisLauncherStopsItBeforeAnythingIsRead() {
            services.folderEditor = FolderEditor.THIS_PROJECT;
            start(() -> flow.duplicate(request(entry(null))));
            start(() -> flow.backUp(request(entry(null))));
            start(() -> flow.cleanCaches(request(entry(null))));
            start(() -> flow.delete(request(entry(null))));

            assertEquals(0, services.reads);
            assertEquals(0, services.engineChecks);
            assertEquals(4, services.errors.size());
            assertTrue(services.errors.stream().allMatch(error -> error.contains("open in an editor this launcher "
                    + "started")), services.errors::toString);
        }

        @Test
        void anEditorOfThisLauncherOnAnotherProjectOfTheFolderStopsItToo() {
            services.folderEditor = FolderEditor.OTHER_PROJECT;
            start(() -> flow.backUp(request(entry(null))));

            assertEquals(0, services.reads);
            assertTrue(services.errors.getFirst().contains("on another project file"), services.errors::toString);
        }

        @Test
        void anotherEditorsLockStopsItWithItsProcess() {
            services.check = readable(new EditorLockState.Held(OptionalLong.of(4242)));
            start(() -> flow.duplicate(request(entry(null))));

            assertEquals(List.of(), services.copyPrompts);
            assertEquals(List.of(ProjectActionsFlow.DUPLICATE_TITLE + ": \"Game\" is open in an editor (process 4242). "
                    + "Close the editor, then duplicate it."), services.errors);
        }

        @Test
        void aMissingOrInvalidProjectStopsIt() {
            services.check = new ProjectFileCheck.Missing("The project file is gone");
            start(() -> flow.backUp(request(entry(null))));
            services.check = new ProjectFileCheck.Invalid("Its schemaVersion is missing");
            start(() -> flow.backUp(request(entry(null))));

            assertEquals(List.of(), services.backupRequests);
            assertEquals(2, services.errors.size());
            assertTrue(services.errors.get(1).contains("Its schemaVersion is missing"), services.errors::toString);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Duplicate…
    // -----------------------------------------------------------------------------------------------------------------

    @Nested
    class Duplicate {

        private final CopyOutcome.Done done = new CopyOutcome.Done(ROOT.resolve("Game-copy/Game-copy.vfproj"),
                ROOT.resolve("Game-copy"), List.of());

        @Test
        void theCopyIsListedBoundAsTheOriginalAndNothingOpens() {
            services.copyNames.add(Optional.of("Game-copy"));
            services.duplicates.add(done);
            RecentEntry copy = RecentEntry.create(UUID.randomUUID(), done.projectFile(), "Game-copy", TestSupport.T0);
            services.listing.complete(new AddedProject(view(copy), copy, false));

            start(() -> flow.duplicate(request(entry("not-a-uuid-but-kept"))));

            CopyNamePrompt prompt = services.copyPrompts.getFirst();
            assertEquals(CopyNamePrompt.Purpose.DUPLICATE, prompt.purpose());
            assertEquals("Game-copy", prompt.suggestedName());
            assertEquals(ROOT, prompt.location());
            assertEquals(List.of(FILE + "|assets|Game-copy"), services.duplicateRequests);
            assertEquals(List.of(done.projectFile() + "|Game-copy|not-a-uuid-but-kept"), services.listings);
            assertEquals(1, services.listed.size());
            assertTrue(services.notices.getFirst().contains("The original is unchanged."), services.notices::toString);
            assertEquals(done.projectFile(), services.reveals.getFirst());
            assertEquals(List.of(), services.errors);
        }

        @Test
        void anUnboundOriginalGivesAnUnboundCopy() {
            services.copyNames.add(Optional.of("Game-copy"));
            services.duplicates.add(done);
            services.listing.complete(new AddedProject(view(), entry(null), false));

            start(() -> flow.duplicate(request(entry(null))));

            assertEquals(List.of(done.projectFile() + "|Game-copy|null"), services.listings);
        }

        @Test
        void aRefusedNameIsAskedAgainWithTheReason() {
            services.copyNames.add(Optional.of("Taken"));
            services.copyNames.add(Optional.empty());
            services.duplicates.add(new CopyOutcome.Refused("A folder named \"Taken\" already exists."));

            start(() -> flow.duplicate(request(entry(null))));

            assertEquals(2, services.copyPrompts.size());
            assertEquals("Taken", services.copyPrompts.get(1).suggestedName());
            assertEquals("A folder named \"Taken\" already exists.", services.copyPrompts.get(1).problem());
            assertEquals(List.of(), services.listings);
        }

        @Test
        void cancellingTheNameCopiesNothing() {
            start(() -> flow.duplicate(request(entry(null))));

            assertEquals(1, services.copyPrompts.size());
            assertEquals(List.of(), services.duplicateRequests);
            assertEquals(List.of(), services.errors);
        }

        @Test
        void aFailedCopySaysTheOriginalIsUnchanged() {
            services.copyNames.add(Optional.of("Game-copy"));
            services.duplicates.add(new CopyOutcome.Failed("The project was opened in the editor meanwhile."));

            start(() -> flow.duplicate(request(entry(null))));

            assertTrue(services.errors.getFirst().contains("opened in the editor meanwhile"), services.errors::toString);
            assertEquals(List.of(), services.listings);
        }

        @Test
        void aReadOnlyListKeepsTheCopyButDoesNotListIt() {
            services.readOnly = true;
            services.copyNames.add(Optional.of("Game-copy"));
            services.duplicates.add(done);

            start(() -> flow.duplicate(request(entry(null))));

            assertEquals(List.of(), services.listings);
            assertTrue(services.notices.getFirst().contains("read-only"), services.notices::toString);
            assertEquals(done.projectFile(), services.reveals.getFirst());
        }

        @Test
        void aCopyThatCannotBeListedIsReported() {
            services.copyNames.add(Optional.of("Game-copy"));
            services.duplicates.add(done);
            services.listing.completeExceptionally(new IllegalStateException("disk full"));

            start(() -> flow.duplicate(request(entry(null))));

            assertTrue(services.errors.getFirst().contains("disk full"), services.errors::toString);
            assertTrue(services.errors.getFirst().contains("Add Existing"), services.errors::toString);
        }

        @Test
        void aProjectWhoseContentLiesOutsideItsFolderIsRefusedBeforeTheName() {
            services.check = new ProjectFileCheck.Readable(new ProjectFile("Game", "1.0", "VertexForge 1.0.0", null,
                    "../elsewhere", "1.0.0"), EditorLockState.FREE);

            start(() -> flow.duplicate(request(entry(null))));

            assertEquals(List.of(), services.copyPrompts);
            assertTrue(services.errors.getFirst().contains("cannot be duplicated"), services.errors::toString);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Back Up
    // -----------------------------------------------------------------------------------------------------------------

    @Nested
    class BackUp {

        @Test
        void theZipIsReportedWithShowInFolder() {
            Path zip = ROOT.resolve("Game-backup-1.0.0-20260102-030405Z.zip");
            services.backup = new BackupOutcome.Done(zip, 2048, 4096, 3, List.of());

            start(() -> flow.backUp(request(entry(null))));

            assertEquals(List.of(new BackupRequest(FILE, "assets", "1.0.0")), services.backupRequests);
            assertTrue(services.notices.getFirst().startsWith("Backed up \"Game\" to " + zip),
                    services.notices::toString);
            assertEquals(zip, services.reveals.getFirst());
        }

        @Test
        void aRefusedBackupIsAnError() {
            services.backup = new BackupOutcome.Refused("Not enough free space.");

            start(() -> flow.backUp(request(entry(null))));

            assertEquals(List.of(ProjectActionsFlow.BACKUP_TITLE + ": Not enough free space."), services.errors);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Clean Caches…
    // -----------------------------------------------------------------------------------------------------------------

    @Nested
    class CleanCaches {

        private void ready(CacheFolder... folders) {
            services.cleanPreview = new PreviewOutcome.Ready(List.of(folders));
        }

        @Test
        void theEnginesFoldersArePreviewedConfirmedAndCleaned() throws Exception {
            EngineOption engine = engine(WITH_CACHES, "[\"scripts/compiled\"]");
            services.engines.complete(options(engine));
            CacheFolder present = folder("assets/scripts/compiled", CacheFolder.State.PRESENT, 1536);
            ready(present);
            services.clean = new CleanOutcome.Done(1536, List.of(present), List.of(), List.of());

            start(() -> flow.cleanCaches(request(entry(WITH_CACHES.toString()))));

            CleanRequest expected = new CleanRequest(FILE, "assets", List.of("scripts/compiled"));
            assertEquals(List.of(expected), services.previewRequests);
            assertEquals(List.of(expected), services.cleanRequests);
            assertEquals(1, services.cleanPrompts.size());
            assertEquals(List.of(present), services.cleanPrompts.getFirst().present());
            assertEquals(List.of(FILE), services.changed);
            assertTrue(services.notices.getFirst().contains("freed 1.5 KB"), services.notices::toString);
        }

        @Test
        void anEngineWithoutTheListLetsTheUserPickOneThatHasIt() throws Exception {
            EngineOption old = engine(WITHOUT_CACHES, null);
            EngineOption current = engine(WITH_CACHES, "[\"scripts/compiled\"]");
            services.engines.complete(options(old, current));
            services.chooser = prompt -> Optional.of(current);
            ready(folder("assets/scripts/compiled", CacheFolder.State.PRESENT, 10));
            services.clean = new CleanOutcome.Done(10, List.of(), List.of(), List.of());

            start(() -> flow.cleanCaches(request(entry(WITHOUT_CACHES.toString()))));

            OpenProjectFlow.EnginePrompt prompt = services.enginePrompts.getFirst();
            assertEquals(List.of(current), prompt.candidates());
            assertFalse(prompt.canRemember(), "the choice is not remembered");
            assertTrue(prompt.problem().contains("names no folders it rebuilds"), prompt.problem());
            assertEquals(1, services.cleanRequests.size());
        }

        @Test
        void noEngineWithTheListIsAnError() throws Exception {
            services.engines.complete(options(engine(WITHOUT_CACHES, null), engine(NO_MANIFEST, null)));

            start(() -> flow.cleanCaches(request(entry(WITHOUT_CACHES.toString()))));

            assertEquals(List.of(), services.enginePrompts);
            assertEquals(List.of(), services.previewRequests);
            assertTrue(services.errors.getFirst().contains("does not know what is safe to delete"),
                    services.errors::toString);
        }

        @Test
        void anEngineThatRebuildsNothingHasNothingToClean() throws Exception {
            services.engines.complete(options(engine(WITH_CACHES, "[]")));

            start(() -> flow.cleanCaches(request(entry(WITH_CACHES.toString()))));

            assertEquals(List.of(), services.previewRequests);
            assertTrue(services.notices.getFirst().contains("nothing to clean"), services.notices::toString);
        }

        @Test
        void nothingPresentIsANoticeWithWhatWasLeftAlone() throws Exception {
            services.engines.complete(options(engine(WITH_CACHES, "[\"scripts/compiled\"]")));
            ready(folder("assets/scripts/compiled", CacheFolder.State.SKIPPED, 0));

            start(() -> flow.cleanCaches(request(entry(WITH_CACHES.toString()))));

            assertEquals(List.of(), services.cleanPrompts);
            assertEquals(List.of(), services.cleanRequests);
            assertTrue(services.notices.getFirst().contains("Left alone: assets/scripts/compiled: it is a link"),
                    services.notices::toString);
        }

        @Test
        void declinedCleansNothing() throws Exception {
            services.engines.complete(options(engine(WITH_CACHES, "[\"scripts/compiled\"]")));
            ready(folder("assets/scripts/compiled", CacheFolder.State.PRESENT, 10));
            services.confirmClean = false;

            start(() -> flow.cleanCaches(request(entry(WITH_CACHES.toString()))));

            assertEquals(1, services.cleanPrompts.size());
            assertEquals(List.of(), services.cleanRequests);
        }

        @Test
        void aFolderThatCouldNotBeDeletedIsAnError() throws Exception {
            services.engines.complete(options(engine(WITH_CACHES, "[\"scripts/compiled\"]")));
            ready(folder("assets/scripts/compiled", CacheFolder.State.PRESENT, 10));
            services.clean = new CleanOutcome.Done(0, List.of(), List.of(),
                    List.of("assets/scripts/compiled could not be deleted completely."));

            start(() -> flow.cleanCaches(request(entry(WITH_CACHES.toString()))));

            assertTrue(services.errors.getFirst().contains("could not be deleted completely"),
                    services.errors::toString);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Move to Recycle Bin…
    // -----------------------------------------------------------------------------------------------------------------

    @Nested
    class MoveToRecycleBin {

        private final DeletePreflight preflight = new DeletePreflight(FOLDER, FOLDER, 4096, 12, 3, List.of());

        @Test
        void theTypedNameMovesTheFolderAndTakesTheEntryOffTheList() throws Exception {
            services.engines.complete(options(engine(WITH_CACHES, null)));
            services.deleteCheck = new CheckOutcome.Ready(preflight);
            services.delete = new DeleteOutcome.Recycled(FOLDER);
            RemovedProject removed = new RemovedProject(view(), Optional.of(new RemovedProject.Removal(entry(null),
                    0)));
            services.unlisting.complete(removed);

            start(() -> flow.delete(request(entry(null))));

            assertEquals("Game", services.checkRequests.getFirst().projectName());
            DeletePrompt prompt = services.deletePrompts.getFirst();
            assertEquals("Game", prompt.projectName());
            assertEquals("12 files in 3 folders, 4.0 KB", prompt.summary());
            assertEquals(List.of("Game|Game"), services.deleteRequests);
            assertEquals(List.of(ENTRY_ID), services.unlistings);
            assertEquals(List.of(removed), services.unlisted);
            assertTrue(services.notices.getFirst().contains("Moved \"Game\""), services.notices::toString);
            assertTrue(services.notices.getFirst().contains("Add Existing"), services.notices::toString);
            assertEquals(List.of(), services.errors);
        }

        @Test
        void aRefusedCheckNeverAsksForTheName() throws Exception {
            services.engines.complete(options());
            services.deleteCheck = new CheckOutcome.Refused("The project is on a network share.");

            start(() -> flow.delete(request(entry(null))));

            assertEquals(List.of(), services.deletePrompts);
            assertEquals(List.of(), services.deleteRequests);
            assertEquals(List.of(ProjectActionsFlow.DELETE_TITLE + ": The project is on a network share."),
                    services.errors);
        }

        @Test
        void cancellingTheNameMovesNothing() throws Exception {
            services.engines.complete(options());
            services.deleteCheck = new CheckOutcome.Ready(preflight);
            services.typedName = Optional.empty();

            start(() -> flow.delete(request(entry(null))));

            assertEquals(1, services.deletePrompts.size());
            assertEquals(List.of(), services.deleteRequests);
        }

        @Test
        void aNameThatDoesNotMatchMovesNothing() throws Exception {
            services.engines.complete(options());
            services.deleteCheck = new CheckOutcome.Ready(preflight);
            services.typedName = Optional.of("game");

            start(() -> flow.delete(request(entry(null))));

            assertEquals(List.of(), services.deleteRequests);
        }

        @Test
        void anEditorStartedWhileTheDialogWasOpenStopsTheMove() throws Exception {
            services.engines.complete(options());
            services.deleteCheck = new CheckOutcome.Ready(preflight);
            services.folderEditorAfterDeletePrompt = FolderEditor.THIS_PROJECT;

            start(() -> flow.delete(request(entry(null))));

            assertEquals(List.of(), services.deleteRequests);
            assertTrue(services.errors.getFirst().contains("was opened in an editor"), services.errors::toString);
        }

        @Test
        void aRefusedMoveKeepsTheEntryAndRechecksTheRow() throws Exception {
            services.engines.complete(options());
            services.deleteCheck = new CheckOutcome.Ready(preflight);
            services.delete = new DeleteOutcome.Refused("The project is open in the editor.");

            start(() -> flow.delete(request(entry(null))));

            assertEquals(List.of(), services.unlistings);
            assertEquals(List.of(FILE), services.changed);
            assertEquals(List.of(ProjectActionsFlow.DELETE_TITLE + ": The project is open in the editor."),
                    services.errors);
        }

        @Test
        void aReadOnlyListKeepsTheEntryAsMissing() throws Exception {
            services.readOnly = true;
            services.engines.complete(options());
            services.deleteCheck = new CheckOutcome.Ready(preflight);
            services.delete = new DeleteOutcome.Recycled(FOLDER);

            start(() -> flow.delete(request(entry(null))));

            assertEquals(List.of(), services.unlistings);
            assertEquals(List.of(FILE), services.changed);
            assertTrue(services.notices.getFirst().contains("Moved \"Game\""), services.notices::toString);
        }
    }
}
