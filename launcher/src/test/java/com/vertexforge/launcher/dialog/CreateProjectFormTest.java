package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.TickingClock;
import com.vertexforge.launcher.dialog.CreateProjectForm.Result;
import com.vertexforge.launcher.dialog.CreateProjectForm.State;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.engine.PickedEngine;
import com.vertexforge.launcher.env.ProcessStarter;
import com.vertexforge.launcher.service.CreateOutcome;
import com.vertexforge.launcher.service.CreateRequest;
import com.vertexforge.launcher.service.CreationJob;
import com.vertexforge.launcher.service.LocationChecker.LocationCheck;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.service.ProjectNameRules;
import com.vertexforge.launcher.template.ProjectBlueprint;
import com.vertexforge.launcher.template.TemplateCatalogs;
import com.vertexforge.launcher.template.TemplateScanner;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The dialog's behaviour without the JavaFX toolkit: timers, checks and threads are all driven by the test. */
class CreateProjectFormTest {

    @TempDir
    Path tempDir;

    private Path projectsDir;
    private ProjectHub hub;
    private final QueueExecutor creation = new QueueExecutor();
    private final QueueExecutor store = new QueueExecutor();
    private final ManualTimers timers = new ManualTimers();
    private final List<PendingCheck> checks = new ArrayList<>();
    private final List<Result> closedWith = new ArrayList<>();
    /** The engines as the real hub checked them: one registered dev engine with content (VK-1689). */
    private EngineOptions engineOptions;
    /** What {@code engines()} answers; a test may swap in a pending or empty answer before building a form. */
    private CompletableFuture<EngineOptions> enginesAnswer;
    /** When set, {@code templates()} answers with this instead of reading the engine. */
    private CompletableFuture<TemplateCatalogs.Answer> templatesAnswer;
    /** When set, every {@code templates()} call gets a pending answer of its own, kept here. */
    private boolean pendingTemplates;
    private final List<CompletableFuture<TemplateCatalogs.Answer>> pendingTemplateAnswers = new ArrayList<>();
    private final List<EngineContent> templateReads = new ArrayList<>();
    private final List<CreateRequest> requests = new ArrayList<>();

    /** A location check the test answers by hand. */
    private record PendingCheck(Path location, String name, ProjectBlueprint blueprint,
                                CompletableFuture<LocationCheck> answer) {
    }

    /** Runs nothing until told to. */
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

    private static final class ManualTimer implements CreateProjectForm.Timer {
        private final Runnable action;
        private boolean running;

        ManualTimer(Runnable action) {
            this.action = action;
        }

        @Override
        public void restart() {
            running = true;
        }

        @Override
        public void stop() {
            running = false;
        }

        boolean running() {
            return running;
        }

        void fire() {
            assertTrue(running, "the timer is not running");
            running = false;
            action.run();
        }
    }

    private static final class ManualTimers implements CreateProjectForm.Timers {
        private final Map<Duration, ManualTimer> byDelay = new HashMap<>();

        @Override
        public CreateProjectForm.Timer create(Duration delay, Runnable action) {
            ManualTimer timer = new ManualTimer(action);
            byDelay.put(delay, timer);
            return timer;
        }

        ManualTimer debounce() {
            return byDelay.get(CreateProjectForm.DEBOUNCE);
        }

        ManualTimer slowNotice() {
            return byDelay.get(CreateProjectForm.SLOW_NOTICE);
        }

        ManualTimer cancelGrace() {
            return byDelay.get(CreateProjectForm.CANCEL_GRACE);
        }
    }

