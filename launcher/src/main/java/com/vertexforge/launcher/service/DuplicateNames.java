package com.vertexforge.launcher.service;

import java.util.Objects;
import java.util.function.Predicate;

/**
 * The name a duplicate is offered (VK-1695): {@code <Folder>-copy}, then {@code <Folder>-copy-2} … {@code -99}, the
 * first one {@code taken} says is free. The user may type any other; the copier checks again. Pure.
 */
final class DuplicateNames {

    static final String SUFFIX = "-copy";
    static final int MAX_NUMBER = 99;
    /** The base of last resort, for a folder and a project name that both break the name rules. */
    static final String FALLBACK_BASE = "Project";

    private DuplicateNames() {
        // Utility class - prevent instantiation
    }

    /**
     * The suggested name.
     *
     * @param folderName  the original's folder name: the usual base
     * @param projectName the original's {@code projectName}: the base when the folder's would make an invalid name
     * @param taken       whether a name is already used beside the original (asked of the file system by the caller)
     */
    static String suggest(String folderName, String projectName, Predicate<String> taken) {
        Objects.requireNonNull(taken, "taken");
        String base = usable(folderName) ? folderName : usable(projectName) ? projectName : FALLBACK_BASE;
        String first = base + SUFFIX;
        if (!taken.test(first)) {
            return first;
        }
        for (int number = 2; number <= MAX_NUMBER; number++) {
            String candidate = first + "-" + number;
            if (!taken.test(candidate)) {
                return candidate;
            }
        }
        // All taken: the dialog shows the copier's refusal for this one, and the user types another.
        return first;
    }

    private static boolean usable(String base) {
        return base != null && ProjectNameRules.checkName(base + SUFFIX).isEmpty();
    }
}
