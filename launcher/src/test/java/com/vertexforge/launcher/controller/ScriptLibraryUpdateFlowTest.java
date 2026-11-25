package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.dialog.LibraryUpdatePrompt;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.scriptlib.LibraryBaseline;
import com.vertexforge.launcher.scriptlib.LibraryPlan;
import com.vertexforge.launcher.scriptlib.LibrarySnapshot;
import com.vertexforge.launcher.service.FileJob;
import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.service.ScriptLibraryUpdater;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.PreviewOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.RecoveryOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.UpdateOutcome;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalLong;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.function.Function;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** "Update Script Library…" without the JavaFX toolkit (VK-1691): every answer, job and FX step driven by the test. */
class ScriptLibraryUpdateFlowTest {

    private static final Path ROOT = Path.of(TestSupport.isWindows() ? "C:\\projects" : "/projects");
    private static final Path FILE = ROOT.resolve("Game").resolve("Game.vfproj");
    private static final UUID WITH_LIBRARY = UUID.fromString("00000000-0000-0000-0000-000000000201");
    private static final UUID WITHOUT_LIBRARY = UUID.fromString("00000000-0000-0000-0000-000000000202");
    private static final String A = "a".repeat(64);
    private static final String B = "b".repeat(64);
    private static final String C = "c".repeat(64);

    private final QueueExecutor fx = new QueueExecutor();
    private final QueueExecutor jobThread = new QueueExecutor();
    private final FakeServices services = new FakeServices();
    private final ScriptLibraryUpdateFlow flow = new ScriptLibraryUpdateFlow(services, fx);
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

    /** One call to {@code startUpdate}. */
    private record Started(ScriptLibraryUpdater.Request request, String fingerprint, boolean keepMine) {
    }

    private final class FakeServices implements ScriptLibraryUpdateFlow.Services {
        CompletableFuture<EngineOptions> engines = new CompletableFuture<>();
        ProjectFileCheck check;
        FolderEditor folderEditor = FolderEditor.NONE;
        Function<OpenProjectFlow.EnginePrompt, Optional<EngineOption>> chooser = prompt -> Optional.empty();
        boolean confirmRecovery;
        final Deque<Optional<Boolean>> answers = new ArrayDeque<>();
        final Deque<PreviewOutcome> previews = new ArrayDeque<>();
        final Deque<UpdateOutcome> updates = new ArrayDeque<>();
        RecoveryOutcome recovery;

        int engineChecks;
        final List<OpenProjectFlow.EnginePrompt> enginePrompts = new ArrayList<>();
        final List<String> recoveryQuestions = new ArrayList<>();
        final List<LibraryUpdatePrompt> prompts = new ArrayList<>();
        final List<String> jobs = new ArrayList<>();
        final List<Boolean> jobClosed = new ArrayList<>();
        final List<ScriptLibraryUpdater.Request> previewRequests = new ArrayList<>();
        final List<Started> updateRequests = new ArrayList<>();
        final List<String> recoveries = new ArrayList<>();
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
            return CompletableFuture.completedFuture(check);
        }

        @Override
        public FolderEditor folderEditor(Path projectFile) {
            return folderEditor;
        }

        @Override
        public Optional<EngineOption> chooseEngine(OpenProjectFlow.EnginePrompt prompt) {
            enginePrompts.add(prompt);
            return chooser.apply(prompt);
        }

        @Override
        public boolean confirmRecovery(String name, String detail) {
            recoveryQuestions.add(name + ": " + detail);
            return confirmRecovery;
        }

        @Override
        public Optional<Boolean> ask(LibraryUpdatePrompt prompt) {
            prompts.add(prompt);
            return answers.isEmpty() ? Optional.empty() : answers.removeFirst();
        }