    private final CreateProjectForm.Services services = new CreateProjectForm.Services() {
        @Override
        public CompletableFuture<EngineOptions> engines() {
            return enginesAnswer;
        }

        @Override
        public CompletableFuture<TemplateCatalogs.Answer> templates(EngineContent engine) {
            templateReads.add(engine);
            if (pendingTemplates) {
                CompletableFuture<TemplateCatalogs.Answer> answer = new CompletableFuture<>();
                pendingTemplateAnswers.add(answer);
                return answer;
            }
            if (templatesAnswer != null) {
                return templatesAnswer;
            }
            try {
                return CompletableFuture.completedFuture(TemplateCatalogs.Answer.of(TemplateScanner.SYSTEM.scan(engine)));
            } catch (IOException e) {
                return CompletableFuture.completedFuture(TemplateCatalogs.Answer.failed(e.getMessage()));
            }
        }

        @Override
        public CompletableFuture<LocationCheck> checkLocation(Path location, String name, ProjectBlueprint blueprint) {
            CompletableFuture<LocationCheck> answer = new CompletableFuture<>();
            checks.add(new PendingCheck(location, name, blueprint, answer));
            return answer;
        }

        @Override
        public CreationJob startCreation(CreateRequest request, CreationJob.ProgressListener listener) {
            requests.add(request);
            return CreationJob.start(hub, request, creation, store, listener);
        }
    };

    @BeforeEach
    void setUp() throws Exception {
        Path root = TestSupport.real(tempDir);
        projectsDir = Files.createDirectories(root.resolve("projects"));
        ProcessStarter noProcesses = builder -> {
            throw new IOException("no processes in this test");
        };
        hub = ProjectHub.create(TestSupport.environment(root.resolve("config"),
                new TickingClock(TestSupport.T0), Map.of(), noProcesses), TestSupport.log(root.resolve("logs")).log);

        // A registered dev engine whose engine.json names the fake content (VK-1689).
        Path repo = root.resolve("repo");
        TestSupport.writeEngineContent(repo);
        PickedEngine picked = hub.inspectEngine(TestSupport.devEngine(repo)).get(10, TimeUnit.SECONDS);
        hub.addEngine(assertInstanceOf(PickedEngine.Usable.class, picked, picked::toString));
        engineOptions = hub.checkEngines(hub.listEngines()).get(10, TimeUnit.SECONDS);
        enginesAnswer = CompletableFuture.completedFuture(engineOptions);
    }

    @AfterEach
    void tearDown() {
        hub.close();
    }

    /** A form whose answers come back on the calling thread, as they would on the FX thread. */
    private CreateProjectForm form() {
        CreateProjectForm form = new CreateProjectForm(services, timers, Runnable::run);
        form.setOnClosed(closedWith::add);
        return form;
    }

    /** Types a name and the projects folder, lets the debounce fire and answers the check with {@code answer}. */
    private void typeAndCheck(CreateProjectForm form, String name, LocationCheck answer) {
        form.nameProperty().set(name);
        form.locationProperty().set(projectsDir.toString());
        timers.debounce().fire();
        checks.getLast().answer().complete(answer);
    }

    private CreateProjectForm readyForm() {
        CreateProjectForm form = form();
        typeAndCheck(form, "Game", LocationCheck.ok(List.of()));
        assertEquals(State.READY, form.getState());
        return form;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Checking
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void itStartsQuietWithCreateDisabled() {
        CreateProjectForm form = form();

        assertEquals(State.EDITING, form.getState());
        assertEquals("", form.errorProperty().get());
        assertFalse(form.createEnabledProperty().get());
        assertTrue(form.cancelEnabledProperty().get());
        assertTrue(form.inputsEnabledProperty().get());
        assertTrue(form.openAfterCreateProperty().get(), "on by default when there is an editor");
        assertTrue(form.openAfterCreateEnabledProperty().get());
    }

    @Test
    void aNameRuleShowsAtOnceWithoutTouchingTheDisk() {
        CreateProjectForm form = form();
        form.locationProperty().set(projectsDir.toString());

        form.nameProperty().set("CON");

        assertEquals(State.INVALID, form.getState());
        assertEquals(ProjectNameRules.checkName("CON").orElseThrow().message(), form.errorProperty().get());
        assertFalse(timers.debounce().running());
        assertTrue(checks.isEmpty());
    }

    @Test
    void theNameIsNeverTrimmed() {
        CreateProjectForm form = form();
        form.locationProperty().set(projectsDir.toString());

        form.nameProperty().set("Game ");

        assertEquals(State.INVALID, form.getState());
        assertEquals("The project name can't end with a space.", form.errorProperty().get());
    }

    @Test
    void aRelativeOrEmptyLocationIsRefusedAsText() {
        CreateProjectForm form = form();
        form.nameProperty().set("Game");
        assertEquals(CreateProjectForm.NO_LOCATION, form.errorProperty().get());

        form.locationProperty().set("projects");

        assertEquals(State.INVALID, form.getState());
        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, form.errorProperty().get());
        assertTrue(checks.isEmpty());
    }

