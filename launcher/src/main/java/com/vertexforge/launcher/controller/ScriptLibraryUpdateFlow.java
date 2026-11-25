package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.LibraryUpdatePrompt;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.VersionCompatibility;
import com.vertexforge.launcher.scriptlib.FileDecision;
import com.vertexforge.launcher.service.FileJob;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.service.ScriptLibraryStatusService.EngineLibrary;
import com.vertexforge.launcher.service.ScriptLibraryUpdater;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.PreviewOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.RecoveryOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.UpdateOutcome;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import java.util.function.Predicate;

/**
 * "Update Script Library…" on a listed project (VK-1691). Free of the JavaFX toolkit, so it runs in unit tests; the
 * window shows its questions.
 * <ol>
 *   <li>The engines are checked and the project file is read again, with its folder's editor lock: an editor that has
 *       the project open stops the update here.</li>
 *   <li>The engine is the one Open would start ({@link EngineResolver}), or the one the user picks among those that can
 *       open the project and have a script library. The choice is not remembered.</li>
 *   <li>A preview job compares the libraries; an earlier update that was interrupted is finished first (when the user
 *       agrees).</li>
 *   <li>The user sees every file the update touches and confirms; the update job then runs, and the row is checked
 *       again.</li>
 * </ol>
 * Every run ends in exactly one {@link Services#release()}: the window keeps its buttons disabled until then.
 * Everything here runs on the FX thread (in tests, the test thread); {@code fx} brings background answers back to it.
 */
public final class ScriptLibraryUpdateFlow {

    static final String TITLE = LibraryUpdatePrompt.TITLE;

    /**
     * One update.
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

        /** Asks which engine to use; empty when the user cancelled. */
        Optional<EngineOption> chooseEngine(OpenProjectFlow.EnginePrompt prompt);

        /** Asks whether to finish an interrupted update first; {@code false} when the user declined. */
        boolean confirmRecovery(String name, String detail);

        /** Asks to go ahead; empty when the user cancelled, else whether files in conflict keep the user's copy. */
        Optional<Boolean> ask(LibraryUpdatePrompt prompt);

        /** Shows a job's progress; {@code cancel} asks it to stop. Returns at once. */
        OpenProjectFlow.JobView showJob(String title, String header, Runnable cancel);

        /** Starts a preview on the window's job thread; returns at once. */
        FileJob<PreviewOutcome> startPreview(ScriptLibraryUpdater.Request request, FileJob.ProgressListener listener);

        /** Starts an update on the window's job thread; returns at once. */
        FileJob<UpdateOutcome> startUpdate(ScriptLibraryUpdater.Request request, String fingerprint, boolean keepMine,
                                           FileJob.ProgressListener listener);

        /** Starts finishing an interrupted update on the window's job thread; returns at once. */
        FileJob<RecoveryOutcome> startRecovery(Path projectFile, String workingDirectory);

        /** The project's library changed (or may have): its row is checked again. */
        void changed(Path projectFile);

        /** The run is over: the window clears its busy state. */
        void release();

        /** Shows an error. */
        void error(String title, String message);

