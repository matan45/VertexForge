package com.vertexforge.launcher.util;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Whether one path lies in another, by their texts alone, compared as Windows compares names: name by name and
 * ignoring case (VK-1690, made public for VK-1695). Pure: no file-system access, so it follows no link; callers that
 * must see through links compare real paths as well.
 */
public final class PathContainment {

    private PathContainment() {
        // Utility class - prevent instantiation
    }

    /**
     * The names of {@code path} after those of {@code base} when {@code path} is {@code base} or lies in it, compared
     * name by name and ignoring case; {@code null} otherwise. Both are absolute and normalized.
     */
    public static List<String> below(Path base, Path path) {
        Path baseRoot = base.getRoot();
        Path pathRoot = path.getRoot();
        if (baseRoot == null || pathRoot == null || !baseRoot.toString().equalsIgnoreCase(pathRoot.toString())
                || path.getNameCount() < base.getNameCount()) {
            return null;
        }
        for (int i = 0; i < base.getNameCount(); i++) {
            if (!base.getName(i).toString().equalsIgnoreCase(path.getName(i).toString())) {
                return null;
            }
        }
        List<String> below = new ArrayList<>();
        for (int i = base.getNameCount(); i < path.getNameCount(); i++) {
            below.add(path.getName(i).toString());
        }
        return below;
    }

    /** Whether {@code path} is {@code base} or lies in it ({@link #below}). */
    public static boolean isWithin(Path base, Path path) {
        return below(base, path) != null;
    }
}
