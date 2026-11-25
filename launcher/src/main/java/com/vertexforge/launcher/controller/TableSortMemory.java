package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.settings.LauncherState.SortKey;
import javafx.animation.PauseTransition;
import javafx.collections.ListChangeListener;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;
import javafx.util.Duration;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.function.Consumer;

/**
 * Remembers how the user sorted the projects table (VK-1694). The saved sort is put back before the list is first
 * shown; a sort the user then chooses (clicking a header, shift-clicking for more columns, or a click that turns a sort
 * off) is saved half a second after the last click, so a run of clicks writes once. FX thread.
 */
final class TableSortMemory<S> {

    static final Duration SAVE_DELAY = Duration.millis(500);

    private final TableView<S> table;
    /** The columns by their stable keys ({@link ProjectSort}). */
    private final Map<String, TableColumn<S, ?>> columns;
    private final Consumer<List<SortKey>> saver;
    private final PauseTransition pending = new PauseTransition(SAVE_DELAY);
    private List<SortKey> saved = List.of();

    /**
     * @param columns the columns by their stable keys
     * @param saver   writes a sort; called on the FX thread, it hands the write to the store thread
     */
    TableSortMemory(TableView<S> table, Map<String, TableColumn<S, ?>> columns, Consumer<List<SortKey>> saver) {
        this.table = Objects.requireNonNull(table, "table");
        this.columns = new LinkedHashMap<>(columns);
        this.saver = Objects.requireNonNull(saver, "saver");
        pending.setOnFinished(event -> saveIfChanged());
    }

    /** Puts {@code keys} back (unknown columns are skipped), then starts following the user's sorting. */
    void restore(List<SortKey> keys) {
        List<TableColumn<S, ?>> order = new ArrayList<>();
        List<SortKey> applied = new ArrayList<>();
        for (SortKey key : ProjectSort.sanitize(keys)) {
            TableColumn<S, ?> column = columns.get(key.column());
            if (column != null && column.isSortable()) {
                column.setSortType(key.ascending() ? TableColumn.SortType.ASCENDING : TableColumn.SortType.DESCENDING);
                order.add(column);
                applied.add(key);
            }
        }
        table.getSortOrder().setAll(order);
        saved = List.copyOf(applied);

        table.getSortOrder().addListener((ListChangeListener<TableColumn<S, ?>>) change -> pending.playFromStart());
        for (TableColumn<S, ?> column : columns.values()) {
            column.sortTypeProperty().addListener((observable, was, now) -> pending.playFromStart());
        }
    }

    /** The table's sort as it is now. */
    List<SortKey> current() {
        List<SortKey> keys = new ArrayList<>();
        for (TableColumn<S, ?> column : table.getSortOrder()) {
            columns.entrySet().stream()
                    .filter(entry -> entry.getValue() == column)
                    .findFirst()
                    .ifPresent(entry -> keys.add(new SortKey(entry.getKey(),
                            column.getSortType() == TableColumn.SortType.ASCENDING)));
        }
        return List.copyOf(keys);
    }

    /** Saves a sort that is still waiting for its delay (the launcher is closing). */
    void saveNow() {
        pending.stop();
        saveIfChanged();
    }

    private void saveIfChanged() {
        List<SortKey> now = current();
        if (!now.equals(saved)) {
            saved = now;
            saver.accept(now);
        }
    }
}
