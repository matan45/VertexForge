package com.vertexforge.launcher.session;

import java.nio.file.Path;
import java.time.Instant;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * An editor that has just been started (VK-1683): everything its session needs to supervise it.
 *
 * @param process          the editor process
 * @param project          the project file it opens
 * @param projectName      the name shown in messages
 * @param log              the per-launch log its stdout and stderr go to
 * @param statusFile       its {@code --launch-status} file; empty when the path could not be passed to the editor
 * @param statusOmitted    why no status file was passed, when it was not
 * @param workingDirectory the folder it runs in (its {@code crashes/} folder lives there)
 * @param contractKnown    whether its engine advertises a launch contract ({@code launchContract} in engine.json)
 * @param command          the command line it was started with (for the failure details)
 * @param startedAt        when it was started
 */
public record SessionStart(
        Process process,
        Path project,
        String projectName,
        Path log,
        Optional<Path> statusFile,
        Optional<String> statusOmitted,
        Path workingDirectory,
        boolean contractKnown,
        List<String> command,
        Instant startedAt
) {
    public SessionStart {
        Objects.requireNonNull(process, "process");
        Objects.requireNonNull(project, "project");
        Objects.requireNonNull(projectName, "projectName");
        Objects.requireNonNull(log, "log");
        Objects.requireNonNull(statusFile, "statusFile");
        Objects.requireNonNull(statusOmitted, "statusOmitted");
        Objects.requireNonNull(workingDirectory, "workingDirectory");
        command = List.copyOf(command);
        Objects.requireNonNull(startedAt, "startedAt");
    }
}
