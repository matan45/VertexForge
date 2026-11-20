package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.CopyNamePrompt;
import com.vertexforge.launcher.dialog.VersionPrompt;
import com.vertexforge.launcher.dialog.VersionPrompt.Choice;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.EditorProjectRules;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.VersionCompatibility;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.service.FileJob;
import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.ProjectBackupService.BackupOutcome;
import com.vertexforge.launcher.service.ProjectBackupService.BackupRequest;
import com.vertexforge.launcher.service.ProjectCopier.CopyOutcome;
import com.vertexforge.launcher.service.ProjectCopier.CopyRequest;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.session.EditorSessions;

import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import java.util.function.Predicate;

/**
 * Opens a project once it is clear that the engine it opens with reads it (VK-1690). Free of the JavaFX toolkit, so it
 * runs in unit tests; the window shows its questions.
 * <ol>
 *   <li>The engines are checked and the project file is read again (with its folder's editor lock), in parallel: the
 *       row's answer may be old, and paths such as the command line never had one.</li>
 *   <li>{@link EngineResolver} picks the engine, or the user does; engines that cannot open the project are not
 *       offered.</li>
 *   <li>{@link VersionCompatibility} compares the project with that engine. Compatible (the same version, or one that
 *       differs only in its patch): it opens. Older: the user chooses Back up and open (the default), Open a copy, Open
 *       anyway or Cancel. Unknown: a warning, shown once per project. Newer, or a project format this engine does not
 *       read: the user picks another engine that can open it.</li>
 * </ol>
 * The backup and the copy run as {@link FileJob}s with a progress window; the editor starts only after a backup was
 * verified and moved into place, so nothing can touch the project before its backup exists.
 * <p>
 * Every open ends in exactly one {@link Services#start} or one {@link Services#release()}: the window keeps Open
 * disabled until then. Everything here runs on the FX thread (in tests, the test thread); {@code fx} brings background
 * answers back to it.
 */
public final class OpenProjectFlow {

    static final String OLDER_CHOICES = "Back up and open first saves a zip of the project beside its folder. "
            + "Open a copy copies the project into a new folder beside it and opens the copy, leaving this one as it "
            + "is. Open anyway opens this project as it is.";
    static final String UNKNOWN_CHOICES = "Open opens it as it is; Back up and open first saves a zip of the project "
            + "beside its folder. This warning is shown once for this project.";

    /**
     * One open.
     *
     * @param projectFile the project file
     * @param entry       its entry, or {@code null} when it is not listed
     * @param name        the name to show
     * @param mcp         its "Open with MCP" setting
     */
    public record Request(Path projectFile, RecentEntry entry, String name, McpSetting mcp) {
        public Request {
            Objects.requireNonNull(projectFile, "projectFile");
            Objects.requireNonNull(name, "name");
            mcp = mcp == null ? McpSetting.DEFAULT : mcp;
        }
    }

    /**
     * Which engine to use instead, or when the resolver cannot decide.
     *
     * @param header            the chooser's header, or {@code null} for its own
     * @param problem           why the launcher asks
     * @param candidates        the engines offered: only ones that can open the project
     * @param canRemember       whether the choice may be written as the project's engine
     * @param rememberByDefault whether "remember" starts ticked
     * @param offered           which engines may be offered after "Add Engine…"
     */
    public record EnginePrompt(String header, String problem, List<EngineOption> candidates, boolean canRemember,
                               boolean rememberByDefault, Predicate<EngineOption> offered) {
        public EnginePrompt {
            Objects.requireNonNull(problem, "problem");
            candidates = List.copyOf(candidates);
            Objects.requireNonNull(offered, "offered");
        }
    }

    /** The engine the user picked, and whether to bind the project to it. */
    public record Picked(EngineOption engine, boolean remember) {
        public Picked {
            Objects.requireNonNull(engine, "engine");
        }
    }

    /** The window that shows a backup or a copy while it runs. */
    public interface JobView {
        /** How far the job got. Any thread. */
        void progress(JobMonitor.Phase phase, long done, long total);

        /** The job has ended. FX thread. */
        void close();
    }

