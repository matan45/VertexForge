package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.service.CreateOutcome;
import com.vertexforge.launcher.service.CreateRequest;
import com.vertexforge.launcher.service.CreationJob;
import com.vertexforge.launcher.service.LocationChecker.LocationCheck;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.service.ProjectNameRules;
import com.vertexforge.launcher.template.ProjectBlueprint;
import com.vertexforge.launcher.template.TemplateCatalog;
import com.vertexforge.launcher.template.TemplateCatalogs;
import com.vertexforge.launcher.template.TemplateException;
import com.vertexforge.launcher.template.TemplateManifest;
import com.vertexforge.launcher.template.TemplateOptionKind;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.ReadOnlyBooleanProperty;
import javafx.beans.property.ReadOnlyBooleanWrapper;
import javafx.beans.property.ReadOnlyDoubleProperty;
import javafx.beans.property.ReadOnlyDoubleWrapper;
import javafx.beans.property.ReadOnlyObjectProperty;
import javafx.beans.property.ReadOnlyObjectWrapper;
import javafx.beans.property.ReadOnlyStringProperty;
import javafx.beans.property.ReadOnlyStringWrapper;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.time.Duration;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;

/**
 * Everything the Create Project dialog does, without its controls (VK-1679), so it runs in unit tests without the
 * JavaFX toolkit: the properties are plain {@code javafx.beans} ones and the timers are injected.
 * <p>
 * The dialog first reads the engines and offers those that can create projects (VK-1689), preselecting the one a new
 * project would open with; choosing an engine reads its templates, choosing a template shows its options. The name is
 * checked on every keystroke. Once it and the location are right as text and the engine's content is read, the
 * location is checked on disk in the background, 300 ms after the last change; only the latest check's answer is
 * used. Create starts a {@link CreationJob} and the dialog stays open: it shows progress, a failure stays in the dialog
 * to be fixed, and Cancel waits for the creation to clean up (at most {@link #CANCEL_GRACE}; after that the window
 * takes over).
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
    static final String READING_ENGINES = "Checking the engines…";
    static final String READING_TEMPLATES = "Reading the engine's templates…";
    static final String CHOOSE_ENGINE = "Choose the engine to create the project with.";
    static final String CHOOSE_TEMPLATE = "Choose a template.";
    static final String ENGINE_PROMPT = "Choose an engine";
    static final String NO_ENGINE_PROMPT = "No engine can create projects";
    static final String TEMPLATE_PROMPT = "Choose a template";
    static final String NO_TEMPLATE_PROMPT = "No templates";

    public enum State {
        /** Nothing typed yet. */
        EDITING,
        /** Right as text; the engines or the chosen engine's templates are still being read. */
        LOADING,
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

    /** What the form needs from the rest of the launcher. Every method returns at once. */
    public interface Services {
        /** The engines, freshly checked. */
        CompletableFuture<EngineOptions> engines();

        /** What {@code engine} offers for new projects; never completes exceptionally. */
        CompletableFuture<TemplateCatalogs.Answer> templates(EngineContent engine);

        CompletableFuture<LocationCheck> checkLocation(Path location, String name, ProjectBlueprint blueprint);

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
    private final Timer debounce;
    private final Timer slowNotice;
    private final Timer cancelGrace;

    private final StringProperty name = new SimpleStringProperty(this, "name", "");
    private final StringProperty location = new SimpleStringProperty(this, "location", "");
    private final BooleanProperty openAfterCreate = new SimpleBooleanProperty(this, "openAfterCreate", true);
    private final ObservableList<EngineChoice> engines = FXCollections.observableArrayList();
    private final ObjectProperty<EngineChoice> engine = new SimpleObjectProperty<>(this, "engine");
    private final ObservableList<TemplateCatalog.Template> templates = FXCollections.observableArrayList();
    private final ObjectProperty<TemplateCatalog.Template> template = new SimpleObjectProperty<>(this, "template");
    private final ObservableList<OptionChoice> options = FXCollections.observableArrayList();
    // One read-only view of each list, kept here: a view follows its list through a weak listener, so a view made
    // per call could be collected together with every listener the dialog added to it.
    private final ObservableList<EngineChoice> enginesView = FXCollections.unmodifiableObservableList(engines);
    private final ObservableList<TemplateCatalog.Template> templatesView =
            FXCollections.unmodifiableObservableList(templates);
    private final ObservableList<OptionChoice> optionsView = FXCollections.unmodifiableObservableList(options);
    private final ReadOnlyStringWrapper enginePrompt = new ReadOnlyStringWrapper(this, "enginePrompt", READING_ENGINES);
    private final ReadOnlyStringWrapper templatePrompt = new ReadOnlyStringWrapper(this, "templatePrompt", "");
    private final ReadOnlyStringWrapper templateDescription = new ReadOnlyStringWrapper(this, "templateDescription", "");
    private final ReadOnlyStringWrapper note = new ReadOnlyStringWrapper(this, "note", "");
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
    /** The files of the latest check; set whenever the engine's content allows a project. */
    private ProjectBlueprint checkedBlueprint;
    /** The location the latest check found fine, for the folder chooser. */
    private Path readyLocation;
    private CreationJob job;
    private Result result;
    private Consumer<Result> onClosed = closed -> {
    };
    /** The latest {done, total} from the creation thread, waiting for the FX thread; null when none is waiting. */
    private final AtomicReference<int[]> pendingProgress = new AtomicReference<>();

    /** The engines as last checked; {@code null} until they are. */
    private EngineOptions engineOptions;
    private boolean enginesLoading = true;
    /** Why no engine can be offered; {@code null} when one can. */
    private String engineProblem;
    /** Bumped by every engine change, so that the templates of an engine no longer chosen are ignored. */
    private long contentGeneration;
    private boolean templatesLoading;
    /** The chosen engine's templates as read; {@code null} while none are. */
    private TemplateCatalog catalog;
    /** Why the chosen engine's templates cannot be used; {@code null} when they can. */
    private String templatesProblem;

    public CreateProjectForm(Services services, Timers timers, Executor fxThread) {
        this.services = Objects.requireNonNull(services, "services");
        this.fxThread = Objects.requireNonNull(fxThread, "fxThread");
        Objects.requireNonNull(timers, "timers");
        this.debounce = timers.create(DEBOUNCE, this::startCheck);
        this.slowNotice = timers.create(SLOW_NOTICE, this::showSlowNotice);
        this.cancelGrace = timers.create(CANCEL_GRACE, this::stopWaitingForCancel);

        name.addListener((observable, was, now) -> inputChanged());
        location.addListener((observable, was, now) -> inputChanged());
        engine.addListener((observable, was, now) -> engineChanged());
        template.addListener((observable, was, now) -> templateChanged());
        setState(State.EDITING);
        loadEngines();
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

    /** The engines projects can be created from, as last checked. Read-only for the dialog. */
    public ObservableList<EngineChoice> engines() {
        return enginesView;
    }

    /** The engine the project is created from; {@code null} while none is chosen. */
    public ObjectProperty<EngineChoice> engineProperty() {
        return engine;
    }

    /** The chosen engine's usable templates. Read-only for the dialog. */
    public ObservableList<TemplateCatalog.Template> templates() {
        return templatesView;
    }

    /** The template the project is created from; {@code null} while none is chosen. */
    public ObjectProperty<TemplateCatalog.Template> templateProperty() {
        return template;
    }

    /** The chosen template's options this launcher can add, in the template's order. Read-only for the dialog. */
    public ObservableList<OptionChoice> options() {
        return optionsView;
    }

    /** What the engine picker shows while no engine is chosen. */
    public ReadOnlyStringProperty enginePromptProperty() {
        return enginePrompt.getReadOnlyProperty();
    }

    /** What the template picker shows while no template is chosen. */
    public ReadOnlyStringProperty templatePromptProperty() {
        return templatePrompt.getReadOnlyProperty();
    }

    public ReadOnlyStringProperty templateDescriptionProperty() {
        return templateDescription.getReadOnlyProperty();
    }

    /** Something to know about the chosen engine: an active override, or an engine the project can't be bound to. */
    public ReadOnlyStringProperty noteProperty() {
        return note.getReadOnlyProperty();
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

    /** What is going on: reading the engines, checking, creating, cancelling. */
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

    /** The checkbox can be used: the inputs are, and an engine is chosen to open the project with. */
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
        EngineChoice chosenEngine = engine.get();
        TemplateCatalog.Template chosenTemplate = template.get();
        if (chosenEngine == null || chosenTemplate == null) {
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
            started = services.startCreation(new CreateRequest(name.get(), checkedLocation, chosenEngine.content(),
                    chosenTemplate.id(), chosenOptionIds()), this::progressFromWorker);
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
    // Engines, templates and options (VK-1689)
    // -----------------------------------------------------------------------------------------------------------------

    private void loadEngines() {
        CompletableFuture<EngineOptions> answer;
        try {
            answer = services.engines();
        } catch (RuntimeException e) {
            applyEngines(null, e);
            return;
        }
        answer.whenComplete((checked, failure) -> fxThread.execute(() -> applyEngines(checked, failure)));
    }

    private void applyEngines(EngineOptions checked, Throwable failure) {
        if (state.get() == State.CLOSED) {
            return;
        }
        enginesLoading = false;
        if (failure != null || checked == null) {
            engineProblem = "The engines could not be checked: " + describe(failure);
            enginePrompt.set(NO_ENGINE_PROMPT);
            refresh();
            return;
        }
        engineOptions = checked;
        List<EngineChoice> choices = EngineChoice.from(checked);
        engineProblem = choices.isEmpty() ? EngineChoice.noChoiceReason(checked) : null;
        enginePrompt.set(choices.isEmpty() ? NO_ENGINE_PROMPT : ENGINE_PROMPT);
        engines.setAll(choices);
        EngineChoice first = EngineChoice.preselect(checked, choices).orElse(null);
        if (Objects.equals(engine.get(), first)) {
            engineChanged();
        } else {
            engine.set(first);
        }
    }

    /** A new engine is chosen (or none): forget the last one's templates and read the new one's. */
    private void engineChanged() {
        if (isFinal()) {
            return;
        }
        contentGeneration++;
        catalog = null;
        templatesProblem = null;
        templatesLoading = false;
        templates.clear();
        template.set(null);
        options.clear();
        updateNote();

        EngineChoice chosen = engine.get();
        if (chosen == null) {
            templatePrompt.set("");
            refresh();
            return;
        }
        templatesLoading = true;
        templatePrompt.set(READING_TEMPLATES);
        long asked = contentGeneration;
        refresh();

        CompletableFuture<TemplateCatalogs.Answer> answer;
        try {
            answer = services.templates(chosen.content());
        } catch (RuntimeException e) {
            applyTemplates(asked, null, e);
            return;
        }
        answer.whenComplete((read, failure) -> fxThread.execute(() -> applyTemplates(asked, read, failure)));
    }

    private void applyTemplates(long asked, TemplateCatalogs.Answer read, Throwable failure) {
        if (asked != contentGeneration || isFinal()) {
            return;
        }
        templatesLoading = false;
        if (failure != null || read == null) {
            templatesProblem = "The engine's templates could not be read: " + describe(failure);
        } else if (read.problem() != null) {
            templatesProblem = read.problem();
        } else {
            catalog = read.catalog();
            templatesProblem = catalog.templates().isEmpty() ? noTemplatesReason(catalog) : null;
        }
        List<TemplateCatalog.Template> usable = catalog == null ? List.of() : catalog.templates();
        templatePrompt.set(usable.isEmpty() ? NO_TEMPLATE_PROMPT : TEMPLATE_PROMPT);
        templates.setAll(usable);
        TemplateCatalog.Template first = usable.stream()
                .filter(candidate -> candidate.id().equals(TemplateCatalog.DEFAULT_TEMPLATE))
                .findFirst()
                .orElse(usable.isEmpty() ? null : usable.getFirst());
        if (Objects.equals(template.get(), first)) {
            templateChanged();
        } else {
            template.set(first);
        }
    }

    /** A new template is chosen (or none): offer its options, each ticked as the template says when it is available. */
    private void templateChanged() {
        if (isFinal()) {
            return;
        }
        options.clear();
        TemplateCatalog.Template chosen = template.get();
        templateDescription.set(chosen == null ? "" : chosen.manifest().description());
        if (chosen != null && catalog != null) {
            for (TemplateManifest.Option option : chosen.manifest().options()) {
                Optional<TemplateOptionKind> kind = TemplateOptionKind.forId(option.id());
                if (kind.isEmpty()) {
                    // An option this launcher cannot add.
                    continue;
                }
                OptionChoice choice = new OptionChoice(option, catalog.unavailableReason(kind.get()).orElse(null));
                choice.selectedProperty().addListener((observable, was, now) -> refresh());
                options.add(choice);
            }
        }
        refresh();
    }

    private void updateNote() {
        EngineChoice chosen = engine.get();
        Optional<EngineOption> override = engineOptions == null ? Optional.empty() : engineOptions.activeOverride();
        if (chosen == null) {
            note.set("");
        } else if (chosen.content().override()) {
            note.set(EditorOverride.ENV_VAR + " is set: the project opens with this editor while it is, and is not"
                    + " bound to an engine.");
        } else if (override.isPresent()) {
            note.set(EditorOverride.ENV_VAR + " is set: the project opens with " + override.get().pathText()
                    + " until it is unset.");
        } else if (chosen.content().engineId().isEmpty()) {
            note.set("This engine is not in the engine list, so the project is not bound to it: it opens with the"
                    + " default engine.");
        } else {
            note.set("");
        }
    }

    private static String noTemplatesReason(TemplateCatalog read) {
        String text = read.engine().label() + " has no project template this launcher can use.";
        return read.notes().isEmpty() ? text : text + " " + read.notes().getFirst();
    }

    /** The ids of the ticked options that can be added, in the template's order. */
    private Set<String> chosenOptionIds() {
        Set<String> ids = new LinkedHashSet<>();
        for (OptionChoice choice : options) {
            if (choice.chosen()) {
                ids.add(choice.id());
            }
        }
        return ids;
    }

    /** The engine's content changed: check again, unless nothing has been typed yet. */
    private void refresh() {
        openAfterCreateEnabled.set(inputsEnabled.get() && engine.get() != null);
        if (state.get() != State.EDITING) {
            inputChanged();
        }
    }

    private boolean isFinal() {
        State current = state.get();
        return current == State.CLOSED;
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
        if (enginesLoading || templatesLoading) {
            error.set("");
            status.set(enginesLoading ? READING_ENGINES : READING_TEMPLATES);
            setState(State.LOADING);
            return;
        }
        Optional<String> contentProblem = problemWithContent();
        if (contentProblem.isPresent()) {
            error.set(contentProblem.get());
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

    /** What keeps the chosen engine, template and options from making a project; sets {@link #checkedBlueprint}. */
    private Optional<String> problemWithContent() {
        if (engineProblem != null) {
            return Optional.of(engineProblem);
        }
        if (engine.get() == null) {
            return Optional.of(CHOOSE_ENGINE);
        }
        if (templatesProblem != null) {
            return Optional.of(templatesProblem);
        }
        TemplateCatalog.Template chosen = template.get();
        if (chosen == null || catalog == null) {
            return Optional.of(CHOOSE_TEMPLATE);
        }
        try {
            checkedBlueprint = catalog.blueprint(chosen.id(), chosenOptionIds());
        } catch (TemplateException e) {
            return Optional.of(e.getMessage());
        }
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
            answer = services.checkLocation(checkedLocation, name.get(), checkedBlueprint);
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
            close(new Result(finishedJob, !cancelled && openAfterCreate.get()));
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
        contentGeneration++;
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
            case EDITING, LOADING, CHECKING, READY, INVALID, FAILED -> true;
            case CREATING, CANCELLING, CLOSED -> false;
        };
        inputsEnabled.set(editable);
        openAfterCreateEnabled.set(editable && engine.get() != null);
        // FAILED is only ever reached from READY, so its inputs were found fine before.
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
