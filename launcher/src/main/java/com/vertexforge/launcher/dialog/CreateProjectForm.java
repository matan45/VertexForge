package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.service.CreateOutcome;
import com.vertexforge.launcher.service.CreateRequest;
import com.vertexforge.launcher.service.CreationJob;
import com.vertexforge.launcher.service.LocationChecker.LocationCheck;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.service.ProjectNameRules;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ReadOnlyBooleanProperty;
import javafx.beans.property.ReadOnlyBooleanWrapper;
import javafx.beans.property.ReadOnlyDoubleProperty;
import javafx.beans.property.ReadOnlyDoubleWrapper;
import javafx.beans.property.ReadOnlyObjectProperty;
import javafx.beans.property.ReadOnlyObjectWrapper;
import javafx.beans.property.ReadOnlyStringProperty;
import javafx.beans.property.ReadOnlyStringWrapper;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;

/**
 * Everything the Create Project dialog does, without its controls (VK-1679), so it runs in unit tests without the
 * JavaFX toolkit: the properties are plain {@code javafx.beans} ones and the timers are injected.
 * <p>
 * The name is checked on every keystroke. Once it and the location are right as text, the location is checked on disk
 * in the background, 300 ms after the last keystroke; only the latest check's answer is used. Create starts a
 * {@link CreationJob} and the dialog stays open: it shows progress, a failure stays in the dialog to be fixed, and
 * Cancel waits for the creation to clean up (at most {@link #CANCEL_GRACE}; after that the window takes over).
 * <p>
 * Everything here runs on the FX thread (in tests, the test thread); {@code fxThread} brings answers back to it.
 */
public final class CreateProjectForm {

    static final Duration DEBOUNCE = Duration.ofMillis(300);
    static final Duration SLOW_NOTICE = Duration.ofSeconds(3);
    static final Duration CANCEL_GRACE = Duration.ofSeconds(2);
    /** The progress value for "busy, amount unknown" ({@code ProgressIndicator.INDETERMINATE_PROGRESS}). */
    public static final double INDETERMINATE = -1;

    static final String CHECKING = "Checking the location…";
    static final String SLOW = "The location hasn't answered yet. It may be an unreachable network folder.";
    static final String PREPARING = "Preparing…";
    static final String CANCELLING = "Cancelling…";
    static final String NO_LOCATION = "Choose a location for the project.";
    static final String INVALID_LOCATION = "The location is not a valid path.";

    public enum State {
        /** Nothing typed yet. */
        EDITING,
        /** Right as text; the location is being checked on disk. */
        CHECKING,
        /** Create is possible. */
        READY,
        /** Something must be fixed first. */
        INVALID,
        /** A creation is running. */
        CREATING,
        /** A creation was asked to stop and is cleaning up. */
        CANCELLING,
        /** The last creation failed; the message says why. Create tries again; any edit checks again first. */
        FAILED,
        /** The dialog is done; {@link #result()} says how. */
        CLOSED
    }

    /** What the form needs from the rest of the launcher. Both methods return at once. */
    public interface Services {
        CompletableFuture<LocationCheck> checkLocation(Path location, String name);

        CreationJob startCreation(CreateRequest request, CreationJob.ProgressListener listener);
    }

    /** A one-shot timer that runs its action on the FX thread. */
    public interface Timer {
        /** Starts the timer again from zero. */
        void restart();

        void stop();
    }

    @FunctionalInterface
    public interface Timers {
        Timer create(Duration delay, Runnable action);
    }

    /**
     * How the dialog ended.
     *
     * @param job             the creation the window should follow up on: a created project, or one still cleaning
     *                        up after Cancel; {@code null} when there is none
     * @param openAfterCreate whether to open the project once it exists
     */
    public record Result(CreationJob job, boolean openAfterCreate) {
        public static final Result NOTHING = new Result(null, false);
    }

    private final Services services;
    private final Executor fxThread;
    private final boolean editorAvailable;
    private final Timer debounce;
    private final Timer slowNotice;
    private final Timer cancelGrace;

