package com.vertexforge.launcher.project;

import java.nio.file.Path;

/**
 * What makes a file name a VertexForge project file (VK-1685). The extension is compared without regard to letter
 * case everywhere, so {@code Game.VFPROJ} is a project like {@code Game.vfproj}. Nothing here touches the disk.
 */
public final class ProjectFiles {

    /** The project file's extension, as the editor and the launcher write it. */
    public static final String EXTENSION = ".vfproj";

    private ProjectFiles() {
        // Utility class - prevent instantiation
    }

    /**
     * Whether {@code fileName} ends with {@link #EXTENSION} in any letter case and has a name before it, so
     * {@code ".vfproj"} on its own is not a project file. {@code null} is not one either.
     */
    public static boolean isProjectFileName(String fileName) {
        if (fileName == null) {
            return false;
        }
        int start = fileName.length() - EXTENSION.length();
        return start > 0 && fileName.regionMatches(true, start, EXTENSION, 0, EXTENSION.length());
    }

    /** Whether the last element of {@code path} is a project file name; looks at the name only, never the disk. */
    public static boolean isProjectFile(Path path) {
        if (path == null) {
            return false;
        }
        Path fileName = path.getFileName();
        return fileName != null && isProjectFileName(fileName.toString());
    }

    /**
     * The name without its project extension ({@code Game.VFPROJ} gives {@code Game}); any other name, and
     * {@code null}, comes back unchanged.
     */
    public static String stem(String fileName) {
        return isProjectFileName(fileName)
                ? fileName.substring(0, fileName.length() - EXTENSION.length())
                : fileName;
    }
}
