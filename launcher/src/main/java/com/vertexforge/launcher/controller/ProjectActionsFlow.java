package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.CleanCachesPrompt;
import com.vertexforge.launcher.dialog.CopyNamePrompt;
import com.vertexforge.launcher.dialog.DeletePrompt;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.VersionCompatibility;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RemovedProject;
import com.vertexforge.launcher.service.DeleteGuards;
import com.vertexforge.launcher.service.FileJob;
import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.ProjectBackupService.BackupOutcome;
import com.vertexforge.launcher.service.ProjectBackupService.BackupRequest;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CleanOutcome;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CleanRequest;
import com.vertexforge.launcher.service.ProjectCacheCleaner.PreviewOutcome;
import com.vertexforge.launcher.service.ProjectCopier.CopyOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.CheckOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.DeleteOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.DeletePreflight;
import com.vertexforge.launcher.service.ProjectDeleter.DeleteRequest;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Predicate;

/**
 * The project actions of a listed project's menu (VK-1695): Duplicate…, Back Up, Clean Caches… and Move to Recycle
 * Bin…. Free of the JavaFX toolkit, so it runs in unit tests; the window shows its questions and runs its jobs.
 * <p>
 * Every action starts the same way: an editor this launcher started on the project's folder stops it at once; then
 * the project file is read again, with its folder's editor lock, and an editor that has the project open stops it too
 * (each job checks the lock once more, and another launcher's, right before it touches anything). Then:
 * <ul>
 *   <li><b>Duplicate…</b> asks the copy's name (offering {@code <Folder>-copy}), copies the project beside it with its
 *       project file and scripts project renamed after it, and lists the copy, bound to the original's engine. It
 *       opens nothing.</li>
 *   <li><b>Back Up</b> zips the project beside its folder (VK-1690's backup) and says where the zip is.</li>
 *   <li><b>Clean Caches…</b> takes the folders to clean from the engine Open would start (or one the user picks among
 *       those that name such folders), shows what is there and deletes it once the user agrees.</li>
 *   <li><b>Move to Recycle Bin…</b> checks the folder (and asks Windows whether its Recycle Bin can take it) before the
 *       user types the project's name, then moves it and takes the project off the list.</li>
 * </ul>
 * Every run ends in exactly one {@link Services#release()}: the window keeps its project buttons disabled until then.
 * Everything here runs on the FX thread (in tests, the test thread); {@code fx} brings background answers back to it.
 */
public final class ProjectActionsFlow {

    static final String DUPLICATE_TITLE = CopyNamePrompt.Purpose.DUPLICATE.title();
    static final String BACKUP_TITLE = "Back Up Project";
    static final String CLEAN_TITLE = CleanCachesPrompt.TITLE;
    static final String DELETE_TITLE = DeletePrompt.TITLE;

    /**
     * One action on one listed project.
     *
     * @param projectFile the project file
     * @param entry       its entry
     * @param name        the name to show
     */
    public record Request(Path projectFile, RecentEntry entry, String name) {
        public Request {
            Objects.requireNonNull(projectFile, "projectFile");
            Objects.requireNonNull(name, "name");
        }
    }

    /** What the flow needs from the window. Only the questions may block (they show dialogs). */
    public interface Services {
        /** Lists and checks the engines; completes on any thread. */
        CompletableFuture<EngineOptions> engines();

        /** Reads the project file and its folder's editor lock; never exceptionally. */
        CompletableFuture<ProjectFileCheck> read(Path projectFile);

        /** Which editor of this window has a project of the file's folder open. */
        FolderEditor folderEditor(Path projectFile);

        /** Whether the project list cannot be written (a duplicate is then not listed). */
        boolean readOnly();

        /** What a delete of this project is checked against: protected folders, engines, the other listed projects. */
        DeleteGuards.DeleteContext deleteContext(Path projectFile, EngineOptions options);

        /** Asks which engine to use; empty when the user cancelled. */
        Optional<EngineOption> chooseEngine(OpenProjectFlow.EnginePrompt prompt);

        /** The name Duplicate offers, looked up beside the project; completes on any thread. */
        CompletableFuture<String> suggestDuplicateName(Path projectFile, String projectName);