    private final StringProperty name = new SimpleStringProperty(this, "name", "");
    private final StringProperty location = new SimpleStringProperty(this, "location", "");
    private final BooleanProperty openAfterCreate = new SimpleBooleanProperty(this, "openAfterCreate");
    private final ReadOnlyObjectWrapper<State> state = new ReadOnlyObjectWrapper<>(this, "state", State.EDITING);
    private final ReadOnlyStringWrapper error = new ReadOnlyStringWrapper(this, "error", "");
    private final ReadOnlyStringWrapper warnings = new ReadOnlyStringWrapper(this, "warnings", "");
    private final ReadOnlyStringWrapper status = new ReadOnlyStringWrapper(this, "status", "");
    private final ReadOnlyDoubleWrapper progress = new ReadOnlyDoubleWrapper(this, "progress", INDETERMINATE);
    private final ReadOnlyBooleanWrapper inputsEnabled = new ReadOnlyBooleanWrapper(this, "inputsEnabled");
    private final ReadOnlyBooleanWrapper openAfterCreateEnabled =
            new ReadOnlyBooleanWrapper(this, "openAfterCreateEnabled");
    private final ReadOnlyBooleanWrapper createEnabled = new ReadOnlyBooleanWrapper(this, "createEnabled");
    private final ReadOnlyBooleanWrapper cancelEnabled = new ReadOnlyBooleanWrapper(this, "cancelEnabled");
    private final ReadOnlyBooleanWrapper busy = new ReadOnlyBooleanWrapper(this, "busy");

    /** Bumped by every edit, create and close, so that an answer to an older check is ignored. */
    private long generation;
    /** The location of the latest check, as a path; set whenever the text is right. */
    private Path checkedLocation;
    /** The location the latest check found fine, for the folder chooser. */
    private Path readyLocation;
    private CreationJob job;
    private Result result;
    private Consumer<Result> onClosed = closed -> {
    };
    /** The latest {done, total} from the creation thread, waiting for the FX thread; null when none is waiting. */
    private final AtomicReference<int[]> pendingProgress = new AtomicReference<>();

