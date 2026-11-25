package com.vertexforge.launcher.viewmodel;

import java.util.Comparator;

/**
 * The project list's order (VK-1694): pinned projects first, whatever the sort; within each group the columns the user
 * sorted by, and then the list's own order (most recently opened first), so that rows that tie keep their place.
 */
public final class ProjectOrder {

    /** Pinned rows before unpinned ones. */
    private static final Comparator<ProjectViewModel> PINNED_FIRST =
            (left, right) -> Boolean.compare(right.isPinned(), left.isPinned());
    private static final Comparator<ProjectViewModel> LIST_ORDER =
            Comparator.comparingInt(ProjectViewModel::getDisplayIndex);

    private ProjectOrder() {
        // Utility class - prevent instantiation
    }

    /**
     * Pinned rows first, then {@code columns} (the table's own comparator, which already applies each column's
     * direction), then the list's own order.
     *
     * @param columns the table's column order, or {@code null} when no column is sorted
     */
    public static Comparator<ProjectViewModel> pinnedFirst(Comparator<ProjectViewModel> columns) {
        Comparator<ProjectViewModel> order = columns == null ? PINNED_FIRST : PINNED_FIRST.thenComparing(columns);
        return order.thenComparing(LIST_ORDER);
    }
}
