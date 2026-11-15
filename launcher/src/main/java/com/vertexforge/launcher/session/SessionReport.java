package com.vertexforge.launcher.session;

import com.vertexforge.launcher.session.CrashReports.CrashReport;

import java.nio.file.Path;
import java.time.Instant;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * How a supervised editor ended (VK-1683), with what the failure dialog shows.
 *
 * @param outcome          the classified end
 * @param exitCode         the process exit code
 * @param lastStatus       the last status line the editor wrote, when it wrote one
 * @param logTail          the last lines of the per-launch log (only gathered for a failure)
 * @param crashReport      the crash report it wrote, when it wrote one during the session
 * @param log              the per-launch log
 * @param workingDirectory the folder the editor ran in
 * @param exitedAt         when the launcher saw it exit
 */
public record SessionReport(
        SessionOutcome outcome,
        int exitCode,
        Optional<LaunchStatus> lastStatus,
        List<String> logTail,
        Optional<CrashReport> crashReport,
        Path log,
        Path workingDirectory,
        Instant exitedAt
) {
    public SessionReport {
        Objects.requireNonNull(outcome, "outcome");
        Objects.requireNonNull(lastStatus, "lastStatus");
        logTail = List.copyOf(logTail);
        Objects.requireNonNull(crashReport, "crashReport");
        Objects.requireNonNull(log, "log");
        Objects.requireNonNull(workingDirectory, "workingDirectory");
        Objects.requireNonNull(exitedAt, "exitedAt");
    }

    /** The editor's crash folder ({@code <working directory>/crashes}). */
    public Path crashFolder() {
        return CrashReports.folder(workingDirectory);
    }
}