    @Test
    void theDiskIsCheckedOnlyOnceTypingPauses() {
        CreateProjectForm form = form();
        form.nameProperty().set("Game");
        form.locationProperty().set("  " + projectsDir + "  ");

        assertEquals(State.CHECKING, form.getState());
        assertEquals(CreateProjectForm.CHECKING, form.statusProperty().get());
        assertFalse(form.createEnabledProperty().get());
        assertTrue(checks.isEmpty(), "nothing is checked on a keystroke");

        timers.debounce().fire();
        assertEquals(1, checks.size());
        assertEquals(projectsDir, checks.getFirst().location(), "surrounding spaces are ignored");
        assertEquals("Game", checks.getFirst().name());

        checks.getFirst().answer().complete(LocationCheck.ok(List.of("Synced by OneDrive", "On a network drive")));
        assertEquals(State.READY, form.getState());
        assertTrue(form.createEnabledProperty().get());
        assertEquals("", form.statusProperty().get());
        assertEquals("Synced by OneDrive\nOn a network drive", form.warningsProperty().get());
        assertEquals(projectsDir, form.readyLocation().orElseThrow());
    }

    @Test
    void anAnswerToAnOlderCheckIsIgnored() {
        CreateProjectForm form = form();
        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());
        timers.debounce().fire();
        PendingCheck first = checks.getFirst();

        form.nameProperty().set("Game2");
        first.answer().complete(LocationCheck.ok(List.of()));

