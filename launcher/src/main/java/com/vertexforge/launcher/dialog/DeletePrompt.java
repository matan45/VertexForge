package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.service.JobMonitor;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;

/**
 * What "Move to Recycle Bin…" asks (VK-1695): the user types the project's name to confirm. Toolkit-free;
 * {@link DeleteProjectDialog} shows it.
 *
 * @param projectName the name the user must type, exactly
 * @param folder      the folder that goes to the Recycle Bin
 * @param bytes       the size of its files
 * @param files       how many files it holds
 * @param folders     how many folders it holds
 * @param notes       what the user should know before confirming
 */
public record DeletePrompt(String projectName, Path folder, long bytes, int files, int folders, List<String> notes) {

    public static final String TITLE = "Move to Recycle Bin";

    public DeletePrompt {
        Objects.requireNonNull(projectName, "projectName");
        Objects.requireNonNull(folder, "folder");
        notes = List.copyOf(notes);
    }

    /** Whether {@code typed} is the project's name, exactly: no trimming, no change of case. */
    public boolean matches(String typed) {
        return projectName.equals(typed);
    }

    public String header() {
        return "Move \"" + projectName + "\" to the Recycle Bin?";
    }

    /** {@code 120 files in 14 folders, 1.2 GB}. */
    public String summary() {
        return count(files, "file") + " in " + count(folders, "folder") + ", " + JobMonitor.formatBytes(bytes);
    }

    /** How to get the project back. */
    public String restoreHint() {
        return "To get it back, restore the folder from the Recycle Bin, then add it to the list again with Add "
                + "Existing….";
    }

    public String confirmLabel() {
        return "Type the project's name, " + projectName + ", to confirm:";
    }

    private static String count(int count, String noun) {
        return count + " " + noun + (count == 1 ? "" : "s");
    }
}