    /** What the flow needs from the window. Only the questions may block (they show dialogs). */
    public interface Services {
        /** Lists and checks the engines; completes on any thread. */
        CompletableFuture<EngineOptions> engines();

        /** Reads the project file, and its folder's editor lock when {@code probeLock}; never exceptionally. */
        CompletableFuture<ProjectFileCheck> read(Path projectFile, boolean probeLock);

        /** Which editor of this window has a project of the file's folder open. */
        FolderEditor folderEditor(Path projectFile);

        /** Whether the list must not be written. */
        boolean readOnly();

        /** Asks which engine to use; empty when the user cancelled. */
        Optional<Picked> chooseEngine(EnginePrompt prompt);

        /** Asks what to do before the project opens; {@link Choice#CANCEL} when the user closed the question. */
        Choice ask(VersionPrompt prompt);

        /** Asks for the copy's folder name; empty when the user cancelled. */
        Optional<String> askCopyName(CopyNamePrompt prompt);

        /** Shows a job's progress; {@code cancel} asks it to stop. Returns at once. */
        JobView showJob(String title, String header, Runnable cancel);

        /** Starts a backup on the window's job thread; returns at once. */
        FileJob<BackupOutcome> startBackup(BackupRequest request, FileJob.ProgressListener listener);

        /** Starts a copy on the window's job thread; returns at once. */
        FileJob<CopyOutcome> startCopy(CopyRequest request, FileJob.ProgressListener listener);

        /** Lists the copy on the store thread, bound to {@code engineId} when given. */
        CompletableFuture<AddedProject> listCopy(Path projectFile, String name, Optional<UUID> engineId);

        /** Shows the list as written and selects the new entry. */
        void listed(AddedProject added);

        /** The list could not be written. */
        void saveFailed(Throwable error);

        /** Records on the store thread that the entry's "unknown version" warning was shown. */
        void rememberWarned(UUID id);

        /**
         * Starts the editor; the window clears its launch state once the process has started.
         *
         * @param id     the entry, or {@code null} when the project is not listed
         * @param bindTo the engine to bind the entry to once the editor is ready
         */
        void start(EngineOption engine, Path projectFile, String name, McpSetting mcp, UUID id,
                   Optional<UUID> bindTo);

        /** Nothing is started: the window clears its launch state. */
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

    /** What one open has learned so far. */
    private record Context(Request request, ProjectFile project, ProjectContent content, EngineOptions options) {
        UUID id() {
            return request.entry() == null ? null : request.entry().id();
        }

        String name() {
            return request.name();
        }
    }

    private final Services services;
    private final Executor fx;
    /** Projects warned about this session, by {@link EditorSessions#key}: the record when the list cannot hold it. */
    private final Set<Path> warned = new HashSet<>();
    private FileJob<?> activeJob;

    /** @param fx runs the background answers on the FX thread */
    public OpenProjectFlow(Services services, Executor fx) {
        this.services = Objects.requireNonNull(services, "services");
        this.fx = Objects.requireNonNull(fx, "fx");
    }

    /** Opens the project; the window has already set its launch state, which this clears through the services. */
    public void open(Request request) {
        Objects.requireNonNull(request, "request");
        guarded(() -> {
            FolderEditor folderEditor = services.folderEditor(request.projectFile());
            CompletableFuture<EngineOptions> engines = services.engines();
            // An editor of this window holds the folder's lock itself; there is nothing to probe then (VK-1686).
            CompletableFuture<ProjectFileCheck> file =
                    services.read(request.projectFile(), folderEditor == FolderEditor.NONE);
            CompletableFuture.allOf(engines, file).whenCompleteAsync((ignored, error) ->
                    guarded(() -> checked(request, folderEditor, engines, file)), fx);
        });
    }

    /** Asks a running backup or copy to stop (Cancel, or the launcher closing). */
    public void cancelJob() {
        FileJob<?> job = activeJob;
        if (job != null) {
            job.cancel();
        }
    }

    /** Whether a backup or a copy is running. */
    public boolean jobRunning() {
        return activeJob != null;
    }