        /** Asks the duplicate's name; empty when the user cancelled. */
        Optional<String> askCopyName(CopyNamePrompt prompt);

        /** Asks to clean; {@code false} when the user cancelled. */
        boolean confirmClean(CleanCachesPrompt prompt);

        /** Asks the user to type the project's name; the typed text, or empty when the user cancelled. */
        Optional<String> askDeleteName(DeletePrompt prompt);

        /** Shows a job's progress, naming its phases with {@code texts}; {@code cancel} asks it to stop. */
        OpenProjectFlow.JobView showJob(String title, String header, Function<JobMonitor.Phase, String> texts,
                                        Runnable cancel);

        /** Starts a duplicate on the window's job thread; returns at once. */
        FileJob<CopyOutcome> startDuplicate(Path projectFile, String workingDirectory, String newName,
                                            FileJob.ProgressListener listener);

        /** Starts a backup on the window's job thread; returns at once. */
        FileJob<BackupOutcome> startBackup(BackupRequest request, FileJob.ProgressListener listener);

        /** Starts looking at what Clean Caches would delete, on the window's job thread; returns at once. */
        FileJob<PreviewOutcome> startCleanPreview(CleanRequest request, FileJob.ProgressListener listener);

        /** Starts cleaning on the window's job thread; returns at once. */
        FileJob<CleanOutcome> startClean(CleanRequest request, FileJob.ProgressListener listener);

        /** Starts checking a move to the Recycle Bin on the window's job thread; returns at once. */
        FileJob<CheckOutcome> startDeleteCheck(DeleteRequest request, FileJob.ProgressListener listener);

        /** Starts the move to the Recycle Bin on the window's job thread; returns at once. */
        FileJob<DeleteOutcome> startDelete(DeleteRequest request, String typed, FileJob.ProgressListener listener);

        /** Lists a duplicate, bound as {@code rawEngineId} says; completes on any thread. */
        CompletableFuture<AddedProject> listDuplicate(Path projectFile, String name, String rawEngineId);

        /** Takes a deleted project off the list; completes on any thread. */
        CompletableFuture<RemovedProject> unlist(UUID id);

        /** A duplicate was listed: the window shows the list and selects it. */
        void listed(AddedProject added);

        /** A deleted project was taken off the list: the window shows the list. */
        void unlisted(RemovedProject removed);

        /** The project's files changed (or may have): its row is checked again. */
        void changed(Path projectFile);

        /** The run is over: the window clears its busy state. */
        void release();

        /** Shows an error. */
        void error(String title, String message);

        /**
         * Shows a note in the window.
         *
         * @param reveal a file or folder to offer "Show in folder" for, or {@code null}
         */
        void notice(String message, Path reveal);
    }

    /** What the opening step learned: the project file as read now, and the engines when the action needs them. */
    private record Opened(Request request, ProjectFile project, EngineOptions options) {
        String name() {
            return request.name();
        }

        Path projectFile() {
            return request.projectFile();
        }

        String workingDirectory() {
            return project.workingDirectory();
        }

        ProjectContent content() {
            return ProjectContent.of(request.projectFile(), project.workingDirectory());
        }
    }

    private final Services services;
    private final Executor fx;
    private FileJob<?> activeJob;

    /** @param fx runs the background answers on the FX thread */
    public ProjectActionsFlow(Services services, Executor fx) {
        this.services = Objects.requireNonNull(services, "services");
        this.fx = Objects.requireNonNull(fx, "fx");
    }

    /** Asks a running job to stop (the launcher is closing). */
    public void cancelJob() {
        FileJob<?> job = activeJob;
        if (job != null) {
            job.cancel();
        }
    }

