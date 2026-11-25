package com.vertexforge.launcher.env;

import java.nio.file.Path;
import java.util.List;
import java.util.Locale;

/**
 * The user's own folders as Windows knows them (VK-1695): a project that IS one of them (someone picked Documents as
 * the project folder) must never be offered for the Recycle Bin, because the user's files would go with it.
 * {@code user.home} alone is not enough: OneDrive's folder backup moves Desktop and Documents into the OneDrive folder,
 * and the user can move Downloads anywhere.
 */
@FunctionalInterface
public interface KnownFolders {

    /**
     * The user's profile, Desktop, Documents, Downloads, Pictures, Music, Videos and Saved Games as Windows reports
     * them (follows OneDrive redirection); empty when unknown. Never throws.
     */
    List<Path> userFolders();

    /** Knows no folders. */
    static KnownFolders none() {
        return List::of;
    }

    /** {@code SHGetKnownFolderPath} on Windows, looked up on first use; {@link #none()} elsewhere. */
    static KnownFolders system() {
        boolean windows = System.getProperty("os.name", "").toLowerCase(Locale.ROOT).startsWith("windows");
        return windows ? new WindowsKnownFolders() : none();
    }
}
