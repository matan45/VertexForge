package com.vertexforge.launcher.viewmodel;

import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineLabels;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.session.EditorSession;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.List;
import java.util.UUID;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The view model only uses javafx.beans, so it runs on the plain test JVM without the JavaFX toolkit. */
class ProjectViewModelTest {

    @TempDir
    Path tempDir;

    private RecentEntry entry(String name, Instant lastOpened) {
        return RecentEntry.create(UUID.randomUUID(), tempDir.resolve(name + ".vfproj"), name, lastOpened);
    }

    @Test
    void lastOpenedIsShownAsALocalDateAndTime() {
        // T0 = 2026-01-02T03:04:05Z; Jerusalem is UTC+2 in January.
        assertEquals("2026-01-02 05:04", ProjectViewModel.formatLastOpened(TestSupport.T0, ZoneId.of("Asia/Jerusalem")));
        assertEquals("2026-01-02 03:04", ProjectViewModel.formatLastOpened(TestSupport.T0, ZoneOffset.UTC));
    }

    @Test
    void anUnknownTimeSaysSo() {
        assertEquals(ProjectViewModel.UNKNOWN_DATE, ProjectViewModel.formatLastOpened(null, ZoneOffset.UTC));
    }

    @Test
    void everyColumnHasAValueForAWrittenEntry() {
        ProjectViewModel row = new ProjectViewModel(entry("Game", TestSupport.T0));

        assertEquals("Game", row.nameProperty().get());
        assertEquals(tempDir.resolve("Game.vfproj").toString(), row.pathTextProperty().get());
        assertEquals(TestSupport.T0, row.lastOpenedProperty().get());
        assertEquals(ProjectStatus.CHECKING, row.statusProperty().get());
    }

    @Test
    void onlyAnOkRowCanBeOpened() {
        ProjectViewModel row = new ProjectViewModel(entry("Game", TestSupport.T0));
        assertFalse(row.isOpenable(), "a row that is still being checked cannot be opened");

        row.setStatus(ProjectStatus.ok());
        assertTrue(row.isOpenable());

        row.setStatus(ProjectStatus.missing("gone"));
        assertFalse(row.isOpenable());
    }

    @Test
    void anEntryWithoutAUsablePathCanNeverBeOpened() {
        RecentEntry relative = RecentEntry.fromJson(UUID.randomUUID(),
                JsonParser.parseString("{\"name\": \"R\", \"path\": \"relative/R.vfproj\"}").getAsJsonObject());
        ProjectViewModel row = new ProjectViewModel(relative);

        row.setStatus(ProjectStatus.ok());

        assertFalse(row.isOpenable());
        assertEquals("relative/R.vfproj", row.getPath());
    }

    @Test
    void anUpdateOfTheSameEntryKeepsTheStatus() {
        RecentEntry original = entry("Game", TestSupport.T0);
        ProjectViewModel row = new ProjectViewModel(original);
        row.setStatus(ProjectStatus.ok());
        Instant later = TestSupport.T0.plusSeconds(60);

        row.update(original.withLastOpened(later).withName("Renamed"));

        assertEquals(later, row.getLastOpened());
        assertEquals("Renamed", row.getName());
        assertTrue(row.getStatus().isOk());
        assertThrows(IllegalArgumentException.class, () -> row.update(entry("Other", later)));
    }

    @Test
    void onlyTheLatestCheckCounts() {
        ProjectViewModel row = new ProjectViewModel(entry("Game", TestSupport.T0));

        long first = row.startProbe();
        long second = row.startProbe();

        assertFalse(row.isCurrentProbe(first));
        assertTrue(row.isCurrentProbe(second));
    }

    @Test
    void theEngineColumnShowsTheBindingAndIsNeverBlank() {
        ProjectViewModel unbound = new ProjectViewModel(entry("Game", TestSupport.T0));
        ProjectViewModel bound = new ProjectViewModel(
                entry("Bound", TestSupport.T0).withEngineId(UUID.randomUUID().toString()));

        // Before the engines are checked: an unbound project follows the default, a bound one is not known yet.
        assertEquals(EngineLabels.DEFAULT, unbound.engineTextProperty().get());
        assertEquals(EngineLabels.CHECKING, bound.getEngineText());

        bound.setEngineText("1.0.0 Development");
        assertEquals("1.0.0 Development", bound.engineTextProperty().get());
        assertThrows(NullPointerException.class, () -> bound.setEngineText(null));
    }