        assertEquals(State.CHECKING, form.getState(), "the answer was for \"Game\"");
        timers.debounce().fire();
        checks.getLast().answer().complete(LocationCheck.refused("A folder named \"Game2\" already exists"));
        assertEquals(State.INVALID, form.getState());
        assertEquals("A folder named \"Game2\" already exists", form.errorProperty().get());
    }

    @Test
    void aSlowLocationSaysSoAndStillGetsItsAnswer() {
        CreateProjectForm form = form();
        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());
        timers.debounce().fire();

        timers.slowNotice().fire();
        assertEquals(CreateProjectForm.SLOW, form.statusProperty().get());
        assertEquals(State.CHECKING, form.getState());

        checks.getFirst().answer().complete(LocationCheck.ok(List.of()));
        assertEquals(State.READY, form.getState());
        assertEquals("", form.statusProperty().get());
    }

    @Test
    void aCheckThatFailsIsShownAsAnError() {
        CreateProjectForm form = form();
        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());
        timers.debounce().fire();

        checks.getFirst().answer().completeExceptionally(new IllegalStateException("checker broke"));

        assertEquals(State.INVALID, form.getState());
        assertEquals("Could not check the location: checker broke", form.errorProperty().get());
    }

    @Test
    void editingAReadyFormChecksAgain() {
        CreateProjectForm form = readyForm();

        form.nameProperty().set("Game2");

        assertEquals(State.CHECKING, form.getState());
        assertFalse(form.createEnabledProperty().get());
        assertTrue(form.readyLocation().isEmpty());
    }

    @Test
    void createDoesNothingUnlessTheFormIsReady() {
        CreateProjectForm form = form();
        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());

        form.create();

        assertEquals(State.CHECKING, form.getState());
        assertTrue(creation.queued.isEmpty());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Engines, templates and options (VK-1689)
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theEngineANewProjectWouldOpenWithIsPreselectedWithItsEmptyTemplate() {
        CreateProjectForm form = form();

        assertEquals(1, form.engines().size());
        assertEquals(form.engines().getFirst(), form.engineProperty().get());
        assertEquals(List.of(form.engines().getFirst().content()), templateReads);
        assertEquals("empty", form.templateProperty().get().id());
        assertEquals("A test template.", form.templateDescriptionProperty().get());
        assertEquals(List.of("examples", "vfxLibrary"), form.options().stream().map(OptionChoice::id).toList());
        assertTrue(form.options().stream().allMatch(OptionChoice::available));
        assertTrue(form.options().stream().noneMatch(option -> option.selectedProperty().get()),
                "the template ticks neither by default");
        assertEquals("", form.noteProperty().get());
        assertEquals(State.EDITING, form.getState(), "nothing typed yet");
    }

    @Test
    void whileTheEnginesAreReadTheFormWaitsWithCreateDisabled() {
        enginesAnswer = new CompletableFuture<>();
        CreateProjectForm form = form();
        assertEquals(CreateProjectForm.READING_ENGINES, form.enginePromptProperty().get());

        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());

        assertEquals(State.LOADING, form.getState());
        assertEquals(CreateProjectForm.READING_ENGINES, form.statusProperty().get());
        assertFalse(form.createEnabledProperty().get());
        assertTrue(form.inputsEnabledProperty().get(), "the name and location stay editable");
        assertFalse(timers.debounce().running(), "the location waits for the engine");

        enginesAnswer.complete(engineOptions);

        assertEquals(State.CHECKING, form.getState());
        assertTrue(timers.debounce().running());
        timers.debounce().fire();
        assertEquals(1, checks.size());
        assertEquals(TestSupport.FAKE_PAYLOAD.size() + TestSupport.FAKE_LIBRARY.size() + 2,
                checks.getFirst().blueprint().fileCount(), "the check gets the chosen files");
    }

    @Test
    void withoutAnEngineNothingCanBeCreated() {
        enginesAnswer = CompletableFuture.completedFuture(
                new EngineOptions(engineOptions.registry(), java.util.Optional.empty(), List.of()));
        CreateProjectForm form = form();

        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());

        assertEquals(State.INVALID, form.getState());
        assertEquals(EngineResolver.NO_ENGINE, form.errorProperty().get());
        assertEquals(CreateProjectForm.NO_ENGINE_PROMPT, form.enginePromptProperty().get());
        assertFalse(form.openAfterCreateEnabledProperty().get());
        assertTrue(checks.isEmpty());
    }

    @Test
    void anEngineCheckThatFailsIsShownAsAnError() {
        enginesAnswer = CompletableFuture.failedFuture(new IllegalStateException("engines.json is locked"));
        CreateProjectForm form = form();

        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());

        assertEquals(State.INVALID, form.getState());
        assertEquals("The engines could not be checked: engines.json is locked", form.errorProperty().get());
    }

    @Test
    void templatesThatCannotBeReadAreShownAsAnError() {
        templatesAnswer = CompletableFuture.completedFuture(TemplateCatalogs.Answer.failed("The templates are gone."));
        CreateProjectForm form = form();

        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());

        assertEquals(State.INVALID, form.getState());
        assertEquals("The templates are gone.", form.errorProperty().get());
        assertEquals(CreateProjectForm.NO_TEMPLATE_PROMPT, form.templatePromptProperty().get());
        assertTrue(form.templates().isEmpty());
    }

    @Test
    void anAnswerForAnEngineChoiceThatWasReplacedIsIgnored() throws Exception {
        pendingTemplates = true;
        CreateProjectForm form = form();
        EngineChoice chosen = form.engineProperty().get();

        form.engineProperty().set(null);
        form.engineProperty().set(chosen);
        assertEquals(2, pendingTemplateAnswers.size(), "choosing the engine again reads its templates again");
        TemplateCatalogs.Answer read = TemplateCatalogs.Answer.of(TemplateScanner.SYSTEM.scan(chosen.content()));

        pendingTemplateAnswers.getFirst().complete(read);
        assertTrue(form.templates().isEmpty(), "the first read's answer is out of date");
        assertEquals(CreateProjectForm.READING_TEMPLATES, form.templatePromptProperty().get());

        pendingTemplateAnswers.getLast().complete(read);
        assertEquals("empty", form.templateProperty().get().id());
        assertEquals(2, form.options().size());
    }

    @Test
    void tickingAnOptionChecksAgainAndTheProjectGetsItsFiles() {
        CreateProjectForm form = readyForm();
        OptionChoice examples = form.options().getFirst();

        examples.selectedProperty().set(true);

        assertEquals(State.CHECKING, form.getState());
        timers.debounce().fire();
        assertEquals(TestSupport.FAKE_PAYLOAD.size() + TestSupport.FAKE_LIBRARY.size() + 1 + 2,
                checks.getLast().blueprint().fileCount());
        checks.getLast().answer().complete(LocationCheck.ok(List.of()));
        assertEquals(State.READY, form.getState());

        form.create();
        creation.runAll();
        store.runAll();

        assertEquals(Set.of("examples"), requests.getLast().options());
        assertTrue(Files.isRegularFile(projectsDir.resolve("Game/assets/scripts/game/examples/PlayerMovement.mt")));
        assertFalse(Files.exists(projectsDir.resolve("Game/assets/vfx")));
    }

    @Test
    void anOptionTheEngineCannotProvideIsOfferedDisabledWithItsReason() throws Exception {
        EngineContent full = form().engineProperty().get().content();
        EngineContent withoutVfx = new EngineContent(full.engineId(), full.engineVersion(), full.templatesDir(),
                full.scriptLibDir(), full.examplesDir(), java.util.Optional.empty(), full.label(), full.path(), false);
        templatesAnswer = CompletableFuture.completedFuture(
                TemplateCatalogs.Answer.of(TemplateScanner.SYSTEM.scan(withoutVfx)));

        CreateProjectForm form = form();
        OptionChoice vfx = form.options().getLast();

        assertEquals("vfxLibrary", vfx.id());
        assertFalse(vfx.available());
        assertTrue(vfx.unavailableReason().orElseThrow().contains("vfxLibraryDir"), vfx.unavailableReason().get());
        vfx.selectedProperty().set(true);
        assertFalse(vfx.chosen(), "an option that is not available is never added");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Creating
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void createKeepsTheDialogOpenAndClosesItWithTheProject() {
        CreateProjectForm form = readyForm();

        form.create();
        assertEquals(State.CREATING, form.getState());
        assertFalse(form.inputsEnabledProperty().get());
        assertFalse(form.createEnabledProperty().get());
        assertTrue(form.cancelEnabledProperty().get());
        assertTrue(form.busyProperty().get());
        assertEquals(CreateProjectForm.INDETERMINATE, form.progressProperty().get());
        assertEquals(CreateProjectForm.PREPARING, form.statusProperty().get());

        creation.runAll();
        assertEquals(1.0, form.progressProperty().get());
        assertTrue(form.statusProperty().get().startsWith("Creating files…"), form.statusProperty().get());
        assertEquals(State.CREATING, form.getState(), "still listing it");

        store.runAll();
        assertEquals(State.CLOSED, form.getState());
        Result result = form.result();
        assertNotNull(result.job());
        assertTrue(result.openAfterCreate());
        CreateOutcome outcome = result.job().outcome().join();
        assertTrue(outcome.created() && outcome.listed());
        assertEquals(List.of(result), closedWith);
    }

    @Test
    void theRequestCarriesTheChosenEngineTemplateAndOptions() {
        CreateProjectForm form = readyForm();

        form.create();
        creation.runAll();
        store.runAll();

        CreateRequest request = requests.getLast();
        assertEquals(form.engines().getFirst().content(), request.engine());
        assertEquals("empty", request.templateId());
        assertEquals(Set.of(), request.options());
        assertTrue(Files.isRegularFile(projectsDir.resolve("Game/assets/scripts/game/Main.mt")));
    }

    @Test
    void untickingTheBoxIsRespected() {
        CreateProjectForm form = readyForm();
        form.openAfterCreateProperty().set(false);

        form.create();
        creation.runAll();
        store.runAll();

        assertFalse(form.result().openAfterCreate());
    }

    @Test
    void aFailureStaysInTheDialogForAnotherTry() throws IOException {
        CreateProjectForm form = readyForm();
        // Something fills the folder between the check and the creation.
        Files.createDirectory(projectsDir.resolve("Game"));
        Files.writeString(projectsDir.resolve("Game/keep.txt"), "mine");

        form.create();
        creation.runAll();

        assertEquals(State.FAILED, form.getState());
        assertTrue(form.errorProperty().get().contains("isn't empty"), form.errorProperty().get());
        assertTrue(form.inputsEnabledProperty().get());
        assertFalse(form.busyProperty().get());
        assertNull(form.result());
        assertTrue(store.queued.isEmpty());
        assertTrue(closedWith.isEmpty());

        form.nameProperty().set("Game2");
        assertEquals(State.CHECKING, form.getState());
    }

    @Test
    void createTriesAgainStraightAfterAFailure() throws IOException {
        CreateProjectForm form = readyForm();
        Path blocker = Files.createDirectory(projectsDir.resolve("Game"));
        Files.writeString(blocker.resolve("keep.txt"), "mine");
        form.create();
        creation.runAll();
        assertEquals(State.FAILED, form.getState());
        assertTrue(form.createEnabledProperty().get(), "the message says to try again");

        // Whatever was in the way is gone now.
        Files.delete(blocker.resolve("keep.txt"));
        Files.delete(blocker);
        form.create();

        assertEquals(State.CREATING, form.getState());
        assertEquals("", form.errorProperty().get());
        creation.runAll();
        store.runAll();
        assertEquals(State.CLOSED, form.getState());
        assertTrue(form.result().job().outcome().join().created());
    }

    @Test
    void cancelWaitsForTheCreationToCleanUp() {
        CreateProjectForm form = readyForm();
        form.create();

        form.cancel();
        assertEquals(State.CANCELLING, form.getState());
        assertFalse(form.cancelEnabledProperty().get());
        assertEquals(CreateProjectForm.CANCELLING, form.statusProperty().get());
        assertTrue(timers.cancelGrace().running());

        creation.runAll();

        assertEquals(State.CLOSED, form.getState());
        Result result = form.result();
        assertFalse(result.openAfterCreate());
        assertTrue(result.job().outcome().join().result().cancelled());
        assertEquals(Set.of(), TestSupport.fileNames(projectsDir));
        assertFalse(timers.cancelGrace().running());
    }

    @Test
    void aCancelThatTakesTooLongHandsTheCreationToTheWindow() {
        CreateProjectForm form = readyForm();
        form.create();
        form.cancel();

        timers.cancelGrace().fire();

        assertEquals(State.CLOSED, form.getState());
        CreationJob job = form.result().job();
        assertNotNull(job);
        assertFalse(job.outcome().isDone(), "still running; the window follows it from here");
        assertFalse(form.result().openAfterCreate());

        creation.runAll();
        assertTrue(job.outcome().join().result().cancelled());
        assertEquals(State.CLOSED, form.getState());
        assertEquals(1, closedWith.size());
    }

    @Test
    void cancelBeforeCreatingClosesWithNothing() {
        CreateProjectForm form = form();
        form.nameProperty().set("Game");
        form.locationProperty().set(projectsDir.toString());
        timers.debounce().fire();

        form.cancel();

        assertEquals(State.CLOSED, form.getState());
        assertSame(Result.NOTHING, form.result());
        assertFalse(timers.slowNotice().running());
        // A late answer changes nothing.
        checks.getFirst().answer().complete(LocationCheck.ok(List.of()));
        assertEquals(State.CLOSED, form.getState());
        assertEquals(List.of(Result.NOTHING), closedWith);
    }
}