        @Override
        public OpenProjectFlow.JobView showJob(String title, String header, Runnable cancel) {
            jobs.add(header);
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
        public FileJob<PreviewOutcome> startPreview(ScriptLibraryUpdater.Request request,
                                                    FileJob.ProgressListener listener) {
            previewRequests.add(request);
            PreviewOutcome outcome = previews.removeFirst();
            return FileJob.start(jobThread,
                    monitor -> monitor.cancelRequested() ? new PreviewOutcome.Cancelled() : outcome, listener,
                    PreviewOutcome.Failed::new);
        }

        @Override
        public FileJob<UpdateOutcome> startUpdate(ScriptLibraryUpdater.Request request, String fingerprint,
                                                  boolean keepMine, FileJob.ProgressListener listener) {
            updateRequests.add(new Started(request, fingerprint, keepMine));
            UpdateOutcome outcome = updates.removeFirst();
            return FileJob.start(jobThread,
                    monitor -> monitor.cancelRequested() ? new UpdateOutcome.Cancelled() : outcome, listener,
                    UpdateOutcome.Failed::new);
        }

        @Override
        public FileJob<RecoveryOutcome> startRecovery(Path projectFile, String workingDirectory) {
            recoveries.add(projectFile + "|" + workingDirectory);
            return FileJob.start(jobThread, monitor -> recovery, FileJob.ProgressListener.NONE,
                    RecoveryOutcome.Failed::new);
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
        finishJobs();
        assertEquals(runs, services.releases, "releases " + services.releases + " for " + runs + " runs");
        assertFalse(flow.jobRunning());
        assertTrue(services.jobClosed.stream().allMatch(closed -> closed), "every job window closed");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Fixtures
    // -----------------------------------------------------------------------------------------------------------------

    private static EngineManifest manifest(String engineVersion, boolean library) throws Exception {
        return EngineManifest.parse("{\"manifestVersion\": 1, \"engineVersion\": \"" + engineVersion + "\","
                + " \"assetFormatVersion\": \"1.0.0\", \"projectSchemaVersion\": \"1.0\","
                + " \"configuration\": \"Development\", \"editorExe\": \"Editor.exe\", \"workingDirectory\": \".\""
                + (library ? ", \"scriptLibDir\": \"lib\"" : "") + "}");
    }

    private static Path exe(UUID id) {
        return ROOT.resolve("engines").resolve(id.toString()).resolve("Editor.exe");
    }

    private static EngineOption engine(UUID id, String engineVersion, boolean library) throws Exception {
        Path exe = exe(id);
        return new EngineOption(Optional.of(id), exe, exe.toString(),
                EngineHealth.ok(manifest(engineVersion, library), exe.getParent()), false, false, false);
    }

    private static EngineOptions options(EngineOption... engines) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.empty(), StoreStatus.OK,
                Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.empty(), List.of(engines));
    }

    private static ProjectFile project(String engineVersion) {
        return new ProjectFile("Game", "1.0", engineVersion, null, "assets", "1.0.0");
    }

    private static RecentEntry entry(UUID engineId) {
        RecentEntry entry = RecentEntry.create(UUID.fromString("00000000-0000-0000-0000-000000000001"), FILE, "Game",
                TestSupport.T0);
        return engineId == null ? entry : entry.withEngineId(engineId.toString());
    }

    private static LibraryPlan plan(String project, String engine, String recorded) {
        try {
            LibraryPlan.Inputs inputs = new LibraryPlan.Inputs(LibrarySnapshot.of(Map.of("engine/Log.mt", project)),
                    List.of("engine/Log.mt"), List.of("engine"), LibrarySnapshot.of(Map.of("engine/Log.mt", engine)),
                    List.of("engine"), new LibraryBaseline.Recorded("1.0.0",
                    LibrarySnapshot.of(Map.of("engine/Log.mt", recorded))), "1.0.0");
            return LibraryPlan.of(inputs, false);
        } catch (LibrarySnapshot.DuplicatePathException e) {
            throw new IllegalStateException(e);
        }
    }

    private static PreviewOutcome.Ready ready(LibraryPlan plan, String fingerprint) {
        Path library = ROOT.resolve("Game/assets/scripts/lib");
        ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(FILE, "assets", library, "1.0.0");
        return new PreviewOutcome.Ready(new ScriptLibraryUpdater.Preview(request, library, true,
                new LibraryBaseline.Recorded("1.0.0", LibrarySnapshot.EMPTY), plan, List.of(), fingerprint,
                ROOT.resolve("Game/.vertexforge/backups"), List.of()));
    }

