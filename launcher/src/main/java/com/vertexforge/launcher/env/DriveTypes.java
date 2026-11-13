package com.vertexforge.launcher.env;

import java.nio.file.Path;
import java.util.Locale;

/**
 * Tells a local drive from a network one (VK-1679), for the "this folder is on a network drive" warning. A UNC path
 * needs no help; a mapped drive letter ({@code Z:\}) does, and Java exposes no API for it.
 */
@FunctionalInterface
public interface DriveTypes {

    enum Kind {
        LOCAL,
        REMOTE,
        UNKNOWN
    }

    /**
     * The kind of drive {@code root} is ({@code C:\}, {@code \\server\share\}). Never throws: anything that cannot be
     * determined is {@link Kind#UNKNOWN}, which shows no warning.
     */
    Kind kindOf(Path root);

    /** Knows nothing: every drive is {@link Kind#UNKNOWN}. */
    static DriveTypes unknown() {
        return root -> Kind.UNKNOWN;
    }

    /** {@code GetDriveTypeW} on Windows, looked up on first use; {@link #unknown()} elsewhere. */
    static DriveTypes system() {
        boolean windows = System.getProperty("os.name", "").toLowerCase(Locale.ROOT).startsWith("windows");
        return windows ? new WindowsDriveTypes() : unknown();
    }
}
