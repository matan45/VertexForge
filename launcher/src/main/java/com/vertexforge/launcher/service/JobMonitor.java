package com.vertexforge.launcher.service;

import java.io.IOException;
import java.util.Locale;

/**
 * Watches a long file job (VK-1690: a project backup or copy): it hears how far the job got and can ask it to stop.
 * Every method is called on the thread that runs the job.
 */
public interface JobMonitor {

    /** What a job is doing. */
    enum Phase {
        /** Listing the files; {@code done} counts the entries seen, the total is {@link #UNKNOWN}. */
        SCANNING,
        /** Writing an archive; {@code done} and {@code total} count content bytes. */
        WRITING,
        /** Reading a written archive back; {@code done} and {@code total} count content bytes. */
        VERIFYING,
        /** Copying files; {@code done} and {@code total} count content bytes. */
        COPYING
    }

    /** The {@code total} of a phase whose size is not known yet. */
    long UNKNOWN = -1;

    /** Watches nothing and never cancels. */
    JobMonitor NONE = new JobMonitor() {
    };

    /**
     * Called as the job moves on: after each entry while scanning, after each chunk of 64 KiB or less otherwise.
     *
     * @param total how much there is to do in this phase, or {@link #UNKNOWN}; {@code done} never exceeds a known total
     * @throws IOException to make the job fail at this point (tests inject faults here); the job then removes what it
     *                     wrote, as for any other failure
     */
    default void progress(Phase phase, long done, long total) throws IOException {
    }

    /** Whether the user asked the job to stop. */
    default boolean cancelRequested() {
        return false;
    }

    /** Throws when the user asked the job to stop: the job's loops call it after every step. */
    default void checkCancelled() throws JobCancelledException {
        if (cancelRequested()) {
            throw new JobCancelledException();
        }
    }

    /**
     * {@code 1.2 GB}: a byte count as progress text and messages show it, in the 1024-based units Windows Explorer
     * uses, with one decimal from KB on.
     */
    static String formatBytes(long bytes) {
        if (bytes < 1024) {
            return bytes == 1 ? "1 byte" : bytes + " bytes";
        }
        String[] units = {"KB", "MB", "GB", "TB", "PB", "EB"};
        double value = bytes;
        int unit = -1;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            unit++;
        }
        // 1023.96 KB would print as "1024.0 KB": move on to the next unit instead.
        if (value >= 1023.95 && unit < units.length - 1) {
            value /= 1024;
            unit++;
        }
        return String.format(Locale.ROOT, "%.1f %s", value, units[unit]);
    }
}