    private void checked(Request request, FolderEditor folderEditor, CompletableFuture<EngineOptions> engines,
                         CompletableFuture<ProjectFileCheck> file) {
        Throwable enginesError = failure(engines);
        EngineOptions options = enginesError == null ? engines.join() : null;
        if (options == null) {
            fail("Launch Failed", "Could not check the engines: " + describe(enginesError));
            return;
        }
        Throwable fileError = failure(file);
        ProjectFileCheck check = fileError == null ? file.join() : null;
        if (check == null) {
            check = new ProjectFileCheck.Unreadable("Could not check the project file: " + describe(fileError));
        }
        Optional<String> refusal = refusal(request.name(), check, folderEditor);
        if (refusal.isPresent()) {
            fail("Open Project", refusal.get());
            return;
        }

        ProjectFile project = ((ProjectFileCheck.Readable) check).file();
        Context context = new Context(request, project,
                ProjectContent.of(request.projectFile(), project.workingDirectory()), options);
        String engineId = request.entry() == null ? null : request.entry().engineId();
        switch (EngineResolver.resolve(options, engineId)) {
            case LaunchPlan.Ready ready -> decide(context, ready.engine(), ready.bindTo(), false);
            case LaunchPlan.Choose choose -> chooseFirst(context, choose);
        }
    }

    /**
     * Why the project cannot be opened at all, whatever the engine: what the row would show for it (VK-1686). Member
     * problems are judged later, once the engine is known: a project from a newer engine may define other members.
     */
    static Optional<String> refusal(String name, ProjectFileCheck check, FolderEditor folderEditor) {
        return switch (check) {
            case ProjectFileCheck.Missing missing -> Optional.of(missing.reason() + ".");
            case ProjectFileCheck.Unreadable unreadable ->
                    Optional.of("\"" + name + "\" could not be read: " + unreadable.reason());
            case ProjectFileCheck.Invalid invalid -> Optional.of("\"" + name + "\" cannot be opened: " + invalid.reason());
            case ProjectFileCheck.Readable readable -> {
                if (readable.file().schema().isEmpty()) {
                    yield Optional.of("\"" + name + "\" cannot be opened: "
                            + EditorProjectRules.schemaFormatProblem(readable.file().schemaVersion()));
                }
                if (folderEditor == FolderEditor.OTHER_PROJECT) {
                    yield Optional.of("\"" + name + "\" is in a folder an editor of this launcher has open, on another"
                            + " project file. Open is available again once that editor has closed it.");
                }
                if (folderEditor == FolderEditor.NONE && readable.lock() instanceof EditorLockState.Held held) {
                    yield Optional.of("\"" + name + "\" is already open in another editor"
                            + (held.pid().isPresent() ? " (pid " + held.pid().getAsLong() + ")" : "")
                            + ". Open is available again once that editor has closed it.");
                }
                yield Optional.empty();
            }
        };
    }

    /** The resolver could not decide: the user picks among the working engines that can open the project. */
    private void chooseFirst(Context context, LaunchPlan.Choose choose) {
        Predicate<EngineOption> opens = engine -> !blocks(context, engine);
        List<EngineOption> offered = choose.candidates().stream().filter(opens).toList();
        StringBuilder problem = new StringBuilder(choose.problem());
        if (offered.size() < choose.candidates().size()) {
            EngineOption blocked = choose.candidates().stream().filter(opens.negate()).findFirst().orElseThrow();
            problem.append(problem.isEmpty() ? "" : " ")
                    .append(offered.isEmpty() ? "None of the working engines can open this project. "
                            + VersionCompatibility.check(context.project(), blocked).reason()
                            : "Engines that cannot open this project are not listed.");
        }
        Optional<Picked> pick = services.chooseEngine(new EnginePrompt(null, problem.toString(), offered,
                canRemember(context), choose.rememberByDefault(), opens));
        if (pick.isEmpty()) {
            services.release();
            return;
        }
        decide(context, pick.get().engine(), boundTo(pick.get()), true);
    }

