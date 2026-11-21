package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.settings.LauncherState.SortKey;
import com.vertexforge.launcher.viewmodel.ProjectOrder;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The projects table's rows (VK-1694): the search, pinned projects first under any sort, and ties in the list's own
 * order. The lists are javafx.base ones, so this runs without the JavaFX toolkit.
 */
class ProjectTableModelTest {

    private static final Path PROJECTS = Path.of("D:/Projects").toAbsolutePath();
    private static final Comparator<ProjectViewModel> BY_NAME = Comparator.comparing(ProjectViewModel::getName);

    private static ProjectViewModel row(String name, int minutes, boolean pinned) {
        RecentEntry entry = RecentEntry.create(UUID.randomUUID(), PROJECTS.resolve(name).resolve(name + ".vfproj"),
                name, TestSupport.T0.plusSeconds(60L * minutes)).withPinned(pinned);
        ProjectViewModel row = new ProjectViewModel(entry);
        row.setEngineText("1.0.0 Development");
        return row;
    }

    private static List<String> shown(ProjectTableModel model) {
        return model.sorted().stream().map(ProjectViewModel::getName).toList();
    }

    @Test
    void withoutASortTheListsOwnOrderIsKeptWithPinnedProjectsFirst() {
        ProjectTableModel model = new ProjectTableModel();

        // The list's own order: most recently opened first.
        model.setAll(List.of(row("Newest", 3, false), row("Pinned", 1, true), row("Middle", 2, false),
                row("Oldest", 0, false)));

        assertEquals(List.of("Pinned", "Newest", "Middle", "Oldest"), shown(model));
    }

    @Test
    void pinnedProjectsStayOnTopUnderAColumnSortBothWays() {
        ProjectTableModel model = new ProjectTableModel();
        model.setAll(List.of(row("Delta", 3, false), row("Bravo", 2, true), row("Charlie", 1, false),
                row("Alpha", 0, true)));

        model.applyComparator(ProjectOrder.pinnedFirst(BY_NAME));
        assertEquals(List.of("Alpha", "Bravo", "Charlie", "Delta"), shown(model));

        model.applyComparator(ProjectOrder.pinnedFirst(BY_NAME.reversed()));
        assertEquals(List.of("Bravo", "Alpha", "Delta", "Charlie"), shown(model));
    }

    @Test
    void rowsThatTieKeepTheListsOwnOrder() {
        ProjectTableModel model = new ProjectTableModel();
        List<ProjectViewModel> rows = new ArrayList<>();
        for (int i = 0; i < 20; i++) {
            ProjectViewModel row = row("P" + i, 100 - i, false);
            row.setStatus(ProjectStatus.ok());
            rows.add(row);
        }
        model.setAll(rows);

        // Every row has the same status, so a sort by status changes nothing.
        model.applyComparator(ProjectOrder.pinnedFirst(Comparator.comparing(row -> row.getStatus().kind())));

        assertEquals(rows.stream().map(ProjectViewModel::getName).toList(), shown(model));
    }

    @Test
    void theSearchNarrowsTheShownRowsButNeverTheSource() {
        ProjectTableModel model = new ProjectTableModel();
        ProjectViewModel shooter = row("Shooter", 2, false);
        ProjectViewModel racer = row("Racer", 1, false);
        model.setAll(List.of(shooter, racer));

        model.setQuery("shoot");

        assertTrue(model.filtering());
        assertEquals(List.of("Shooter"), shown(model));
        assertEquals(2, model.source().size());
        assertTrue(model.isVisible(shooter));
        assertFalse(model.isVisible(racer));

        model.setQuery("  ");
        assertFalse(model.filtering());
        assertEquals(List.of("Shooter", "Racer"), shown(model));
    }

    @Test
    void aRowWhoseEngineChangesIsFilteredAgainAtOnce() {
        ProjectTableModel model = new ProjectTableModel();
        ProjectViewModel game = row("Game", 1, false);
        model.setAll(List.of(game));
        model.setQuery("release");
        assertEquals(List.of(), shown(model));

        game.setEngineText("1.2.0 Release");

        assertEquals(List.of("Game"), shown(model));
    }

