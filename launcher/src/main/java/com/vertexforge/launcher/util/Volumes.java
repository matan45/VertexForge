package com.vertexforge.launcher.util;

import java.nio.file.Files;
import java.nio.file.Path;

/** Questions about the volume a path lives on, shared by the project and engine checks (VK-1678, VK-1681). */
public final class Volumes {

    private Volumes() {
        // Utility class - prevent instantiation
    }

    /**
     * Whether {@code file} is on a drive letter that does not exist right now (an unplugged USB drive, a removed
     * {@code subst}). Only drive-letter roots are checked: looking at a network share's root could block.
     */
    public static boolean isAbsentDriveLetter(Path file) {
        Path root = file.getRoot();
        if (root == null) {
            return false;
        }
        String text = root.toString();
        boolean driveLetter = text.length() == 3 && Character.isLetter(text.charAt(0)) && text.charAt(1) == ':'
                && (text.charAt(2) == '\\' || text.charAt(2) == '/');
        return driveLetter && !Files.exists(root);
    }
}
