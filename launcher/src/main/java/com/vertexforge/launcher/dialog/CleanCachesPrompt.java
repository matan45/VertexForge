package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CacheFolder;

import java.util.List;
import java.util.Objects;

/**
 * What "Clean Caches…" asks before it deletes (VK-1695). Toolkit-free; {@link CleanCachesDialog} shows it.
 *
 * @param projectName the project being cleaned
 * @param engineLabel the engine whose {@code engine.json} named the folders
 * @param folders     every folder the engine names, as found
 */
public record CleanCachesPrompt(String projectName, String engineLabel, List<CacheFolder> folders) {

    public static final String TITLE = "Clean Caches";

    public CleanCachesPrompt {
        Objects.requireNonNull(projectName, "projectName");
        Objects.requireNonNull(engineLabel, "engineLabel");
        folders = List.copyOf(folders);
    }

    /** The folders that will be deleted. */
    public List<CacheFolder> present() {
        return folders.stream().filter(folder -> folder.state() == CacheFolder.State.PRESENT).toList();
    }

    /** The folders left alone, with why. */
    public List<CacheFolder> skipped() {
        return folders.stream().filter(folder -> folder.state() == CacheFolder.State.SKIPPED).toList();
    }

    /** The size of everything that will be deleted. */
    public long totalBytes() {
        return present().stream().mapToLong(CacheFolder::bytes).sum();
    }

    public String header() {
        return "Clean the caches of \"" + projectName + "\"?";
    }

    /** One line per folder that will be deleted: {@code assets/scripts/compiled — 1.2 MB (3 files)}. */
    public List<String> presentLines() {
        return present().stream().map(folder -> folder.relative() + "  —  " + JobMonitor.formatBytes(folder.bytes())
                + " (" + folder.files() + (folder.files() == 1 ? " file)" : " files)")).toList();
    }

    /** One line per folder left alone: {@code x: it is a link; what it points to is left alone}. */
    public List<String> skippedLines() {
        return skipped().stream().map(folder -> folder.relative() + ": " + folder.reason()).toList();
    }

    /** What deleting them means. */
    public String explanation() {
        return engineLabel + " rebuilds these folders, so they are deleted for good, not moved to the Recycle Bin "
                + "(" + JobMonitor.formatBytes(totalBytes()) + " in all). The editor builds the scripts again before "
                + "Play; an export with \"Build scripts before export\" turned off asks you to build them first.";
    }
}
