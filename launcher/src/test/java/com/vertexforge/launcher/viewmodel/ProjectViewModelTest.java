package com.vertexforge.launcher.viewmodel;

import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
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
}
