package com.vertexforge.launcher.env;

import java.nio.file.Path;
import java.util.Locale;

/**
 * Tells what kind of drive a folder is on, as Windows' {@code GetDriveTypeW} reports it: for the "this folder is on a
 * network drive" warning (VK-1679), and for telling the user why a folder on a removable, optical or network drive
 * may have no Recycle Bin (VK-1695). A UNC path needs no help to be recognized as remote; a mapped drive letter
 * ({@code Z:\}) does, and Java exposes no API for it.
 */
@FunctionalInterface
public interface DriveTypes {

    /** The drive kinds {@code GetDriveTypeW} tells apart. */
    enum Kind {
        /** A fixed drive ({@code DRIVE_FIXED}): an internal disk, the only kind that always has a Recycle Bin. */
        LOCAL,
        /** A network drive ({@code DRIVE_REMOTE}): a mapped letter or a UNC share. */
        REMOTE,
        /** A removable drive ({@code DRIVE_REMOVABLE}): a USB stick, a memory card. */
        REMOVABLE,
        /** An optical drive ({@code DRIVE_CDROM}). */
        OPTICAL,
        /** A RAM disk ({@code DRIVE_RAMDISK}). */
        RAM_DISK,
        /** Not known: no such root, the lookup failed, or a type newer than this list. */
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
