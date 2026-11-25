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
import java.util.OptionalLong;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The editor's crash reports (VK-1683). The editor's crash handler writes {@code crash_YYYYMMDD_HHMMSS.dmp} and a
 * readable {@code .txt} beside it ({@code CrashHandler.cpp}).
 * <p>
 * An editor older than VK-1732 writes them to {@code crashes/} in its working directory ({@link #folder}). Since
 * VK-1732 the editor names its crash folder in its launch-status line ({@link LaunchStatus#crashDir}); that folder
 * may be per user ({@code ~/.vertexforge/editor/crashes}) and shared by every editor the user runs, so the names also
 * carry the editor's process id: {@code crash_YYYYMMDD_HHMMSS_<pid>.txt}. Either folder can hold other editors'
 * reports (several projects can share one engine's working directory), so only a report written since a session
 * started can be its own, and one tagged with another process id never is ({@link #newestIn}).
 * <p>
 * An editor of VK-1684 or later writes the report in UTF-8; an older one wrote module and source paths in the ANSI
 * code page. Each line is decoded on its own ({@link EditorText#decodeLines}).
 */
public final class CrashReports {

    public static final String DIRECTORY_NAME = "crashes";
    /** The report's stamp has whole seconds and file times may lag a little; a session never starts that close. */
    static final Duration CLOCK_SLACK = Duration.ofSeconds(1);
    static final int MAX_REPORT_BYTES = 64 * 1024;
    /**
     * A readable report's name, with the writer's process id since VK-1732 (group 1, decimal). Case is ignored, as
     * Windows ignores it in file names.
     */
    static final Pattern REPORT_NAME = Pattern.compile("crash_\\d{8}_\\d{6}(?:_(\\d+))?\\.txt",
            Pattern.CASE_INSENSITIVE);

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

    /** The crash folder of an editor older than VK-1732 that runs in {@code workingDirectory}. */
    public static Path folder(Path workingDirectory) {
        return workingDirectory.resolve(DIRECTORY_NAME);
    }

    /**
     * {@link #newestIn} the crash folder of an editor that runs in {@code workingDirectory}, for any process id.
     *
     * @param fallback the charset of a line that is not valid UTF-8 (the ANSI code page)
     */
    public static Optional<CrashReport> newestSince(Path workingDirectory, Instant since, Charset fallback) {
        return newestIn(folder(workingDirectory), since, OptionalLong.empty(), fallback);
    }

    /**
     * The newest readable report in {@code folder} written at or after {@code since} (less {@link #CLOCK_SLACK});
     * empty when there is none or the folder cannot be read. Only names that match {@link #REPORT_NAME} count. Blocks
     * on the file system.
     * <p>
     * With a {@code pid}: the newest report tagged with that process id; when none is, the newest untagged report (an
     * editor older than VK-1732 tags none); a report tagged with another process id never counts. Without one, every
     * report counts. Equal times go to the greater name.
     *
     * @param pid      the editor's process id, when it is known
     * @param fallback the charset of a line that is not valid UTF-8 (the ANSI code page)
     */
    public static Optional<CrashReport> newestIn(Path folder, Instant since, OptionalLong pid, Charset fallback) {
        Instant earliest = since.minus(CLOCK_SLACK);
        // Reports tagged with the editor's pid; the rest are untagged ones, or every report when the pid is unknown.
        Newest own = new Newest();
        Newest rest = new Newest();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(folder, "crash_*.txt")) {
            for (Path report : stream) {
                Matcher name = REPORT_NAME.matcher(report.getFileName().toString());
                if (!name.matches()) {
                    continue;
                }
                Newest bucket;
                if (pid.isEmpty() || name.group(1) == null) {
                    bucket = rest;
                } else if (taggedWith(name.group(1), pid.getAsLong())) {
                    bucket = own;
                } else {
                    continue;
                }
                FileTime time = Files.getLastModifiedTime(report);
                if (time.toInstant().isBefore(earliest)) {
                    continue;
                }
                bucket.offer(report, time);
            }
        } catch (IOException | RuntimeException e) {
            return Optional.empty();
        }
        Path newest = own.report != null ? own.report : rest.report;
        if (newest == null) {
            return Optional.empty();
        }

        String content;
        try (InputStream in = Files.newInputStream(newest)) {
            byte[] bytes = in.readNBytes(MAX_REPORT_BYTES);
            content = EditorText.decodeLines(bytes, bytes.length, fallback);
        } catch (IOException | RuntimeException e) {
            content = "(the report could not be read: " + e.getMessage() + ")";
        }
        String name = newest.getFileName().toString();
        Path dump = newest.resolveSibling(name.substring(0, name.length() - ".txt".length()) + ".dmp");
        return Optional.of(new CrashReport(newest, content, Files.isRegularFile(dump) ? Optional.of(dump)
                : Optional.empty()));
    }

    /** Whether a name's decimal tag is {@code pid}; a tag too long for a {@code long} is nobody's. */
    private static boolean taggedWith(String tag, long pid) {
        try {
            return Long.parseLong(tag) == pid;
        } catch (NumberFormatException e) {
            return false;
        }
    }

    /** The newest report offered so far; equal times go to the greater name. */
    private static final class Newest {
        private Path report;
        private FileTime time;

        void offer(Path candidate, FileTime candidateTime) {
            if (report == null || candidateTime.compareTo(time) > 0
                    || (candidateTime.compareTo(time) == 0
                    && candidate.getFileName().toString().compareTo(report.getFileName().toString()) > 0)) {
                report = candidate;
                time = candidateTime;
            }
        }
    }
}