    public CreateProjectForm(Services services, Timers timers, Executor fxThread, boolean editorAvailable) {
        this.services = Objects.requireNonNull(services, "services");
        this.fxThread = Objects.requireNonNull(fxThread, "fxThread");
        Objects.requireNonNull(timers, "timers");
        this.editorAvailable = editorAvailable;
        this.debounce = timers.create(DEBOUNCE, this::startCheck);
        this.slowNotice = timers.create(SLOW_NOTICE, this::showSlowNotice);
        this.cancelGrace = timers.create(CANCEL_GRACE, this::stopWaitingForCancel);

        openAfterCreate.set(editorAvailable);
        name.addListener((observable, was, now) -> inputChanged());
        location.addListener((observable, was, now) -> inputChanged());
        setState(State.EDITING);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Properties
    // -----------------------------------------------------------------------------------------------------------------

    /** The project name exactly as typed. */
    public StringProperty nameProperty() {
        return name;
    }

    /** The location as typed; surrounding spaces are ignored. */
    public StringProperty locationProperty() {
        return location;
    }

    public BooleanProperty openAfterCreateProperty() {
        return openAfterCreate;
    }

    public ReadOnlyObjectProperty<State> stateProperty() {
        return state.getReadOnlyProperty();
    }

    public State getState() {
        return state.get();
    }

    /** Why Create is not possible, or why the last creation failed; empty otherwise. */
    public ReadOnlyStringProperty errorProperty() {
        return error.getReadOnlyProperty();
    }

    /** Things to know about the location (one per line); they never block Create. */
    public ReadOnlyStringProperty warningsProperty() {
        return warnings.getReadOnlyProperty();
    }

    /** What is going on: checking, creating, cancelling. */
    public ReadOnlyStringProperty statusProperty() {
        return status.getReadOnlyProperty();
    }

    /** 0 to 1 while creating, or {@link #INDETERMINATE}. */
    public ReadOnlyDoubleProperty progressProperty() {
        return progress.getReadOnlyProperty();
    }

    public ReadOnlyBooleanProperty inputsEnabledProperty() {
        return inputsEnabled.getReadOnlyProperty();
    }

    /** The checkbox can be used: the inputs are, and an editor was found to open the project with. */
    public ReadOnlyBooleanProperty openAfterCreateEnabledProperty() {
        return openAfterCreateEnabled.getReadOnlyProperty();
    }

    public ReadOnlyBooleanProperty createEnabledProperty() {
        return createEnabled.getReadOnlyProperty();
    }

    public ReadOnlyBooleanProperty cancelEnabledProperty() {
        return cancelEnabled.getReadOnlyProperty();
    }

    /** A creation is running or cleaning up. */
    public ReadOnlyBooleanProperty busyProperty() {
        return busy.getReadOnlyProperty();
    }

    /** The last location found fine, if the form is ready to create there. */
    public Optional<Path> readyLocation() {
        return state.get() == State.READY ? Optional.ofNullable(readyLocation) : Optional.empty();
    }

    /** How the dialog ended; {@code null} until it has. */
    public Result result() {
        return result;
    }

    /** Called once, when the form closes. */
    public void setOnClosed(Consumer<Result> onClosed) {
        this.onClosed = Objects.requireNonNull(onClosed, "onClosed");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Actions
    // -----------------------------------------------------------------------------------------------------------------

    /**
     * Create was pressed: starts the creation when the form is ready, or tries again after a failure (the creator
     * checks everything again itself); does nothing otherwise.
     */
    public void create() {
        if (state.get() != State.READY && state.get() != State.FAILED) {
            return;
        }
        generation++;
        debounce.stop();
        slowNotice.stop();
        error.set("");
        status.set(PREPARING);
        progress.set(INDETERMINATE);
        pendingProgress.set(null);
        setState(State.CREATING);

        CreationJob started;
        try {
            started = services.startCreation(new CreateRequest(name.get(), checkedLocation), this::progressFromWorker);
        } catch (RuntimeException e) {
            fail("Could not start creating the project: " + describe(e));
            return;
        }
        job = started;
        started.outcome().whenComplete((outcome, failure) -> fxThread.execute(() -> finished(started, outcome, failure)));
    }

    /**
     * Cancel or the window's close button: closes the dialog, or, while a creation runs, asks it to stop and waits
     * for it to clean up.
     */
    public void cancel() {
        switch (state.get()) {
            case CREATING -> {
                job.cancel();
                status.set(CANCELLING);
                setState(State.CANCELLING);
                cancelGrace.restart();
            }
            case CANCELLING, CLOSED -> {
                // Already stopping or done.
            }
            default -> close(Result.NOTHING);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Checking
    // -----------------------------------------------------------------------------------------------------------------

    private void inputChanged() {
        State current = state.get();
        if (current == State.CREATING || current == State.CANCELLING || current == State.CLOSED) {
            return;
        }
        generation++;
        debounce.stop();
        slowNotice.stop();
        warnings.set("");
        status.set("");

        Optional<String> problem = problemAsText();
        if (problem.isPresent()) {
            error.set(problem.get());
            setState(State.INVALID);
            return;
        }
        error.set("");
        status.set(CHECKING);
        setState(State.CHECKING);
        debounce.restart();
    }

    /** What is wrong with the name or the location as text; sets {@link #checkedLocation} when nothing is. */
    private Optional<String> problemAsText() {
        Optional<ProjectNameRules.Problem> nameProblem = ProjectNameRules.checkName(name.get());
        if (nameProblem.isPresent()) {
            return Optional.of(nameProblem.get().message());
        }

        String text = location.get() == null ? "" : location.get().trim();
        if (text.isEmpty()) {
            return Optional.of(NO_LOCATION);
        }
        Path path;
        try {
            path = Path.of(text);
        } catch (InvalidPathException e) {
            return Optional.of(INVALID_LOCATION);
        }
        if (!path.isAbsolute()) {
            return Optional.of(ProjectCreator.LOCATION_NOT_ABSOLUTE);
        }
        checkedLocation = path.normalize();
        return Optional.empty();
    }

    private void startCheck() {
        if (state.get() != State.CHECKING) {
            return;
        }
        long asked = generation;
        slowNotice.restart();
        CompletableFuture<LocationCheck> answer;
        try {
            answer = services.checkLocation(checkedLocation, name.get());
        } catch (RuntimeException e) {
            applyCheck(asked, null, e);
            return;
        }
        answer.whenComplete((check, failure) -> fxThread.execute(() -> applyCheck(asked, check, failure)));
    }

    /** Applies a check's answer, unless the inputs changed since it was asked (then {@link #checkedLocation} did too). */
    private void applyCheck(long asked, LocationCheck check, Throwable failure) {
        if (asked != generation || state.get() != State.CHECKING) {
            return;
        }
        slowNotice.stop();
        status.set("");
        if (failure != null || check == null) {
            error.set("Could not check the location: " + describe(failure));
            setState(State.INVALID);
            return;
        }
        if (!check.ok()) {
            error.set(check.error().orElse(""));
            setState(State.INVALID);
            return;
        }
        error.set("");
        warnings.set(String.join("\n", check.warnings()));
        readyLocation = checkedLocation;
        setState(State.READY);
    }

    private void showSlowNotice() {
        if (state.get() == State.CHECKING) {
            status.set(SLOW);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Creating
    // -----------------------------------------------------------------------------------------------------------------

    /** Called on the creation thread; hands only the latest value to the FX thread. */
    private void progressFromWorker(int done, int total) {
        if (pendingProgress.getAndSet(new int[]{done, total}) == null) {
            fxThread.execute(this::applyProgress);
        }
    }

    private void applyProgress() {
        int[] latest = pendingProgress.getAndSet(null);
        State current = state.get();
        if (latest == null || (current != State.CREATING && current != State.CANCELLING)) {
            return;
        }
        int done = latest[0];
        int total = latest[1];
        progress.set(total > 0 ? Math.min(1.0, (double) done / total) : INDETERMINATE);
        if (current == State.CREATING) {
            status.set("Creating files… " + done + " of " + total);
        }
    }

    private void finished(CreationJob finishedJob, CreateOutcome outcome, Throwable failure) {
        if (finishedJob != job || state.get() == State.CLOSED) {
            return;
        }
        cancelGrace.stop();
        boolean cancelled = state.get() == State.CANCELLING;

        if (failure == null && outcome != null && outcome.created()) {
            close(new Result(finishedJob, !cancelled && openAfterCreate.get() && editorAvailable));
            return;
        }
        if (failure == null && outcome != null && outcome.result().cancelled()) {
            close(new Result(finishedJob, false));
            return;
        }

        job = null;
        String message = failure != null || outcome == null
                ? "Could not create the project: " + describe(failure)
                : Optional.ofNullable(outcome.result().errorMessage()).orElse("Could not create the project.");
        fail(message);
    }

    private void fail(String message) {
        progress.set(0);
        status.set("");
        error.set(message);
        setState(State.FAILED);
    }

    /** The creation did not stop within the grace period: close anyway, and let the window wait for it. */
    private void stopWaitingForCancel() {
        if (state.get() == State.CANCELLING) {
            close(new Result(job, false));
        }
    }

    private void close(Result closedWith) {
        generation++;
        debounce.stop();
        slowNotice.stop();
        cancelGrace.stop();
        result = closedWith;
        setState(State.CLOSED);
        onClosed.accept(closedWith);
    }

    private void setState(State next) {
        state.set(next);
        boolean editable = switch (next) {
            case EDITING, CHECKING, READY, INVALID, FAILED -> true;
            case CREATING, CANCELLING, CLOSED -> false;
        };
        inputsEnabled.set(editable);
        openAfterCreateEnabled.set(editable && editorAvailable);
        // FAILED is only ever reached from READY, so its name and location were found fine before.
        createEnabled.set(next == State.READY || next == State.FAILED);
        cancelEnabled.set(next != State.CANCELLING && next != State.CLOSED);
        busy.set(next == State.CREATING || next == State.CANCELLING);
    }

    private static String describe(Throwable failure) {
        Throwable cause = failure;
        while (cause instanceof CompletionException && cause.getCause() != null) {
            cause = cause.getCause();
        }
        if (cause == null) {
            return "unknown error";
        }
        return cause.getMessage() != null ? cause.getMessage() : cause.getClass().getSimpleName();
    }
}