    /** Whether a job of this flow is running. */
    public boolean jobRunning() {
        return activeJob != null;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The opening step
    // ---------------------------------------------------------------------------------------------------------------

    /**
     * Refuses when an editor has the project open, reads the project file (and the engines when {@code withEngines}),
     * and hands what it read to {@code next} on the FX thread.
     *
     * @param title the action's title, for errors
     * @param then  what the user must do after closing the editor ("duplicate it")
     */
    private void open(Request request, String title, String then, boolean withEngines, Consumer<Opened> next) {
        Objects.requireNonNull(request, "request");
        guarded(() -> {
            FolderEditor folderEditor = services.folderEditor(request.projectFile());
            if (folderEditor == FolderEditor.THIS_PROJECT) {
                fail(title, "\"" + request.name() + "\" is open in an editor this launcher started. Close the editor, "
                        + "then " + then + ".");
                return;
            }
            if (folderEditor == FolderEditor.OTHER_PROJECT) {
                fail(title, "\"" + request.name() + "\" is in a folder an editor of this launcher has open, on another "
                        + "project file. Close that editor, then " + then + ".");
                return;
            }
            CompletableFuture<EngineOptions> engines = withEngines
                    ? services.engines()
                    : CompletableFuture.completedFuture(null);
            CompletableFuture<ProjectFileCheck> file = services.read(request.projectFile());
            CompletableFuture.allOf(engines, file).whenCompleteAsync((ignored, error) -> guarded(() -> {
                EngineOptions options = null;
                if (withEngines) {
                    Throwable enginesError = failure(engines);
                    options = enginesError == null ? engines.join() : null;
                    if (options == null) {
                        fail(title, "Could not check the engines: " + describe(enginesError));
                        return;
                    }
                }
                Throwable fileError = failure(file);
                ProjectFileCheck check = fileError == null ? file.join() : null;
                if (check == null) {
                    check = new ProjectFileCheck.Unreadable("Could not check the project file: "
                            + describe(fileError));
                }
                Optional<String> refusal = refusal(request.name(), then, check);
                if (refusal.isPresent()) {
                    fail(title, refusal.get());
                    return;
                }
                next.accept(new Opened(request, ((ProjectFileCheck.Readable) check).file(), options));
            }), fx);
        });
    }

    /** Why the action cannot start on the project as read; empty when it can. */
    static Optional<String> refusal(String name, String then, ProjectFileCheck check) {
        return switch (check) {
            case ProjectFileCheck.Missing missing -> Optional.of(missing.reason() + ".");
            case ProjectFileCheck.Unreadable unreadable ->
                    Optional.of("\"" + name + "\" could not be read: " + unreadable.reason());
            case ProjectFileCheck.Invalid invalid ->
                    Optional.of("\"" + name + "\" is not a project the editor can open: " + invalid.reason());
            case ProjectFileCheck.Readable readable -> readable.lock() instanceof EditorLockState.Held held
                    ? Optional.of("\"" + name + "\" is open in an editor"
                    + (held.pid().isPresent() ? " (process " + held.pid().getAsLong() + ")" : "")
                    + ". Close the editor, then " + then + ".")
                    : Optional.empty();
        };
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Duplicate…
    // ---------------------------------------------------------------------------------------------------------------

    /** Duplicates the project; the window has already set its busy state, which this clears through the services. */
    public void duplicate(Request request) {
        open(request, DUPLICATE_TITLE, "duplicate it", false, opened -> {
            ProjectContent content = opened.content();
            Optional<String> unavailable = content.unavailableReason();
            if (unavailable.isPresent()) {
                fail(DUPLICATE_TITLE, "\"" + opened.name() + "\" cannot be duplicated: " + unavailable.get());
                return;
            }
            services.suggestDuplicateName(opened.projectFile(), opened.project().projectName())
                    .whenCompleteAsync((suggested, error) -> guarded(() -> askDuplicateName(opened,
                            suggested != null ? suggested : opened.project().projectName() + "-copy", null)), fx);
        });
    }

    /** @param problem why the copier refused the last name, or {@code null} */
    private void askDuplicateName(Opened opened, String suggested, String problem) {
        Path location = opened.content().folder().getParent();
        Optional<String> name = services.askCopyName(new CopyNamePrompt(opened.name(), location, suggested, problem,
                CopyNamePrompt.Purpose.DUPLICATE));
        if (name.isEmpty()) {
            services.release();
            return;
        }
        String newName = name.get();
        OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
        FileJob<CopyOutcome> job = services.startDuplicate(opened.projectFile(), opened.workingDirectory(), newName,
                progressTo(view));
        view[0] = services.showJob(DUPLICATE_TITLE, "Duplicating \"" + opened.name() + "\" as \"" + newName + "\"…",
                ProjectActionsFlow::duplicatePhaseText, job::cancel);
        follow(job, view[0], DUPLICATE_TITLE, outcome -> {
            switch (outcome) {
                case CopyOutcome.Done done -> listDuplicate(opened, newName, done);
                case CopyOutcome.Refused refused -> askDuplicateName(opened, newName, refused.reason());
                case CopyOutcome.Cancelled ignored -> {
                    services.release();
                    services.notice("Duplicating \"" + opened.name() + "\" was cancelled; nothing was left behind.",
                            null);
                }
                case CopyOutcome.Failed failed -> fail(DUPLICATE_TITLE, "The duplicate failed: " + failed.message()
                        + " \"" + opened.name() + "\" is unchanged.");
            }
        });
    }

    /** Lists the duplicate, bound as the original is; nothing is opened. */
    private void listDuplicate(Opened opened, String newName, CopyOutcome.Done done) {
        String made = "Duplicated \"" + opened.name() + "\" as \"" + newName + "\" in " + done.folder() + ".";
        String notes = notes(done.notes());
        if (services.readOnly()) {
            services.release();
            services.notice(made + " The project list is read-only, so the duplicate was not added to it; add it "
                    + "with Add Existing… later." + notes, done.projectFile());
            return;
        }
        String engineId = opened.request().entry() == null ? null : opened.request().entry().engineId();
        services.listDuplicate(done.projectFile(), newName, engineId).whenCompleteAsync((added, error) ->
                guarded(() -> {
                    services.release();
                    if (error != null || added == null) {
                        services.error(DUPLICATE_TITLE, made + " It could not be added to the list: "
                                + describe(unwrap(error)) + " Add it with Add Existing…." + notes);
                        return;
                    }
                    services.listed(added);
                    services.notice(made + " The original is unchanged." + notes, done.projectFile());
                }), fx);
    }

    /** The progress window's text for each phase of a duplicate. */
    static String duplicatePhaseText(JobMonitor.Phase phase) {
        return switch (phase) {
            case SCANNING -> "Looking at the project's files";
            case WRITING, VERIFYING, COPYING -> "Copying";
            case COMMITTING -> "Finishing";
        };
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Back Up
    // ---------------------------------------------------------------------------------------------------------------

    /** Backs the project up beside its folder; the window has already set its busy state. */
    public void backUp(Request request) {
        open(request, BACKUP_TITLE, "back it up", false, opened -> {
            Optional<String> unavailable = opened.content().unavailableReason();
            if (unavailable.isPresent()) {
                fail(BACKUP_TITLE, "\"" + opened.name() + "\" cannot be backed up: " + unavailable.get());
                return;
            }
            BackupRequest backup = new BackupRequest(opened.projectFile(), opened.workingDirectory(),
                    VersionCompatibility.savedVersion(opened.project()).orElse(null));
            OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
            FileJob<BackupOutcome> job = services.startBackup(backup, progressTo(view));
            view[0] = services.showJob(BACKUP_TITLE, "Backing up \"" + opened.name() + "\"…",
                    ProjectActionsFlow::backupPhaseText, job::cancel);
            follow(job, view[0], BACKUP_TITLE, outcome -> {
                switch (outcome) {
                    case BackupOutcome.Done done -> {
                        services.release();
                        services.notice("Backed up \"" + opened.name() + "\" to " + done.zip() + " ("
                                + JobMonitor.formatBytes(done.zipBytes()) + ", " + done.files()
                                + (done.files() == 1 ? " file)." : " files).") + notes(done.notes()), done.zip());
                    }
                    case BackupOutcome.Refused refused -> fail(BACKUP_TITLE, refused.reason());
                    case BackupOutcome.Cancelled ignored -> {
                        services.release();
                        services.notice("The backup of \"" + opened.name() + "\" was cancelled; nothing was left "
                                + "behind.", null);
                    }
                    case BackupOutcome.Failed failed -> fail(BACKUP_TITLE, "The backup failed: " + failed.message());
                }
            });
        });
    }

    /** The progress window's text for each phase of a backup. */
    static String backupPhaseText(JobMonitor.Phase phase) {
        return switch (phase) {
            case SCANNING -> "Looking at the project's files";
            case WRITING, COPYING -> "Writing the backup";
            case VERIFYING -> "Checking the backup";
            case COMMITTING -> "Finishing";
        };
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Clean Caches…
    // ---------------------------------------------------------------------------------------------------------------

    /** The folders {@code engine}'s manifest lists for Clean Caches; empty when it lists none (or has no manifest). */
    static Optional<List<String>> cacheDirs(EngineOption engine) {
        EngineManifest manifest = engine.health().manifest();
        return manifest == null ? Optional.empty() : Optional.ofNullable(manifest.projectCacheDirs());
    }

    /** Cleans the project's caches; the window has already set its busy state. */
    public void cleanCaches(Request request) {
        open(request, CLEAN_TITLE, "clean its caches", true, opened -> {
            Predicate<EngineOption> usable = engine -> engine.isOk() && cacheDirs(engine).isPresent()
                    && !VersionCompatibility.check(opened.project(), engine).blocksOpen();
            String engineId = request.entry() == null ? null : request.entry().engineId();
            EngineOption engine = switch (EngineResolver.resolve(opened.options(), engineId)) {
                case LaunchPlan.Ready ready when usable.test(ready.engine()) -> ready.engine();
                case LaunchPlan.Ready ready -> choose(opened, usable, "The engine that opens \"" + opened.name()
                        + "\" (" + ready.engine().label() + ") names no folders it rebuilds: engines built before "
                        + "VK-1695 do not.");
                case LaunchPlan.Choose choose -> choose(opened, usable, choose.problem());
            };
            if (engine == null) {
                return;
            }
            List<String> dirs = cacheDirs(engine).orElseThrow();
            if (dirs.isEmpty()) {
                services.release();
                services.notice(engine.label() + " rebuilds no folders in a project, so there is nothing to clean in \""
                        + opened.name() + "\".", null);
                return;
            }
            CleanRequest clean = new CleanRequest(opened.projectFile(), opened.workingDirectory(), dirs);
            OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
            FileJob<PreviewOutcome> job = services.startCleanPreview(clean, progressTo(view));
            view[0] = services.showJob(CLEAN_TITLE, "Looking for the caches of \"" + opened.name() + "\"…",
                    ProjectActionsFlow::cleanPhaseText, job::cancel);
            follow(job, view[0], CLEAN_TITLE, outcome -> {
                switch (outcome) {
                    case PreviewOutcome.Ready ready -> confirmClean(opened, engine, clean, ready);
                    case PreviewOutcome.Refused refused -> fail(CLEAN_TITLE, refused.reason());
                    case PreviewOutcome.Cancelled ignored -> services.release();
                    case PreviewOutcome.Failed failed -> fail(CLEAN_TITLE, failed.message());
                }
            });
        });
    }

    /**
     * Asks which engine names the folders to clean, among those that can open the project and name some; releases
     * and returns {@code null} when there is none or the user cancelled.
     */
    private EngineOption choose(Opened opened, Predicate<EngineOption> usable, String problem) {
        List<EngineOption> offered = opened.options().usable().stream().filter(usable).toList();
        if (offered.isEmpty()) {
            fail(CLEAN_TITLE, problem.trim() + " None of your engines that can open \"" + opened.name() + "\" names "
                    + "the folders it rebuilds, so the launcher does not know what is safe to delete.");
            return null;
        }
        Optional<EngineOption> chosen = services.chooseEngine(new OpenProjectFlow.EnginePrompt(
                "Clean the caches of \"" + opened.name() + "\" as which engine names them?",
                (problem + " Only engines that name the folders they rebuild are listed.").trim(), offered, false,
                false, usable));
        if (chosen.isEmpty()) {
            services.release();
            return null;
        }
        return chosen.get();
    }

    private void confirmClean(Opened opened, EngineOption engine, CleanRequest clean, PreviewOutcome.Ready ready) {
        CleanCachesPrompt prompt = new CleanCachesPrompt(opened.name(), engine.label(), ready.folders());
        if (prompt.present().isEmpty()) {
            services.release();
            String skipped = prompt.skippedLines().isEmpty() ? ""
                    : " Left alone: " + String.join("; ", prompt.skippedLines()) + ".";
            services.notice("There is nothing to clean in \"" + opened.name() + "\"." + skipped, null);
            return;
        }
        if (!services.confirmClean(prompt)) {
            services.release();
            return;
        }
        OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
        FileJob<CleanOutcome> job = services.startClean(clean, progressTo(view));
        view[0] = services.showJob(CLEAN_TITLE, "Cleaning the caches of \"" + opened.name() + "\"…",
                ProjectActionsFlow::cleanPhaseText, job::cancel);
        follow(job, view[0], CLEAN_TITLE, outcome -> {
            services.changed(opened.projectFile());
            switch (outcome) {
                case CleanOutcome.Done done -> {
                    services.release();
                    String text = "Cleaned \"" + opened.name() + "\": freed " + JobMonitor.formatBytes(done.freedBytes())
                            + (done.cleaned().isEmpty() ? "." : " (" + String.join(", ", done.cleaned().stream()
                            .map(folder -> folder.relative()).toList()) + ").");
                    if (done.failures().isEmpty()) {
                        services.notice(text, null);
                    } else {
                        services.error(CLEAN_TITLE, text + " " + String.join(" ", done.failures()));
                    }
                }
                case CleanOutcome.Cancelled cancelled -> {
                    services.release();
                    services.notice("Cleaning \"" + opened.name() + "\" was stopped after freeing "
                            + JobMonitor.formatBytes(cancelled.freedBytes()) + ".", null);
                }
                case CleanOutcome.Refused refused -> fail(CLEAN_TITLE, refused.reason());
                case CleanOutcome.Failed failed -> fail(CLEAN_TITLE, failed.message());
            }
        });
    }

    /** The progress window's text for each phase of a cache clean. */
    static String cleanPhaseText(JobMonitor.Phase phase) {
        return switch (phase) {
            case SCANNING -> "Looking at the caches";
            case WRITING, VERIFYING, COPYING -> "Checking";
            case COMMITTING -> "Deleting";
        };
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Move to Recycle Bin…
    // ---------------------------------------------------------------------------------------------------------------

    /** Moves the project's folder to the Recycle Bin; the window has already set its busy state. */
    public void delete(Request request) {
        open(request, DELETE_TITLE, "move it to the Recycle Bin", true, opened -> {
            DeleteRequest delete = new DeleteRequest(opened.projectFile(), opened.workingDirectory(),
                    opened.project().projectName(), services.deleteContext(opened.projectFile(), opened.options()));
            OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
            FileJob<CheckOutcome> job = services.startDeleteCheck(delete, progressTo(view));
            view[0] = services.showJob(DELETE_TITLE, "Checking \"" + opened.name() + "\"…",
                    ProjectActionsFlow::deletePhaseText, job::cancel);
            follow(job, view[0], DELETE_TITLE, outcome -> {
                switch (outcome) {
                    case CheckOutcome.Ready ready -> askDeleteName(opened, delete, ready.preflight());
                    case CheckOutcome.Refused refused -> fail(DELETE_TITLE, refused.reason());
                    case CheckOutcome.Cancelled ignored -> services.release();
                    case CheckOutcome.Failed failed -> fail(DELETE_TITLE, failed.message());
                }
            });
        });
    }

    private void askDeleteName(Opened opened, DeleteRequest delete, DeletePreflight preflight) {
        DeletePrompt prompt = new DeletePrompt(delete.projectName(), preflight.folder(), preflight.bytes(),
                preflight.files(), preflight.folders(), preflight.notes());
        Optional<String> typed = services.askDeleteName(prompt);
        if (typed.isEmpty() || !prompt.matches(typed.get())) {
            services.release();
            return;
        }
        // The dialog may have stayed open for a while: an editor of this launcher may have opened the project.
        if (services.folderEditor(opened.projectFile()) != FolderEditor.NONE) {
            fail(DELETE_TITLE, "\"" + opened.name() + "\" was opened in an editor this launcher started, so it was not "
                    + "moved. Close the editor, then try again.");
            return;
        }
        OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
        FileJob<DeleteOutcome> job = services.startDelete(delete, typed.get(), progressTo(view));
        view[0] = services.showJob(DELETE_TITLE, "Moving \"" + opened.name() + "\" to the Recycle Bin…",
                ProjectActionsFlow::deletePhaseText, job::cancel);
        follow(job, view[0], DELETE_TITLE, outcome -> {
            switch (outcome) {
                case DeleteOutcome.Recycled recycled -> unlist(opened, recycled);
                case DeleteOutcome.Refused refused -> {
                    services.changed(opened.projectFile());
                    fail(DELETE_TITLE, refused.reason());
                }
                case DeleteOutcome.Failed failed -> {
                    services.changed(opened.projectFile());
                    fail(DELETE_TITLE, failed.message());
                }
            }
        });
    }

    /** Takes the deleted project off the list (a read-only list keeps it, shown as missing). */
    private void unlist(Opened opened, DeleteOutcome.Recycled recycled) {
        String moved = "Moved \"" + opened.name() + "\" (" + recycled.folder() + ") to the Recycle Bin. To get it "
                + "back, restore it from there, then add it with Add Existing….";
        RecentEntry entry = opened.request().entry();
        if (entry == null || services.readOnly()) {
            services.changed(opened.projectFile());
            services.release();
            services.notice(moved, null);
            return;
        }
        services.unlist(entry.id()).whenCompleteAsync((removed, error) -> guarded(() -> {
            services.release();
            if (error != null || removed == null) {
                services.changed(opened.projectFile());
                services.error(DELETE_TITLE, moved + " It could not be taken off the list: "
                        + describe(unwrap(error)));
                return;
            }
            services.unlisted(removed);
            services.notice(moved, null);
        }), fx);
    }

    /** The progress window's text for each phase of a move to the Recycle Bin. */
    static String deletePhaseText(JobMonitor.Phase phase) {
        return switch (phase) {
            case SCANNING -> "Looking at the project's files";
            case WRITING, COPYING -> "Working";
            case VERIFYING -> "Asking Windows whether the Recycle Bin can take it";
            case COMMITTING -> "Moving it to the Recycle Bin";
        };
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------------------------------------------

    /** A listener that hands progress to the job's window once it is shown ({@code view[0]}). */
    private static FileJob.ProgressListener progressTo(OpenProjectFlow.JobView[] view) {
        return (phase, done, total) -> {
            OpenProjectFlow.JobView shown = view[0];
            if (shown != null) {
                shown.progress(phase, done, total);
            }
        };
    }

    /** Follows a job to its end: closes its window, then hands its outcome to {@code then} on the FX thread. */
    private <T> void follow(FileJob<T> job, OpenProjectFlow.JobView view, String title, Consumer<T> then) {
        activeJob = job;
        job.outcome().whenCompleteAsync((outcome, error) -> guarded(() -> {
            if (activeJob == job) {
                activeJob = null;
            }
            view.close();
            if (error != null || outcome == null) {
                // The outcome never completes exceptionally; this is only a safety net.
                fail(title, "It stopped unexpectedly: " + describe(unwrap(error)));
                return;
            }
            then.accept(outcome);
        }), fx);
    }

    private void fail(String title, String message) {
        services.release();
        services.error(title, message);
    }

    /**
     * Runs one step; a step that throws must not leave the window's busy state set, and must not go unseen: most steps
     * run in a future's callback, where a thrown exception would only fail that future.
     */
    private void guarded(Runnable step) {
        try {
            step.run();
        } catch (RuntimeException e) {
            services.release();
            services.error("Project action", "It stopped unexpectedly: " + e);
        } catch (Error e) {
            services.release();
            throw e;
        }
    }

    private static String notes(List<String> notes) {
        return notes.isEmpty() ? "" : " " + String.join(" ", notes);
    }

    /** The future's failure, or {@code null} when it completed normally. The future must be done. */
    private static Throwable failure(CompletableFuture<?> future) {
        try {
            future.join();
            return null;
        } catch (CompletionException | CancellationException e) {
            return unwrap(e);
        }
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while ((current instanceof CompletionException || current instanceof ExecutionException)
                && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static String describe(Throwable error) {
        if (error == null) {
            return "unknown error";
        }
        return error.getMessage() == null ? error.toString() : error.getMessage();
    }
}