    @Test
    void aReusedRowIsPlacedByItsNewPositionAndPin() {
        ProjectTableModel model = new ProjectTableModel();
        ProjectViewModel first = row("First", 2, false);
        ProjectViewModel second = row("Second", 1, false);
        model.setAll(List.of(first, second));
        assertEquals(List.of("First", "Second"), shown(model));

        // Second was opened: the next read lists it first, and the same row objects are shown again.
        model.setAll(List.of(second, first));
        assertEquals(List.of("Second", "First"), shown(model));

        // First was pinned: the reused row now carries the pin.
        first.update(first.getEntry().withPinned(true));
        assertEquals(List.of("First", "Second"), shown(model));
    }

    @Test
    void aStatusChangeReSortsAStatusSortAtOnce() {
        ProjectTableModel model = new ProjectTableModel();
        ProjectViewModel a = row("A", 2, false);
        ProjectViewModel b = row("B", 1, false);
        a.setStatus(ProjectStatus.ok());
        b.setStatus(ProjectStatus.ok());
        model.setAll(List.of(a, b));
        model.applyComparator(ProjectOrder.pinnedFirst(Comparator.comparing(row -> row.getStatus().kind())));
        assertEquals(List.of("A", "B"), shown(model));

        a.setStatus(ProjectStatus.missing("moved"));

        // MISSING sorts after OK in the enum, so A now follows B.
        assertTrue(ProjectStatus.Kind.MISSING.compareTo(ProjectStatus.Kind.OK) > 0);
        assertEquals(List.of("B", "A"), shown(model));
    }

    /** Many row changes at once (the engines' answer, a re-read) sort once, not once per change. */
    @Test
    void aBatchSortsOnceAtTheEnd() {
        ProjectTableModel model = new ProjectTableModel();
        List<ProjectViewModel> rows = new ArrayList<>();
        for (int i = 0; i < 60; i++) {
            rows.add(row(String.format("P%02d", i), 100 - i, false));
        }
        model.setAll(rows);
        int[] comparisons = {0};
        Comparator<ProjectViewModel> counting = (left, right) -> {
            comparisons[0]++;
            return right.getEngineText().compareTo(left.getEngineText());
        };
        model.applyComparator(ProjectOrder.pinnedFirst(counting));

        comparisons[0] = 0;
        model.batch(() -> {
            for (int i = 0; i < rows.size(); i++) {
                rows.get(i).setEngineText(String.format("1.0.%02d", i));
            }
        });
        int batched = comparisons[0];

        comparisons[0] = 0;
        for (int i = 0; i < rows.size(); i++) {
            rows.get(i).setEngineText(String.format("2.0.%02d", i));
        }
        int oneByOne = comparisons[0];

        // One sort of 60 rows is about 60 log 60 comparisons; one sort per change is at least 60 x 59.
        assertTrue(batched > 0, "the batch sorts at its end");
        assertTrue(batched * 4 < oneByOne, batched + " comparisons in a batch, " + oneByOne + " one by one");
        // Descending by engine text: the last row has the highest version.
        assertEquals("P59", shown(model).getFirst());
    }

    @Test
    void aBatchThatThrowsStillRestoresTheOrder() {
        ProjectTableModel model = new ProjectTableModel();
        ProjectViewModel a = row("A", 1, false);
        ProjectViewModel b = row("B", 2, false);
        model.setAll(List.of(a, b));
        model.applyComparator(ProjectOrder.pinnedFirst(BY_NAME));

        try {
            model.batch(() -> {
                model.applyComparator(ProjectOrder.pinnedFirst(BY_NAME.reversed()));
                throw new IllegalStateException("boom");
            });
        } catch (IllegalStateException expected) {
            // The order chosen inside the batch still applies.
        }

        assertEquals(List.of("B", "A"), shown(model));
    }

    @Test
    void theSortKeysAreStableAndCleaned() {
        assertEquals(List.of("name", "status", "engine", "path", "lastOpened"), ProjectSort.KEYS);
        assertEquals(List.of(new SortKey("path", false), new SortKey("name", true)), ProjectSort.sanitize(List.of(
                new SortKey("path", false), new SortKey("colour", true), new SortKey("name", true),
                new SortKey("path", true))));
    }
}