        /**
         * Shows a note in the window.
         *
         * @param reveal a file to offer "Show in folder" for, or {@code null}
         */
        void notice(String message, Path reveal);
    }

    /** What one run has learned so far. */
    private record Context(Request request, ProjectFile project, EngineOption engine, EngineLibrary library) {
        ScriptLibraryUpdater.Request updaterRequest() {
            return new ScriptLibraryUpdater.Request(request.projectFile(), project.workingDirectory(),
                    library.folder(), library.version());
        }

        String name() {
            return request.name();
        }
    }

    private final Services services;
    private final Executor fx;
    private FileJob<?> activeJob;

    /** @param fx runs the background answers on the FX thread */
    public ScriptLibraryUpdateFlow(Services services, Executor fx) {
        this.services = Objects.requireNonNull(services, "services");
        this.fx = Objects.requireNonNull(fx, "fx");
    }

    /** Starts an update; the window has already set its busy state, which this clears through the services. */
    public void update(Request request) {
        Objects.requireNonNull(request, "request");
        guarded(() -> {
            FolderEditor folderEditor = services.folderEditor(request.projectFile());
            if (folderEditor == FolderEditor.THIS_PROJECT) {
                fail("\"" + request.name() + "\" is open in an editor this launcher started. Close the editor, then "
                        + "update its script library.");
                return;
            }
            if (folderEditor == FolderEditor.OTHER_PROJECT) {
                fail("\"" + request.name() + "\" is in a folder an editor of this launcher has open, on another "
                        + "project file. Close that editor, then update the script library.");
                return;
            }
            CompletableFuture<EngineOptions> engines = services.engines();
            CompletableFuture<ProjectFileCheck> file = services.read(request.projectFile());
            CompletableFuture.allOf(engines, file).whenCompleteAsync((ignored, error) ->
                    guarded(() -> checked(request, engines, file)), fx);
        });
    }

    /** The progress window's text for each phase of a preview, an update or a recovery. */
    public static String phaseText(com.vertexforge.launcher.service.JobMonitor.Phase phase) {
        return switch (phase) {
            case SCANNING -> "Looking at the libraries' files";
            case WRITING -> "Backing up the library";
            case VERIFYING -> "Checking";
            case COPYING -> "Building the new library";
            case COMMITTING -> "Putting the new library in place";
        };
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

    private void checked(Request request, CompletableFuture<EngineOptions> engines,
                         CompletableFuture<ProjectFileCheck> file) {
        Throwable enginesError = failure(engines);
        EngineOptions options = enginesError == null ? engines.join() : null;
        if (options == null) {
            fail("Could not check the engines: " + describe(enginesError));
            return;
        }
        Throwable fileError = failure(file);
        ProjectFileCheck check = fileError == null ? file.join() : null;
        if (check == null) {
            check = new ProjectFileCheck.Unreadable("Could not check the project file: " + describe(fileError));
        }
        Optional<String> refusal = refusal(request.name(), check);
        if (refusal.isPresent()) {
            fail(refusal.get());
            return;
        }
        ProjectFile project = ((ProjectFileCheck.Readable) check).file();

        Predicate<EngineOption> usable = engine -> EngineLibrary.of(engine).isPresent()
                && !VersionCompatibility.check(project, engine).blocksOpen();
        String engineId = request.entry() == null ? null : request.entry().engineId();
        EngineOption engine = switch (EngineResolver.resolve(options, engineId)) {
            case LaunchPlan.Ready ready -> ready.engine();
            case LaunchPlan.Choose choose -> {
                List<EngineOption> offered = choose.candidates().stream().filter(usable).toList();
                String problem = choose.problem() + (offered.size() < choose.candidates().size()
                        ? " Engines that cannot open this project or have no script library are not listed." : "");
                yield services.chooseEngine(new OpenProjectFlow.EnginePrompt(
                        "Update the script library of \"" + request.name() + "\" with which engine?", problem.trim(),
                        offered, false, false, usable)).orElse(null);
            }
        };
        if (engine == null) {
            services.release();
            return;
        }
        VersionCompatibility.Verdict verdict = VersionCompatibility.check(project, engine);
        if (verdict.blocksOpen()) {
            fail(verdict.reason() + " Its script library cannot be updated to this engine's.");
            return;
        }
        Optional<EngineLibrary> library = EngineLibrary.of(engine);
        if (library.isEmpty()) {
            fail(engine.label() + " names no script library in its engine.json, so there is nothing to update to.");
            return;
        }
        preview(new Context(request, project, engine, library.get()), null);
    }

    /** Why the project's library cannot be updated at all; empty when it can. */
    static Optional<String> refusal(String name, ProjectFileCheck check) {
        return switch (check) {
            case ProjectFileCheck.Missing missing -> Optional.of(missing.reason() + ".");
            case ProjectFileCheck.Unreadable unreadable ->
                    Optional.of("\"" + name + "\" could not be read: " + unreadable.reason());
            case ProjectFileCheck.Invalid invalid ->
                    Optional.of("\"" + name + "\" cannot be updated: " + invalid.reason());
            case ProjectFileCheck.Readable readable -> readable.lock() instanceof EditorLockState.Held held
                    ? Optional.of("\"" + name + "\" is open in an editor"
                    + (held.pid().isPresent() ? " (process " + held.pid().getAsLong() + ")" : "")
                    + ". Close the editor, then update its script library.")
                    : Optional.empty();
        };
    }

    /** Compares the libraries in the background, then asks. */
    private void preview(Context context, String changed) {
        OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
        FileJob<PreviewOutcome> job = services.startPreview(context.updaterRequest(),
                (phase, done, total) -> {
                    OpenProjectFlow.JobView shown = view[0];
                    if (shown != null) {
                        shown.progress(phase, done, total);
                    }
                });
        view[0] = services.showJob(TITLE, "Comparing the script library of \"" + context.name() + "\"…", job::cancel);
        follow(job, view[0], outcome -> {
            switch (outcome) {
                case PreviewOutcome.Ready ready -> {
                    if (ready.preview().plan().upToDate()) {
                        services.release();
                        services.notice("The script library of \"" + context.name() + "\" is up to date with "
                                + context.engine().label() + ".", null);
                        return;
                    }
                    ask(context, ready.preview(), changed);
                }
                case PreviewOutcome.Interrupted interrupted -> recoverFirst(context, interrupted.detail());
                case PreviewOutcome.Refused refused -> fail(refused.reason());
                case PreviewOutcome.Failed failed -> fail(failed.message());
                case PreviewOutcome.Cancelled ignored -> services.release();
            }
        });
    }

    private void recoverFirst(Context context, String detail) {
        if (!services.confirmRecovery(context.name(), detail)) {
            services.release();
            return;
        }
        FileJob<RecoveryOutcome> job = services.startRecovery(context.request().projectFile(),
                context.project().workingDirectory());
        OpenProjectFlow.JobView view = services.showJob(TITLE, "Finishing the interrupted update of \""
                + context.name() + "\"…", job::cancel);
        follow(job, view, outcome -> {
            services.changed(context.request().projectFile());
            switch (outcome) {
                case RecoveryOutcome.Recovered recovered -> preview(context, recovered.summary());
                case RecoveryOutcome.NothingToDo ignored -> preview(context, null);
                case RecoveryOutcome.NeedsAttention attention -> fail(attention.reason());
                case RecoveryOutcome.Refused refused -> fail(refused.reason());
                case RecoveryOutcome.Failed failed -> fail(failed.message());
            }
        });
    }

    private void ask(Context context, ScriptLibraryUpdater.Preview preview, String changed) {
        Optional<Boolean> answer = services.ask(LibraryUpdatePrompt.of(context.name(), context.engine().label(),
                preview, changed));
        if (answer.isEmpty()) {
            services.release();
            return;
        }
        boolean keepMine = answer.get();
        OpenProjectFlow.JobView[] view = new OpenProjectFlow.JobView[1];
        FileJob<UpdateOutcome> job = services.startUpdate(context.updaterRequest(), preview.fingerprint(), keepMine,
                (phase, done, total) -> {
                    OpenProjectFlow.JobView shown = view[0];
                    if (shown != null) {
                        shown.progress(phase, done, total);
                    }
                });
        view[0] = services.showJob(TITLE, "Updating the script library of \"" + context.name() + "\"…", job::cancel);
        follow(job, view[0], outcome -> {
            services.changed(context.request().projectFile());
            switch (outcome) {
                case UpdateOutcome.Done done -> {
                    services.release();
                    services.notice(doneText(context, done), done.backup());
                }
                case UpdateOutcome.UpToDate ignored -> {
                    services.release();
                    services.notice("The script library of \"" + context.name() + "\" is already up to date.", null);
                }
                case UpdateOutcome.Changed again -> ask(context, again.preview(), "The library or the engine's "
                        + "library changed since the last look; this is what the update would do now.");
                case UpdateOutcome.Cancelled ignored -> {
                    services.release();
                    services.notice("The update of \"" + context.name() + "\"'s script library was cancelled; the "
                            + "library is as it was.", null);
                }
                case UpdateOutcome.Refused refused -> fail(refused.reason());
                case UpdateOutcome.Failed failed -> fail(failed.message());
                case UpdateOutcome.Interrupted interrupted -> fail(interrupted.message());
            }
        });
    }

    /** The notice after an update: what changed, and where the backup is. */
    private static String doneText(Context context, UpdateOutcome.Done done) {
        StringBuilder text = new StringBuilder("The script library of \"" + context.name() + "\" was updated to "
                + context.engine().label() + ".");
        long replaced = done.plan().decisions().stream()
                .filter(decision -> decision.verdict() == FileDecision.Verdict.CONFLICT)
                .count();
        if (replaced > 0) {
            text.append(done.plan().keepMine()
                    ? " " + replaced + " file(s) you changed were kept."
                    : " " + replaced + " file(s) you changed were replaced; your copies are in the backup.");
        }
        if (done.backup() != null) {
            text.append(" Backup: ").append(done.backup()).append('.');
        }
        done.notes().forEach(note -> text.append(' ').append(note));
        return text.toString();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Helpers

    /** Follows a job to its end: closes its window, then hands its outcome to {@code then} on the FX thread. */
    private <T> void follow(FileJob<T> job, OpenProjectFlow.JobView view, Consumer<T> then) {
        activeJob = job;
        job.outcome().whenCompleteAsync((outcome, error) -> guarded(() -> {
            if (activeJob == job) {
                activeJob = null;
            }
            view.close();
            if (error != null || outcome == null) {
                // The outcome never completes exceptionally; this is only a safety net.
                fail("It stopped unexpectedly: " + describe(error));
                return;
            }
            then.accept(outcome);
        }), fx);
    }

    private void fail(String message) {
        services.release();
        services.error(TITLE, message);
    }

    /** Runs one step; a step that throws must not leave the window's busy state set. */
    private void guarded(Runnable step) {
        try {
            step.run();
        } catch (RuntimeException | Error e) {
            services.release();
            throw e;
        }
    }

    /** The future's failure, or {@code null} when it completed normally. The future must be done. */
    private static Throwable failure(CompletableFuture<?> future) {
        try {
            future.join();
            return null;
        } catch (CompletionException | CancellationException e) {
            Throwable current = e;
            while ((current instanceof CompletionException || current instanceof ExecutionException)
                    && current.getCause() != null) {
                current = current.getCause();
            }
            return current;
        }
    }

    private static String describe(Throwable error) {
        if (error == null) {
            return "unknown error";
        }
        return error.getMessage() == null ? error.toString() : error.getMessage();
    }
}
