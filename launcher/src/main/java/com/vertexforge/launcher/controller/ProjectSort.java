package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.settings.LauncherState.SortKey;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The project list's remembered sort (VK-1694): the stable keys its columns are saved under, so a column renamed on
 * screen keeps its saved sort.
 */
final class ProjectSort {

    static final String NAME = "name";
    static final String STATUS = "status";
    static final String ENGINE = "engine";
    static final String PATH = "path";
    static final String LAST_OPENED = "lastOpened";

    /** Every key, in the table's column order. */
    static final List<String> KEYS = List.of(NAME, STATUS, ENGINE, PATH, LAST_OPENED);

    private ProjectSort() {
        // Utility class - prevent instantiation
    }

    /** {@code keys} without unknown columns and without a column named twice (its first place counts). */
    static List<SortKey> sanitize(List<SortKey> keys) {
        Set<String> seen = new HashSet<>();
        List<SortKey> kept = new ArrayList<>();
        for (SortKey key : keys) {
            if (KEYS.contains(key.column()) && seen.add(key.column())) {
                kept.add(key);
            }
        }
        return List.copyOf(kept);
    }
}
