package com.vertexforge.launcher.session;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.time.Duration;
import java.time.Instant;
import java.util.Optional;

/**
 * The editor's crash reports (VK-1683). The editor's crash handler writes {@code crashes/crash_YYYYMMDD_HHMMSS.dmp}
 * and a readable {@code .txt} beside it, relative to its working directory ({@code CrashHandler.cpp}). Several
 * projects can share one engine's working directory, so only a report written since a session started is its own.
 */
public final class CrashReports {

    public static final String DIRECTORY_NAME = "crashes";
    /** The report's stamp has whole seconds and file times may lag a little; a session never starts that close. */
    static final Duration CLOCK_SLACK = Duration.ofSeconds(1);
    static final int MAX_REPORT_BYTES = 64 * 1024;

    /**
     * @param text    the readable report file ({@code .txt})
     * @param content its text, cut at {@link #MAX_REPORT_BYTES}
     * @param dump    the minidump next to it, when it exists
     */
    public record CrashReport(Path text, String content, Optional<Path> dump) {
    }

    private CrashReports() {
        // Utility class - prevent instantiation
    }

    /** The crash folder of an editor that runs in {@code workingDirectory}. */
    public static Path folder(Path workingDirectory) {
        return workingDirectory.resolve(DIRECTORY_NAME);
    }

    /**
     * The newest {@code crash_*.txt} in the editor's crash folder written at or after {@code since} (less
     * {@link #CLOCK_SLACK}); empty when there is none or the folder cannot be read. Blocks on the file system.
     */
    public static Optional<CrashReport> newestSince(Path workingDirectory, Instant since, Charset charset) {
        Path folder = folder(workingDirectory);
        Instant earliest = since.minus(CLOCK_SLACK);
        Path newest = null;
        FileTime newestTime = null;
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(folder, "crash_*.txt")) {
            for (Path report : stream) {
                FileTime time = Files.getLastModifiedTime(report);
                if (time.toInstant().isBefore(earliest)) {
                    continue;
                }
                if (newest == null || time.compareTo(newestTime) > 0
                        || (time.compareTo(newestTime) == 0
                        && report.getFileName().toString().compareTo(newest.getFileName().toString()) > 0)) {
                    newest = report;
                    newestTime = time;
                }
            }
        } catch (IOException | RuntimeException e) {
            return Optional.empty();
        }
        if (newest == null) {
            return Optional.empty();
        }

        String content;
        try (InputStream in = Files.newInputStream(newest)) {
            content = new String(in.readNBytes(MAX_REPORT_BYTES), charset);
        } catch (IOException | RuntimeException e) {
            content = "(the report could not be read: " + e.getMessage() + ")";
        }
        String name = newest.getFileName().toString();
        Path dump = newest.resolveSibling(name.substring(0, name.length() - ".txt".length()) + ".dmp");
        return Optional.of(new CrashReport(newest, content, Files.isRegularFile(dump) ? Optional.of(dump)
                : Optional.empty()));
    }
}