    /**
     * Opens the project with {@code engine}, or asks first.
     *
     * @param bindTo the engine to bind the entry to once the editor is ready
     * @param picked whether the user picked the engine (it was offered only because it can open the project)
     */
    private void decide(Context context, EngineOption engine, Optional<UUID> bindTo, boolean picked) {
        VersionCompatibility.Verdict verdict = VersionCompatibility.check(context.project(), engine);
        if (verdict.blocksOpen()) {
            if (picked) {
                // Only engines that can open the project are offered; this is a safety net.
                fail("Open Project", verdict.reason());
            } else {
                chooseInstead(context, engine, verdict);
            }
            return;
        }
        if (context.project().structureProblem() != null) {
            fail("Open Project", "\"" + context.name() + "\" cannot be opened: " + context.project().structureProblem());
            return;
        }
        switch (verdict) {
            case VersionCompatibility.Compatible compatible -> start(context, engine, bindTo);
            case VersionCompatibility.Older older -> askOlder(context, engine, bindTo, older);
            case VersionCompatibility.Unknown unknown -> {
                if (isWarned(context)) {
                    start(context, engine, bindTo);
                } else {
                    askUnknown(context, engine, bindTo, unknown);
                }
            }
            case VersionCompatibility.Newer newer -> throw new IllegalStateException("blocks: " + newer);
            case VersionCompatibility.Unsupported unsupported -> throw new IllegalStateException("blocks: " + unsupported);
        }
    }

    /**
     * The engine cannot open the project: the dialog names what it needs and lists the registered engines that can
     * open it (none: the user can add one there).
     */
    private void chooseInstead(Context context, EngineOption engine, VersionCompatibility.Verdict verdict) {
        List<EngineOption> others = VersionCompatibility.openable(context.project(), context.options()).stream()
                .filter(other -> !other.equals(engine))
                .toList();
        String header = verdict instanceof VersionCompatibility.Newer newer
                ? "\"" + context.name() + "\" needs " + newer.required()
                : engine.versionText() + " cannot open \"" + context.name() + "\"";
        String problem = verdict.reason() + (others.isEmpty()
                ? " No engine this launcher knows can open it: add one with Add Engine…."
                : " Open it with one of these engines instead.");
        Optional<Picked> pick = services.chooseEngine(new EnginePrompt(header, problem, others, canRemember(context),
                true, other -> !blocks(context, other)));
        if (pick.isEmpty()) {
            services.release();
            return;
        }
        decide(context, pick.get().engine(), boundTo(pick.get()), true);
    }

    private void askOlder(Context context, EngineOption engine, Optional<UUID> bindTo,
                          VersionCompatibility.Older older) {
        String unavailable = context.content().unavailableReason().orElse(null);
        VersionPrompt prompt = new VersionPrompt("Open Project",
                "Open \"" + context.name() + "\" with " + engine.versionText() + "?",
                older.reason() + "\n\n" + OLDER_CHOICES,
                List.of(Choice.BACK_UP_AND_OPEN, Choice.OPEN_COPY, Choice.OPEN_ANYWAY, Choice.CANCEL),
                // One editor save is enough to upgrade a project: the default backs up first, or does nothing.
                unavailable == null ? Choice.BACK_UP_AND_OPEN : Choice.CANCEL,
                unavailable);
        answer(context, engine, bindTo, prompt, false);
    }

    private void askUnknown(Context context, EngineOption engine, Optional<UUID> bindTo,
                            VersionCompatibility.Unknown unknown) {
        String unavailable = context.content().unavailableReason().orElse(null);
        VersionPrompt prompt = new VersionPrompt("Open Project",
                "The launcher cannot tell whether " + engine.versionText() + " reads \"" + context.name() + "\"",
                unknown.reason() + "\n\n" + UNKNOWN_CHOICES,
                List.of(Choice.OPEN, Choice.BACK_UP_AND_OPEN, Choice.CANCEL),
                Choice.OPEN,
                unavailable);
        answer(context, engine, bindTo, prompt, true);
    }

