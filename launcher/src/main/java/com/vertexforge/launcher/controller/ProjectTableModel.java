package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.viewmodel.ProjectFilter;
import com.vertexforge.launcher.viewmodel.ProjectOrder;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import javafx.beans.Observable;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.collections.transformation.FilteredList;
import javafx.collections.transformation.SortedList;

import java.util.Comparator;
import java.util.List;
import java.util.Objects;

/**
 * The projects table's rows (VK-1694): every listed project in {@link #source()}, the search's matches in
 * {@link #sorted()}, ordered pinned first. The background checks, the engine labels and every lookup by id work on the
 * source, so a row the search hides is still kept up to date.
 * <p>
 * The sorted list re-sorts on every change of a row, so every value a sort or the search reads is in the extractor:
 * a value left out would only move its row at some later, unrelated change. Both derived lists follow their source
 * through weak listeners, so they are kept here, in fields. FX thread (the lists are {@code javafx.base} ones, so this
 * also runs in unit tests).
 */
final class ProjectTableModel {

    private final ObservableList<ProjectViewModel> source = FXCollections.observableArrayList(row -> new Observable[]{
            row.nameProperty(), row.pathTextProperty(), row.engineTextProperty(), row.lastOpenedProperty(),
            row.pinnedProperty(), row.statusProperty(), row.sessionPhaseProperty(), row.libraryProperty()});
    private final FilteredList<ProjectViewModel> filtered = new FilteredList<>(source, ProjectFilter.of(""));
    private final SortedList<ProjectViewModel> sorted = new SortedList<>(filtered, ProjectOrder.pinnedFirst(null));
    /** The order the table asked for; {@link #sorted} uses it except during a {@link #batch}. */
    private Comparator<ProjectViewModel> order = ProjectOrder.pinnedFirst(null);
    private String query = "";
    private boolean batching;

    /** Every listed project, in the list's own order (most recently opened first). */
    ObservableList<ProjectViewModel> source() {
        return source;
    }

    /** The rows the table shows: the search's matches, pinned first, then in the table's sort. */
    SortedList<ProjectViewModel> sorted() {
        return sorted;
    }

    /** Shows {@code rows} (the list's own order); each row's place in it becomes the last tiebreak of every sort. */
    void setAll(List<ProjectViewModel> rows) {
        for (int i = 0; i < rows.size(); i++) {
            rows.get(i).setDisplayIndex(i);
        }
        source.setAll(rows);
    }

    /** Filters the rows by the search box's text. */
    void setQuery(String text) {
        query = text == null ? "" : text;
        filtered.setPredicate(ProjectFilter.of(query));
    }

    /** The search box's text as last applied. */
    String query() {
        return query;
    }

    /** Whether the search is narrowing the list. */
    boolean filtering() {
        return !query.isBlank();
    }

    /** Whether {@code row} is listed and shown by the search. */
    boolean isVisible(ProjectViewModel row) {
        return row != null && filtered.getPredicate().test(row) && source.contains(row);
    }

    /** Orders the shown rows by {@code newOrder} (the table's sort policy hands it the column order). */
    void applyComparator(Comparator<ProjectViewModel> newOrder) {
        order = Objects.requireNonNull(newOrder, "newOrder");
        if (!batching) {
            sorted.setComparator(order);
        }
    }

    /**
     * Runs {@code changes} (many row updates, or a new list) and sorts once at the end instead of once per change: the
     * sorted list re-sorts all its rows on every update event. Meanwhile the shown order stays as it was (a comparator
     * that ties every pair keeps it, and costs one pass per change).
     */
    void batch(Runnable changes) {
        if (batching) {
            changes.run();
            return;
        }
        batching = true;
        sorted.setComparator((left, right) -> 0);
        try {
            changes.run();
        } finally {
            batching = false;
            sorted.setComparator(order);
        }
    }
}
