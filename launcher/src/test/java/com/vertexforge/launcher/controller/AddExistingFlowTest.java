package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.controller.AddExistingFlow.Item;
import com.vertexforge.launcher.controller.AddExistingFlow.Report;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.store.StoreWriteException.Reason;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The add flow without the JavaFX toolkit: every answer and the FX thread are driven by the test (VK-1685). */
class AddExistingFlowTest {

    private static final Path ROOT = Path.of(TestSupport.isWindows() ? "C:\\projects" : "/projects");

    private final QueueExecutor fx = new QueueExecutor();
    private final FakeServices services = new FakeServices();
    private AddExistingFlow flow;

    /** Runs nothing until told to, like answers waiting for the FX thread. */
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

    /** One call to {@link AddExistingFlow.Services#open}. */
    private record Opened(Path projectFile, RecentEntry entry, String name) {
    }

    private static final class FakeServices implements AddExistingFlow.Services {
        final Map<Path, PickedProject> answers = new HashMap<>();
        final Map<Path, CompletableFuture<PickedProject>> pending = new HashMap<>();
        final List<Path> inspected = new ArrayList<>();
        final Set<Path> listed = new HashSet<>();
        final List<PickedProject.Found> addedFound = new ArrayList<>();
        final List<AddedProject> shown = new ArrayList<>();
        final Deque<Optional<PickedProject.Found>> choices = new ArrayDeque<>();
        final List<PickedProject.Choose> asked = new ArrayList<>();
        final List<Throwable> saveFailures = new ArrayList<>();
        final List<Opened> opened = new ArrayList<>();
        final List<Report> reports = new ArrayList<>();
        StoreWriteException addFailure;
        boolean readOnly;
        boolean busy;
        boolean openStarts = true;

        @Override
        public CompletableFuture<PickedProject> inspect(Path path) {
            inspected.add(path);
            CompletableFuture<PickedProject> waiting = pending.get(path);
            if (waiting != null) {
                return waiting;
            }
            PickedProject answer = answers.get(path);
            return CompletableFuture.completedFuture(answer != null ? answer
                    : new PickedProject.Refused(path, "Was not found"));
        }

        @Override
        public CompletableFuture<AddedProject> add(PickedProject.Found found) {
            addedFound.add(found);
            if (addFailure != null) {
                return CompletableFuture.failedFuture(addFailure);
            }
            boolean existed = !listed.add(found.file());
            RecentEntry entry = RecentEntry.create(UUID.randomUUID(), found.file(), found.project().projectName(),
                    TestSupport.T0);
            RecentProjectsView view = new RecentProjectsView(List.of(entry), StoreStatus.OK, Optional.empty(), "",
                    0);
            return CompletableFuture.completedFuture(new AddedProject(view, entry, existed));
        }

        @Override
        public Optional<PickedProject.Found> choose(PickedProject.Choose choose) {
            asked.add(choose);
            return choices.isEmpty() ? Optional.empty() : choices.removeFirst();
        }

        @Override
        public boolean readOnly() {
            return readOnly;
        }

        @Override
        public boolean busy() {
            return busy;
        }

        @Override
        public void added(AddedProject added) {
            shown.add(added);
        }

        @Override
        public void saveFailed(Throwable error) {
            saveFailures.add(error);
        }

        @Override
        public boolean open(Path projectFile, RecentEntry entry, String name) {
            opened.add(new Opened(projectFile, entry, name));
            return openStarts;
        }

        @Override
        public void report(Report report) {
            reports.add(report);
        }
    }

    @BeforeEach
    void setUp() {
        flow = new AddExistingFlow(services, fx);
    }

    /** A project file the inspector finds, named {@code name}. */
    private PickedProject.Found project(String folder, String name) {
        Path file = ROOT.resolve(folder).resolve(folder + ".vfproj");
        PickedProject.Found found = new PickedProject.Found(file, new ProjectFile(name, "1.0", "VertexForge 1.0.0"));
        services.answers.put(file, found);
        return found;
    }

    private static Item add(Path path) {
        return new Item(path, false);
    }

    private static Item open(Path path) {
        return new Item(path, true);
    }

    private Report onlyReport() {
        assertEquals(1, services.reports.size(), services.reports::toString);
        return services.reports.getFirst();
    }