    /** @param warning whether this is the once-per-project warning (going ahead records that it was shown) */
    private void answer(Context context, EngineOption engine, Optional<UUID> bindTo, VersionPrompt prompt,
                        boolean warning) {
        Choice choice = services.ask(prompt);
        if (choice == null || !prompt.enabled(choice)) {
            choice = Choice.CANCEL;
        }
        if (warning && choice != Choice.CANCEL) {
            markWarned(context);
        }
        switch (choice) {
            case BACK_UP_AND_OPEN -> backUp(context, engine, bindTo);
            case OPEN_COPY -> askCopyName(context, engine, suggestedCopyName(context, engine), null);
            case OPEN_ANYWAY, OPEN -> start(context, engine, bindTo);
            case CANCEL -> services.release();
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Back up and open
    // ---------------------------------------------------------------------------------------------------------------

    private void backUp(Context context, EngineOption engine, Optional<UUID> bindTo) {
        JobView view = services.showJob("Back Up Project",
                "Backing up \"" + context.name() + "\" before it opens with " + engine.versionText(), this::cancelJob);
        FileJob<BackupOutcome> job;
        try {
            job = services.startBackup(new BackupRequest(context.request().projectFile(),
                    context.project().workingDirectory(),
                    VersionCompatibility.savedVersion(context.project()).orElse(null)), view::progress);
        } catch (RuntimeException e) {
            view.close();
            throw e;
        }
        follow(job, view, "Back Up Project", outcome -> backedUp(context, engine, bindTo, outcome));
    }

    private void backedUp(Context context, EngineOption engine, Optional<UUID> bindTo, BackupOutcome outcome) {
        switch (outcome) {
            case BackupOutcome.Done done -> {
                services.notice("Backed up \"" + context.name() + "\" to " + done.zip() + "." + notes(done.notes()),
                        done.zip());
                start(context, engine, bindTo);
            }
            case BackupOutcome.Refused refused ->
                    fail("Back Up Project", refused.reason() + " The project was not opened.");
            case BackupOutcome.Cancelled cancelled -> {
                services.release();
                services.notice("The backup of \"" + context.name() + "\" was cancelled, so it was not opened.", null);
            }
            case BackupOutcome.Failed failed ->
                    fail("Back Up Project", "The backup failed: " + failed.message() + " The project was not opened.");
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Open a copy
    // ---------------------------------------------------------------------------------------------------------------

    /** {@code Game-1.1.0}: the project folder's name and the engine's version. */
    static String suggestedCopyName(String folderName, EngineManifest manifest) {
        return folderName + "-" + (manifest != null ? manifest.engineVersion() : "copy");
    }

    private static String suggestedCopyName(Context context, EngineOption engine) {
        Path folderName = context.content().folder().getFileName();
        return suggestedCopyName(folderName == null ? context.name() : folderName.toString(),
                engine.health().manifest());
    }

    /** @param problem why the copy refused the last name, or {@code null} */
    private void askCopyName(Context context, EngineOption engine, String suggested, String problem) {
        Path location = context.content().folder().getParent();
        if (location == null) {
            // The prompt only offers a copy when the folder has a parent; this is a safety net.
            fail("Open a Copy", context.content().unavailableReason().orElse("The project folder has no parent."));
            return;
        }
        Optional<String> name = services.askCopyName(new CopyNamePrompt(context.name(), location, suggested, problem));
        if (name.isEmpty()) {
            services.release();
            return;
        }
        copy(context, engine, name.get());
    }

    private void copy(Context context, EngineOption engine, String newName) {
        JobView view = services.showJob("Open a Copy",
                "Copying \"" + context.name() + "\" to \"" + newName + "\"", this::cancelJob);
        FileJob<CopyOutcome> job;
        try {
            job = services.startCopy(new CopyRequest(context.request().projectFile(),
                    context.project().workingDirectory(), newName), view::progress);
        } catch (RuntimeException e) {
            view.close();
            throw e;
        }
        follow(job, view, "Open a Copy", outcome -> copied(context, engine, newName, outcome));
    }

    private void copied(Context context, EngineOption engine, String newName, CopyOutcome outcome) {
        switch (outcome) {
            case CopyOutcome.Done done -> listCopy(context, engine, newName, done);
            case CopyOutcome.Refused refused -> askCopyName(context, engine, newName, refused.reason());
            case CopyOutcome.Cancelled cancelled -> {
                services.release();
                services.notice("Copying \"" + context.name() + "\" was cancelled; nothing was opened.", null);
            }
            case CopyOutcome.Failed failed -> fail("Open a Copy", "The copy failed: " + failed.message()
                    + " Nothing was opened, and \"" + context.name() + "\" is unchanged.");
        }
    }

    /** Lists the copy, bound to the engine it opens with, and opens it; the original's entry is never touched. */
    private void listCopy(Context context, EngineOption engine, String newName, CopyOutcome.Done done) {
        String note = "Opened a copy of \"" + context.name() + "\" in " + done.folder() + "; the original is unchanged."
                + notes(done.notes());
        if (services.readOnly()) {
            services.notice(note + " The recent-projects list is read-only here, so the copy was not added to it.",
                    done.projectFile());
            services.start(engine, done.projectFile(), newName, McpSetting.DEFAULT, null, Optional.empty());
            return;
        }
        CompletableFuture<AddedProject> listing;
        try {
            listing = services.listCopy(done.projectFile(), newName, engine.id());
        } catch (RuntimeException e) {
            listing = CompletableFuture.failedFuture(e);
        }
        listing.whenCompleteAsync((added, error) -> guarded(() -> {
            if (error != null || added == null) {
                services.saveFailed(unwrap(error));
                services.notice(note + " It could not be added to the list.", done.projectFile());
                services.start(engine, done.projectFile(), newName, McpSetting.DEFAULT, null, Optional.empty());
                return;
            }
            services.listed(added);
            services.notice(note, done.projectFile());
            Path listedPath = added.entry().path() != null ? added.entry().path() : done.projectFile();
            // Bound in the same write as the add; the first open binds nothing more.
            services.start(engine, listedPath, newName, McpSetting.DEFAULT, added.entry().id(), Optional.empty());
        }), fx);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------------------------------------------

    /** Follows a job to its end: closes its window, then hands its outcome to {@code then} on the FX thread. */
    private <T> void follow(FileJob<T> job, JobView view, String title, Consumer<T> then) {
        activeJob = job;
        job.outcome().whenCompleteAsync((outcome, error) -> guarded(() -> {
            if (activeJob == job) {
                activeJob = null;
            }
            view.close();
            if (error != null || outcome == null) {
                // The outcome never completes exceptionally; this is only a safety net.
                fail(title, "It stopped unexpectedly: " + describe(error) + " Nothing was opened.");
                return;
            }
            then.accept(outcome);
        }), fx);
    }

    private void start(Context context, EngineOption engine, Optional<UUID> bindTo) {
        services.start(engine, context.request().projectFile(), context.name(), context.request().mcp(), context.id(),
                bindTo);
    }

    private void fail(String title, String message) {
        services.release();
        services.error(title, message);
    }

    private boolean canRemember(Context context) {
        return context.id() != null && !services.readOnly();
    }

    private static Optional<UUID> boundTo(Picked pick) {
        return pick.remember() ? pick.engine().id() : Optional.empty();
    }

    private static boolean blocks(Context context, EngineOption engine) {
        return VersionCompatibility.check(context.project(), engine).blocksOpen();
    }

    private boolean isWarned(Context context) {
        RecentEntry entry = context.request().entry();
        return (entry != null && entry.unknownVersionWarned())
                || warned.contains(EditorSessions.key(context.request().projectFile()));
    }

    private void markWarned(Context context) {
        warned.add(EditorSessions.key(context.request().projectFile()));
        RecentEntry entry = context.request().entry();
        if (entry != null && !entry.unknownVersionWarned() && !services.readOnly()) {
            services.rememberWarned(entry.id());
        }
    }

    private static String notes(List<String> notes) {
        return notes == null || notes.isEmpty() ? "" : " Not included: " + String.join("; ", notes) + ".";
    }

    /** Runs one step; a step that throws must not leave the window's launch state set. */
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
        Throwable cause = unwrap(error);
        return cause.getMessage() != null ? cause.getMessage() : cause.getClass().getSimpleName();
    }
}