    private static PreviewOutcome.Ready outdated(String fingerprint) {
        return ready(plan(A, B, A), fingerprint);
    }

    /** Starts a run whose engines and file read have answered, and lets the FX thread take them. */
    private void run(RecentEntry entry, EngineOptions options, ProjectFileCheck check) {
        services.engines.complete(options);
        services.check = check;
        runs++;
        flow.update(new ScriptLibraryUpdateFlow.Request(FILE, entry, "Game"));
        fx.runAll();
    }

    private void run(EngineOption engine) {
        run(entry(engine.id().orElseThrow()), options(engine), new ProjectFileCheck.Readable(project(
                "VertexForge 1.0.0"), EditorLockState.FREE));
    }

    /** Runs every job and every FX step they lead to, until nothing is left (a run may chain several jobs). */
    private void finishJobs() {
        while (!jobThread.idle() || !fx.idle()) {
            jobThread.runAll();
            fx.runAll();
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The way through
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aConfirmedUpdateRunsAndEndsInOneReleaseWithTheBackupToShow() throws Exception {
        EngineOption engine = engine(WITH_LIBRARY, "1.0.0", true);
        services.previews.add(outdated("fp-1"));
        services.answers.add(Optional.of(false));
        Path zip = ROOT.resolve("Game/.vertexforge/backups/scripts-lib-1.0.0-20260102-030405Z.zip");
        services.updates.add(new UpdateOutcome.Done(zip, plan(A, B, A), List.of("A note.")));

        run(engine);

        assertEquals(List.of(new ScriptLibraryUpdater.Request(FILE, "assets", exe(WITH_LIBRARY).getParent()
                .resolve("lib").normalize(), "1.0.0")), services.previewRequests);
        assertTrue(flow.jobRunning());
        assertEquals(0, services.releases, "nothing is released while the preview runs");

        finishJobs();

        assertEquals(1, services.prompts.size());
        assertEquals(List.of(new Started(services.previewRequests.getFirst(), "fp-1", false)),
                services.updateRequests, "the update is the confirmed one, with the preview's fingerprint");
        assertEquals(List.of(FILE), services.changed, "the row is checked again");
        assertEquals(1, services.releases);
        String notice = services.notices.getFirst();
        assertTrue(notice.contains("was updated to " + engine.label()), notice);
        assertTrue(notice.contains("Backup: " + zip), notice);
        assertTrue(notice.contains("A note."), notice);
        assertEquals(zip, services.reveals.getFirst());
        assertEquals(List.of("Comparing the script library of \"Game\"…",
                "Updating the script library of \"Game\"…"), services.jobs);
    }

    @Test
    void keepingTheUsersCopiesIsPassedOnAndReported() throws Exception {
        services.previews.add(ready(plan(C, B, A), "fp-1"));
        services.answers.add(Optional.of(true));
        LibraryPlan kept = LibraryPlan.of(new LibraryPlan.Inputs(LibrarySnapshot.of(Map.of("engine/Log.mt", C)),
                List.of("engine/Log.mt"), List.of("engine"), LibrarySnapshot.of(Map.of("engine/Log.mt", B)),
                List.of("engine"), new LibraryBaseline.Recorded("1.0.0",
                LibrarySnapshot.of(Map.of("engine/Log.mt", A))), "1.0.0"), true);
        services.updates.add(new UpdateOutcome.Done(ROOT.resolve("b.zip"), kept, List.of()));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertTrue(services.updateRequests.getFirst().keepMine());
        assertEquals(1, services.prompts.getFirst().conflicts());
        assertTrue(services.notices.getFirst().contains("1 file(s) you changed were kept"),
                services.notices.getFirst());
    }

    @Test
    void replacedConflictsSayWhereTheUsersCopiesAre() throws Exception {
        services.previews.add(ready(plan(C, B, A), "fp-1"));
        services.answers.add(Optional.of(false));
        services.updates.add(new UpdateOutcome.Done(ROOT.resolve("b.zip"), plan(C, B, A), List.of()));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertTrue(services.notices.getFirst().contains("were replaced; your copies are in the backup"),
                services.notices.getFirst());
    }

    @Test
    void anUpToDateLibraryIsReportedWithoutAQuestion() throws Exception {
        services.previews.add(ready(plan(A, A, A), "fp-1"));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertTrue(services.prompts.isEmpty());
        assertTrue(services.updateRequests.isEmpty());
        assertTrue(services.notices.getFirst().contains("is up to date with"), services.notices.toString());
        assertNull(services.reveals.getFirst());
    }

    @Test
    void cancellingTheQuestionChangesNothing() throws Exception {
        services.previews.add(outdated("fp-1"));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertEquals(1, services.prompts.size());
        assertTrue(services.updateRequests.isEmpty());
        assertTrue(services.errors.isEmpty());
        assertTrue(services.notices.isEmpty());
    }

    @Test
    void aLibraryThatChangedSinceThePreviewIsAskedAboutAgain() throws Exception {
        services.previews.add(outdated("fp-1"));
        services.answers.add(Optional.of(false));
        services.answers.add(Optional.of(false));
        services.updates.add(new UpdateOutcome.Changed(outdated("fp-2").preview()));
        services.updates.add(new UpdateOutcome.Done(null, plan(A, B, A), List.of()));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertEquals(2, services.prompts.size());
        assertTrue(services.prompts.get(1).intro().contains("changed since the last look"),
                services.prompts.get(1).intro());
        assertEquals(List.of("fp-1", "fp-2"), services.updateRequests.stream().map(Started::fingerprint).toList(),
                "the second update carries the new preview's fingerprint");
        assertEquals(List.of(FILE, FILE), services.changed);
        assertNull(services.reveals.getFirst(), "no backup to show");
    }

    @Test
    void anUpdateThatFindsNothingToDoSaysSo() throws Exception {
        services.previews.add(outdated("fp-1"));
        services.answers.add(Optional.of(false));
        services.updates.add(new UpdateOutcome.UpToDate());

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertTrue(services.notices.getFirst().contains("is already up to date"), services.notices.toString());
    }

    @Test
    void aCancelledUpdateSaysTheLibraryIsAsItWas() throws Exception {
        services.previews.add(outdated("fp-1"));
        services.answers.add(Optional.of(false));
        services.updates.add(new UpdateOutcome.Done(null, plan(A, B, A), List.of()));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        jobThread.runAll();
        fx.runAll();
        // The update job is queued: the launcher closes and cancels it before it runs.
        assertTrue(flow.jobRunning());
        flow.cancelJob();
        finishJobs();

        assertTrue(services.notices.getFirst().contains("was cancelled"), services.notices.toString());
        assertTrue(services.errors.isEmpty());
    }

    @Test
    void aCancelledPreviewEndsQuietly() throws Exception {
        services.previews.add(outdated("fp-1"));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        flow.cancelJob();
        finishJobs();

        assertTrue(services.prompts.isEmpty());
        assertTrue(services.notices.isEmpty());
        assertTrue(services.errors.isEmpty());
    }

    @Test
    void aRefusedFailedOrInterruptedUpdateIsAnError() throws Exception {
        List<UpdateOutcome> outcomes = List.of(new UpdateOutcome.Refused("refused because"),
                new UpdateOutcome.Failed("failed because"), new UpdateOutcome.Interrupted("interrupted because"));
        for (UpdateOutcome outcome : outcomes) {
            services.engines = new CompletableFuture<>();
            services.previews.add(outdated("fp"));
            services.answers.add(Optional.of(false));
            services.updates.add(outcome);

            run(engine(WITH_LIBRARY, "1.0.0", true));
            finishJobs();
        }

        assertEquals(List.of(ScriptLibraryUpdateFlow.TITLE + ": refused because",
                ScriptLibraryUpdateFlow.TITLE + ": failed because",
                ScriptLibraryUpdateFlow.TITLE + ": interrupted because"), services.errors);
    }

    @Test
    void aRefusedOrFailedPreviewIsAnError() throws Exception {
        services.previews.add(new PreviewOutcome.Refused("no scripts folder"));
        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        services.engines = new CompletableFuture<>();
        services.previews.add(new PreviewOutcome.Failed("disk on fire"));
        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertEquals(List.of(ScriptLibraryUpdateFlow.TITLE + ": no scripts folder",
                ScriptLibraryUpdateFlow.TITLE + ": disk on fire"), services.errors);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // An interrupted update
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anInterruptedUpdateIsFinishedFirstWhenTheUserAgrees() throws Exception {
        services.previews.add(new PreviewOutcome.Interrupted("An earlier update was interrupted."));
        services.previews.add(outdated("fp-1"));
        services.confirmRecovery = true;
        services.recovery = new RecoveryOutcome.Recovered("An interrupted update was undone.", false);

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertEquals(List.of("Game: An earlier update was interrupted."), services.recoveryQuestions);
        assertEquals(List.of(FILE + "|assets"), services.recoveries);
        assertEquals(2, services.previewRequests.size(), "a new preview after the recovery");
        assertEquals(List.of(FILE), services.changed, "the row is checked again after the recovery");
        assertTrue(services.prompts.getFirst().intro().contains("An interrupted update was undone."),
                services.prompts.getFirst().intro());
    }

    @Test
    void aRecoveryWithNothingToDoGoesOnWithThePreview() throws Exception {
        services.previews.add(new PreviewOutcome.Interrupted("detail"));
        services.previews.add(ready(plan(A, A, A), "fp"));
        services.confirmRecovery = true;
        services.recovery = new RecoveryOutcome.NothingToDo();

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertTrue(services.notices.getFirst().contains("is up to date"), services.notices.toString());
    }

    @Test
    void anInterruptedUpdateTheUserLeavesAloneChangesNothing() throws Exception {
        services.previews.add(new PreviewOutcome.Interrupted("detail"));

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertEquals(1, services.recoveryQuestions.size());
        assertTrue(services.recoveries.isEmpty());
        assertTrue(services.errors.isEmpty());
    }

    @Test
    void aRecoveryThatNeedsTheUserIsAnError() throws Exception {
        services.previews.add(new PreviewOutcome.Interrupted("detail"));
        services.confirmRecovery = true;
        services.recovery = new RecoveryOutcome.NeedsAttention("Look at lib.old-1a2b3c4d.");

        run(engine(WITH_LIBRARY, "1.0.0", true));
        finishJobs();

        assertEquals(List.of(ScriptLibraryUpdateFlow.TITLE + ": Look at lib.old-1a2b3c4d."), services.errors);
        assertEquals(1, services.previewRequests.size());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Refusals before anything runs
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aProjectAnEditorOfThisWindowHasOpenIsRefusedAtOnce() throws Exception {
        services.folderEditor = FolderEditor.THIS_PROJECT;

        run(engine(WITH_LIBRARY, "1.0.0", true));

        assertEquals(0, services.engineChecks, "refused before the engines are even checked");
        assertTrue(services.errors.getFirst().contains("is open in an editor this launcher started"),
                services.errors.toString());
        assertTrue(services.previewRequests.isEmpty());
    }

    @Test
    void aProjectWhoseFolderAnEditorOfThisWindowHasOpenOnAnotherFileIsRefusedAtOnce() throws Exception {
        services.folderEditor = FolderEditor.OTHER_PROJECT;

        run(engine(WITH_LIBRARY, "1.0.0", true));

        assertEquals(0, services.engineChecks, "refused before the engines are even checked");
        assertTrue(services.errors.getFirst().contains("on another project file"), services.errors.toString());
        assertTrue(services.previewRequests.isEmpty());
    }

    @Test
    void aProjectAnotherEditorHasOpenIsRefused() throws Exception {
        EngineOption engine = engine(WITH_LIBRARY, "1.0.0", true);

        run(entry(WITH_LIBRARY), options(engine), new ProjectFileCheck.Readable(project("VertexForge 1.0.0"),
                new EditorLockState.Held(OptionalLong.of(42))));

        assertEquals(List.of(ScriptLibraryUpdateFlow.TITLE + ": \"Game\" is open in an editor (process 42). Close "
                + "the editor, then update its script library."), services.errors);
        assertTrue(services.previewRequests.isEmpty());
    }

    @Test
    void aProjectFileThatCannotBeUsedIsRefused() throws Exception {
        EngineOption engine = engine(WITH_LIBRARY, "1.0.0", true);
        List<ProjectFileCheck> checks = List.of(new ProjectFileCheck.Missing("The project file does not exist"),
                new ProjectFileCheck.Unreadable("access denied"), new ProjectFileCheck.Invalid("not JSON"));
        for (ProjectFileCheck check : checks) {
            services.engines = new CompletableFuture<>();
            run(entry(WITH_LIBRARY), options(engine), check);
        }

        assertEquals(List.of(ScriptLibraryUpdateFlow.TITLE + ": The project file does not exist.",
                ScriptLibraryUpdateFlow.TITLE + ": \"Game\" could not be read: access denied",
                ScriptLibraryUpdateFlow.TITLE + ": \"Game\" cannot be updated: not JSON"), services.errors);
    }

    @Test
    void anEngineCheckThatFailsSaysSo() {
        services.engines.completeExceptionally(new IllegalStateException("registry broken"));
        services.check = new ProjectFileCheck.Readable(project("VertexForge 1.0.0"), EditorLockState.FREE);
        runs++;
        flow.update(new ScriptLibraryUpdateFlow.Request(FILE, entry(WITH_LIBRARY), "Game"));
        fx.runAll();

        assertTrue(services.errors.getFirst().contains("Could not check the engines: registry broken"),
                services.errors.toString());
    }

    @Test
    void theBoundEngineWithoutAScriptLibraryIsRefused() throws Exception {
        run(engine(WITHOUT_LIBRARY, "1.0.0", false));

        assertTrue(services.errors.getFirst().contains("names no script library"), services.errors.toString());
    }

    @Test
    void anEngineThatCannotOpenTheProjectIsRefused() throws Exception {
        EngineOption engine = engine(WITH_LIBRARY, "1.0.0", true);

        run(entry(WITH_LIBRARY), options(engine), new ProjectFileCheck.Readable(
                new ProjectFile("Game", "1.0", "VertexForge 2.0.0", null, "assets", "2.0.0"), EditorLockState.FREE));

        assertTrue(services.errors.getFirst().endsWith("Its script library cannot be updated to this engine's."),
                services.errors.toString());
        assertTrue(services.previewRequests.isEmpty());
    }

    @Test
    void whenTheEngineMustBeChosenOnlyEnginesWithALibraryAreOffered() throws Exception {
        EngineOption with = engine(WITH_LIBRARY, "1.0.0", true);
        EngineOption without = engine(WITHOUT_LIBRARY, "1.0.0", false);
        services.chooser = prompt -> Optional.of(with);
        services.previews.add(outdated("fp"));

        run(entry(null), options(with, without), new ProjectFileCheck.Readable(project("VertexForge 1.0.0"),
                EditorLockState.FREE));
        finishJobs();

        OpenProjectFlow.EnginePrompt prompt = services.enginePrompts.getFirst();
        assertEquals(List.of(with), prompt.candidates());
        assertTrue(prompt.problem().contains("have no script library are not listed"), prompt.problem());
        assertFalse(prompt.canRemember(), "the choice is not remembered");
        assertTrue(prompt.offered().test(with));
        assertFalse(prompt.offered().test(without));
        assertEquals(exe(WITH_LIBRARY).getParent().resolve("lib").normalize(),
                services.previewRequests.getFirst().engineLibrary());
    }

    @Test
    void choosingNoEngineEndsTheRun() throws Exception {
        run(entry(null), options(engine(WITH_LIBRARY, "1.0.0", true), engine(WITHOUT_LIBRARY, "1.0.0", true)),
                new ProjectFileCheck.Readable(project("VertexForge 1.0.0"), EditorLockState.FREE));

        assertEquals(1, services.enginePrompts.size());
        assertTrue(services.errors.isEmpty());
        assertTrue(services.previewRequests.isEmpty());
    }

    @Test
    void everyPhaseHasAText() {
        for (JobMonitor.Phase phase : JobMonitor.Phase.values()) {
            assertFalse(ScriptLibraryUpdateFlow.phaseText(phase).isBlank(), phase.name());
        }
        assertEquals("Putting the new library in place", ScriptLibraryUpdateFlow.phaseText(
                JobMonitor.Phase.COMMITTING));
    }
}