    /** VK-1683: an editor this launcher started wins the Status column, and the file's status comes back after. */
    @Test
    void theStatusColumnShowsARunningEditor() {
        ProjectViewModel row = new ProjectViewModel(entry("Game", TestSupport.T0));
        row.setStatus(ProjectStatus.ok());
        assertEquals("OK", row.getStatusDisplay().label());
        assertFalse(row.getStatusDisplay().running());

        row.setSessionPhase(EditorSession.Phase.STARTING);
        assertEquals("Starting…", row.getStatusDisplay().label());
        row.setSessionPhase(EditorSession.Phase.READY);
        assertEquals("Running", row.statusDisplayProperty().get().label());
        assertTrue(row.getStatusDisplay().running());
        assertTrue(row.isOpenable(), "Open stays enabled, so a second Open can say the project is running");
        row.setStatus(ProjectStatus.missing("gone"));
        assertEquals("Running", row.getStatusDisplay().label(), "a status check never hides the running editor");

        row.setSessionPhase(EditorSession.Phase.EXITED);
        assertNull(row.getSessionPhase());
        assertEquals(ProjectStatus.missing("gone").label(), row.getStatusDisplay().label());
    }

    @Test
    void runningProjectsSortFirst() {
        StatusDisplay running = new StatusDisplay(ProjectStatus.ok(), EditorSession.Phase.READY);
        StatusDisplay idle = new StatusDisplay(ProjectStatus.ok(), null);
        StatusDisplay missing = new StatusDisplay(ProjectStatus.missing("x"), null);

        assertTrue(StatusDisplay.ORDER.compare(running, idle) < 0);
        assertTrue(StatusDisplay.ORDER.compare(idle, missing) < 0);
    }

    /** VK-1686: a fine project that cannot be used right now is not opened either. */
    @Test
    void anAlreadyOpenOrTooNewProjectCannotBeOpened() {
        ProjectViewModel row = new ProjectViewModel(entry("Game", TestSupport.T0));

        row.setStatus(ProjectStatus.alreadyOpen("open elsewhere"));
        assertFalse(row.isOpenable());
        assertEquals("Already open", row.getStatusDisplay().label());
        assertEquals("open elsewhere", row.getStatusDisplay().tooltip());

        row.setStatus(ProjectStatus.needsNewerEngine("Needs VertexForge 2.1.0", "saved by 2.1.0"));
        assertFalse(row.isOpenable());
        assertEquals("Needs VertexForge 2.1.0", row.getStatusDisplay().label());

        row.setStatus(ProjectStatus.invalid("bad"));
        assertFalse(row.isOpenable());
        assertEquals("Invalid", row.getStatusDisplay().label());

        row.setStatus(ProjectStatus.ok("could not check the lock"));
        assertTrue(row.isOpenable());
    }

    @Test
    void theNewKindsSortAfterOkAndBeforeTheFileProblems() {
        StatusDisplay ok = new StatusDisplay(ProjectStatus.ok(), null);
        StatusDisplay open = new StatusDisplay(ProjectStatus.alreadyOpen("x"), null);
        StatusDisplay newer = new StatusDisplay(ProjectStatus.needsNewerEngine("Needs VertexForge 2.0.0", "x"), null);
        StatusDisplay invalid = new StatusDisplay(ProjectStatus.invalid("x"), null);
        StatusDisplay missing = new StatusDisplay(ProjectStatus.missing("x"), null);

        assertEquals(List.of(ok, open, newer, invalid, missing),
                Stream.of(missing, invalid, newer, ok, open).sorted(StatusDisplay.ORDER).toList());
    }

    @Test
    void theFilesLastAnswerSurvivesAnUpdateOfTheSameEntry() {
        RecentEntry entry = entry("Game", TestSupport.T0);
        ProjectViewModel row = new ProjectViewModel(entry);
        assertNull(row.getFileCheck());
        ProjectFileCheck check = new ProjectFileCheck.Missing("gone");

        row.setFileCheck(check);
        row.update(entry.withName("Renamed"));

        assertSame(check, row.getFileCheck());
    }
}