    @Test
    void requestsRunInOrderAndOneReportListsWhatWasAdded() {
        PickedProject.Found a = project("A", "Alpha");
        PickedProject.Found b = project("B", "Beta");

        flow.submit(List.of(add(a.file()), add(b.file())));
        assertTrue(flow.isBusy());
        fx.runAll();

        assertEquals(List.of(a.file(), b.file()), services.inspected);
        assertEquals(List.of(a, b), services.addedFound);
        assertEquals(2, services.shown.size(), "each added project is shown and selected");
        Report report = onlyReport();
        assertEquals(List.of("Alpha", "Beta"), report.added());
        assertEquals(Optional.of("Added \"Alpha\" and \"Beta\"."), report.summary());
        assertFalse(report.hasWarning());
        assertTrue(report.refusals().isEmpty());
        assertFalse(flow.isBusy());
        assertTrue(services.opened.isEmpty(), "adding never opens");
    }

    @Test
    void aFolderWithSeveralProjectsAsksWhichOneAndCancelAddsNothing() {
        PickedProject.Found first = project("Multi", "First");
        Path folder = ROOT.resolve("Multi");
        PickedProject.Choose choose = new PickedProject.Choose(folder, List.of(first,
                new PickedProject.Refused(folder.resolve("Broken.vfproj"), "Not valid JSON at line 1, column 2: x")));
        services.answers.put(folder, choose);

        flow.submit(List.of(add(folder)));
        fx.runAll();

        assertEquals(List.of(choose), services.asked);
        assertTrue(services.addedFound.isEmpty());
        assertTrue(services.reports.isEmpty(), "a cancelled choice reports nothing");
        assertFalse(flow.isBusy());

        services.choices.add(Optional.of(first));
        flow.submit(List.of(add(folder)));
        fx.runAll();

        assertEquals(List.of(first), services.addedFound);
        assertEquals(List.of("First"), onlyReport().added());
    }

    @Test
    void validAndInvalidRequestsGiveOneReportThatNamesEveryRefusal() {
        PickedProject.Found good = project("Good", "Good");
        Path broken = ROOT.resolve("Broken/Broken.vfproj");
        services.answers.put(broken, new PickedProject.Refused(broken,
                "Not valid JSON at line 3, column 5: expected ':'"));
        Path empty = ROOT.resolve("Empty");
        services.answers.put(empty, new PickedProject.Refused(empty, "Contains no .vfproj file"));

        flow.submit(List.of(add(broken), add(good.file()), add(empty)));
        fx.runAll();

        Report report = onlyReport();
        assertEquals(List.of("Good"), report.added());
        assertEquals(2, report.refused().size());
        String text = report.refusals().orElseThrow();
        assertTrue(text.startsWith("Could not add 2 items:"), text);
        assertTrue(text.contains(broken + ": Not valid JSON at line 3, column 5: expected ':'"), text);
        assertTrue(text.contains(empty + ": Contains no .vfproj file"), text);
    }

    @Test
    void aSingleInvalidProjectFileReportsItsParseError() {
        Path broken = ROOT.resolve("Broken/Broken.vfproj");
        services.answers.put(broken, new PickedProject.Refused(broken,
                "Not valid JSON at line 3, column 5: expected ':'"));

        flow.submit(List.of(add(broken)));
        fx.runAll();

        Report report = onlyReport();
        assertTrue(report.summary().isEmpty());
        assertEquals(Optional.of("Could not add " + broken + ":\nNot valid JSON at line 3, column 5: expected ':'"),
                report.refusals());
        assertTrue(services.addedFound.isEmpty());
    }

    @Test
    void aListedProjectIsSelectedAndReportedAsAlreadyInTheList() {
        PickedProject.Found game = project("Game", "Game");
        services.listed.add(game.file());

        flow.submit(List.of(add(game.file())));
        fx.runAll();

        assertEquals(1, services.shown.size(), "the listed project is still shown and selected");
        Report report = onlyReport();
        assertEquals(List.of("Game"), report.alreadyListed());
        assertEquals(Optional.of("\"Game\" is already in the list."), report.summary());
    }

    @Test
    void aReadOnlyListAddsNothingAndSaysSo() {
        PickedProject.Found game = project("Game", "Game");
        services.readOnly = true;

        flow.submit(List.of(add(game.file())));
        fx.runAll();

        assertTrue(services.addedFound.isEmpty());
        Report report = onlyReport();
        assertEquals(List.of("Game"), report.notAdded());
        assertTrue(report.hasWarning());
        assertTrue(report.summary().orElseThrow().contains("read-only"), report.summary()::toString);
    }

    @Test
    void anOpenRequestOpensTheListedEntry() {
        PickedProject.Found game = project("Game", "My Game");

        flow.submit(List.of(open(game.file())));
        fx.runAll();

        assertEquals(1, services.opened.size());
        Opened opened = services.opened.getFirst();
        AddedProject added = services.shown.getFirst();
        assertSame(added.entry(), opened.entry());
        assertEquals(added.entry().path(), opened.projectFile());
        assertEquals("My Game", opened.name());
        assertEquals(List.of("My Game"), onlyReport().added());
    }

