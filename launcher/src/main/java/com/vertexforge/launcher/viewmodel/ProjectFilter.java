package com.vertexforge.launcher.viewmodel;

import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.function.Predicate;

/**
 * The project list's search (VK-1694): a project matches when every word of the query occurs in its name, its path or
 * its engine, ignoring case. A blank query matches every project.
 */
public final class ProjectFilter {

    private ProjectFilter() {
        // Utility class - prevent instantiation
    }

    /**
     * The rows {@code query} keeps. Each call returns a new predicate, so a {@code FilteredList} given it always
     * filters again.
     */
    public static Predicate<ProjectViewModel> of(String query) {
        List<String> words = words(query);
        return row -> matches(row, words);
    }

    /** The query's words, lower-cased the same way on every machine. */
    static List<String> words(String query) {
        if (query == null || query.isBlank()) {
            return List.of();
        }
        return Arrays.stream(query.strip().split("\\s+"))
                .map(word -> word.toLowerCase(Locale.ROOT))
                .toList();
    }

    /** Whether every word occurs in the row's name, path text or engine text. */
    static boolean matches(ProjectViewModel row, List<String> words) {
        if (words.isEmpty()) {
            return true;
        }
        String name = lower(row.getName());
        String path = lower(row.getPath());
        String engine = lower(row.getEngineText());
        for (String word : words) {
            if (!name.contains(word) && !path.contains(word) && !engine.contains(word)) {
                return false;
            }
        }
        return true;
    }

    private static String lower(String text) {
        return text == null ? "" : text.toLowerCase(Locale.ROOT);
    }
}