    @Test
    void anOpenRequestOnAReadOnlyListOpensTheProjectUnlisted() {
        PickedProject.Found game = project("Game", "My Game");
        services.readOnly = true;

        flow.submit(List.of(open(game.file())));
        fx.runAll();

        assertTrue(services.addedFound.isEmpty());
        assertEquals(List.of(new Opened(game.file(), null, "My Game")), services.opened);
        assertTrue(services.reports.isEmpty(), "the read-only banner already explains the list");
    }

    @Test
    void anOpenRequestWhoseListWriteFailsStillOpensTheProject() {
        PickedProject.Found game = project("Game", "My Game");
        services.addFailure = new StoreWriteException(Reason.NEWER_VERSION, "newer", null);

        flow.submit(List.of(open(game.file())));
        fx.runAll();

        assertEquals(1, services.saveFailures.size());
        assertSame(services.addFailure, services.saveFailures.getFirst(), "the cause, not a CompletionException");
        assertEquals(List.of(new Opened(game.file(), null, "My Game")), services.opened);
        assertFalse(flow.isBusy());
    }

    @Test
    void aProjectThatCouldNotBeOpenedBecauseOfAnotherLaunchIsReported() {
        PickedProject.Found game = project("Game", "Game");
        services.openStarts = false;

        flow.submit(List.of(open(game.file())));
        fx.runAll();

        Report report = onlyReport();
        assertEquals(List.of("Game"), report.notOpened());
        assertTrue(report.hasWarning());
    }

    @Test
    void requestsWaitWhileTheWindowIsBusyAndRunWhenItIsFreeAgain() {
        PickedProject.Found a = project("A", "A");
        PickedProject.Found b = project("B", "B");
        services.busy = true;

        flow.submit(List.of(open(a.file()), open(b.file())));
        fx.runAll();

        assertTrue(services.inspected.isEmpty(), "nothing runs while the window is busy");
        assertTrue(flow.isBusy(), "the requests are waiting");

        services.busy = false;
        // The first open starts a launch: the window is busy again until it ends.
        services.openStarts = true;
        flow.drain();
        services.busy = true;
        fx.runAll();
        assertEquals(List.of(a.file()), services.opened.stream().map(Opened::projectFile).toList());
        assertTrue(flow.isBusy());
        assertTrue(services.reports.isEmpty(), "the report waits until every request ran");

        services.busy = false;
        flow.drain();
        fx.runAll();
        assertEquals(List.of(a.file(), b.file()), services.opened.stream().map(Opened::projectFile).toList(),
                "every open opens, one after the other");
        assertEquals(List.of("A", "B"), onlyReport().added());
        assertFalse(flow.isBusy());
    }

    @Test
    void aRequestSubmittedWhileAnotherRunsWaitsForIt() {
        PickedProject.Found a = project("A", "A");
        PickedProject.Found b = project("B", "B");
        CompletableFuture<PickedProject> slow = new CompletableFuture<>();
        services.pending.put(a.file(), slow);

        flow.submit(List.of(add(a.file())));
        flow.submit(List.of(add(b.file())));
        fx.runAll();
        assertEquals(List.of(a.file()), services.inspected, "the second waits for the first");

        slow.complete(a);
        fx.runAll();
        assertEquals(List.of(a.file(), b.file()), services.inspected);
        assertEquals(List.of("A", "B"), onlyReport().added());
    }

    @Test
    void aCheckThatFailsIsReportedAsARefusal() {
        Path path = ROOT.resolve("Odd.vfproj");
        services.pending.put(path, CompletableFuture.failedFuture(new IllegalStateException("boom")));

        flow.submit(List.of(add(path)));
        fx.runAll();

        Report report = onlyReport();
        assertEquals(List.of(new Report.Refusal(path, "Could not be checked: boom")), report.refused());
    }

    @Test
    void nothingToDoReportsNothing() {
        flow.submit(List.of());
        fx.runAll();
        flow.drain();

        assertTrue(services.reports.isEmpty());
        assertFalse(flow.isBusy());
    }

    @Test
    void namesAreQuotedAndJoinedLikeASentence() {
        assertEquals("\"A\"", Report.quoted(List.of("A")));
        assertEquals("\"A\" and \"B\"", Report.quoted(List.of("A", "B")));
        assertEquals("\"A\", \"B\" and \"C\"", Report.quoted(List.of("A", "B", "C")));
        Report report = new Report(List.of(), List.of("A", "B"), List.of(), List.of(), List.of());
        assertEquals(Optional.of("\"A\" and \"B\" are already in the list."), report.summary());
        assertNull(new Report(List.of(), List.of(), List.of(), List.of(), List.of()).summary().orElse(null));
    }
}
