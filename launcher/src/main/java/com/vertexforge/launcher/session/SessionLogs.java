package com.vertexforge.launcher.session;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.FileTime;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Deque;
import java.util.List;
import java.util.Objects;
import java.util.function.Consumer;

/**
 * The per-launch logs in {@code ~/.vertexforge/logs/} (VK-1683). Each launch gets
 * {@code <project>-<UTC time>.log}, which receives the editor's stdout and stderr through a handle the editor owns
 * (so the log is complete even when the launcher exits first), and {@code <same name>.status.json}, the editor's
 * {@code --launch-status} file. The launcher writes a header before the start and a footer after the exit.
 * <p>
 * Only the newest {@link #KEEP} logs are kept. A running editor's log cannot be deleted (its handle does not share
 * delete access), so pruning never touches a live session.
 */
public final class SessionLogs {

    public static final String DIRECTORY_NAME = "logs";
    public static final int KEEP = 20;
    public static final String LOG_SUFFIX = ".log";
    public static final String STATUS_SUFFIX = ".status.json";
    /** The longest part of a file name taken from the project's name. */
    static final int MAX_NAME_LENGTH = 40;
    /** How much of a log's end {@link #tail} reads at most. */
    static final int TAIL_BYTES = 64 * 1024;
    private static final int MAX_COLLISIONS = 100;

    private static final DateTimeFormatter STAMP =
            DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmssSSS'Z'").withZone(ZoneOffset.UTC);
    private static final String NEWLINE = "\r\n";

    /**
     * The files of one launch.
     *
     * @param log    the log the editor's output goes to; already created (empty)
     * @param status where the editor writes its status; not created (the editor refuses a file it cannot replace)
     */
    public record LaunchFiles(Path log, Path status) {
    }

    private final Path directory;
    private final Consumer<String> warn;

    /**
     * @param directory {@code ~/.vertexforge/logs}
     * @param warn      receives problems pruning recovers from (normally {@code LauncherLog::warn})
     */
    public SessionLogs(Path directory, Consumer<String> warn) {
        this.directory = Objects.requireNonNull(directory, "directory").toAbsolutePath().normalize();
        this.warn = Objects.requireNonNull(warn, "warn");
    }

    public Path directory() {
        return directory;
    }

    /**
     * Creates the folder and a new, empty log for a launch of {@code projectName} at {@code now}. Two launches in the
     * same millisecond get {@code -2}, {@code -3}, ... so no launch ever reuses another one's files.
     */
    public LaunchFiles create(String projectName, Instant now) throws IOException {
        Files.createDirectories(directory);
        String base = fileNamePart(projectName) + "-" + STAMP.format(now);
        for (int attempt = 1; attempt <= MAX_COLLISIONS; attempt++) {
            String name = attempt == 1 ? base : base + "-" + attempt;
            Path log = directory.resolve(name + LOG_SUFFIX);
            try {
                Files.createFile(log);
            } catch (FileAlreadyExistsException e) {
                continue;
            }
            // A status file whose log was deleted by hand must not pass for this launch's.
            Path status = directory.resolve(name + STATUS_SUFFIX);
            Files.deleteIfExists(status);
            return new LaunchFiles(log, status);
        }
        throw new IOException("Could not find a free log name for " + base + " in " + directory);
    }

    /**
     * {@code projectName} reduced to {@code [A-Za-z0-9_-]}: every other run of characters becomes one {@code _}, so
     * the name is plain ASCII (any code page can pass it to the editor), has no dot (never a reserved device name such
     * as {@code CON}) and no comma (Explorer splits its arguments at commas).
     */
    static String fileNamePart(String projectName) {
        StringBuilder out = new StringBuilder();
        boolean pendingSeparator = false;
        String text = projectName == null ? "" : projectName;
        for (int i = 0; i < text.length() && out.length() < MAX_NAME_LENGTH; i++) {
            char c = text.charAt(i);
            boolean allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                    || c == '_' || c == '-';
            if (allowed) {
                if (pendingSeparator && !out.isEmpty()) {
                    out.append('_');
                }
                pendingSeparator = false;
                out.append(c);
            } else {
                pendingSeparator = true;
            }
        }
        if (out.length() > MAX_NAME_LENGTH) {
            out.setLength(MAX_NAME_LENGTH);
        }
        return out.isEmpty() ? "project" : out.toString();
    }

    /** Writes the header lines (with {@code \r\n}) before the editor starts. */
    public static void writeHeader(Path log, List<String> lines) throws IOException {
        append(log, lines);
    }

    /** Appends the footer lines after the editor exited (or failed to start). */
    public static void appendFooter(Path log, List<String> lines) throws IOException {
        append(log, lines);
    }

    private static void append(Path log, List<String> lines) throws IOException {
        StringBuilder text = new StringBuilder();
        for (String line : lines) {
            text.append(line).append(NEWLINE);
        }
        Files.writeString(log, text, StandardCharsets.UTF_8, StandardOpenOption.APPEND);
    }

    /**
     * Deletes all but the newest {@code keep} logs (by last change), each with its status file and the editor's
     * leftover temp files ({@code <status>.<pid>.tmp}, {@code <status>.<pid>.crash.tmp}). A log that cannot be deleted
     * (its editor still runs) is skipped together with its status file. Never throws.
     */
    public void prune(int keep) {
        List<Path> logs = new ArrayList<>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(directory, "*" + LOG_SUFFIX)) {
            for (Path log : stream) {
                logs.add(log);
            }
        } catch (IOException | RuntimeException e) {
            warn.accept("Could not list the launch logs in " + directory + ": " + e.getMessage());
            return;
        }
        if (logs.size() <= keep) {
            return;
        }
        logs.sort(Comparator.comparing(SessionLogs::lastModified).reversed());
        for (Path log : logs.subList(keep, logs.size())) {
            try {
                Files.delete(log);
            } catch (IOException e) {
                // Still written by a running editor (or locked by a viewer): kept, with its status file.
                continue;
            }
            String base = log.getFileName().toString();
            base = base.substring(0, base.length() - LOG_SUFFIX.length());
            deleteQuietly(directory.resolve(base + STATUS_SUFFIX));
            try (DirectoryStream<Path> leftovers =
                         Files.newDirectoryStream(directory, globEscape(base + STATUS_SUFFIX) + ".*")) {
                for (Path leftover : leftovers) {
                    deleteQuietly(leftover);
                }
            } catch (IOException | RuntimeException e) {
                warn.accept("Could not clean up after the launch log " + log + ": " + e.getMessage());
            }
        }
    }

    private void deleteQuietly(Path file) {
        try {
            Files.deleteIfExists(file);
        } catch (IOException e) {
            warn.accept("Could not delete the old launch file " + file + ": " + e.getMessage());
        }
    }

    /** Escapes the glob special characters (names are {@code [A-Za-z0-9_-]} plus the suffixes, so this is a guard). */
    private static String globEscape(String text) {
        StringBuilder out = new StringBuilder();
        for (char c : text.toCharArray()) {
            if ("*?[]{}\\,".indexOf(c) >= 0) {
                out.append('\\');
            }
            out.append(c);
        }
        return out.toString();
    }

    private static FileTime lastModified(Path file) {
        try {
            return Files.getLastModifiedTime(file);
        } catch (IOException e) {
            return FileTime.fromMillis(0);
        }
    }

    /**
     * The last {@code count} lines of {@code log}, read from at most its last {@link #TAIL_BYTES} bytes. Each line is
     * decoded as UTF-8, or in {@code fallback} (the ANSI code page) when it is not valid UTF-8: most editor output is
     * UTF-8, but some lines carry paths in the code page. Empty when the file cannot be read.
     */
    public static List<String> tail(Path log, int count, Charset fallback) {
        byte[] bytes;
        boolean fromStart;
        try (RandomAccessFile file = new RandomAccessFile(log.toFile(), "r")) {
            long length = file.length();
            long start = Math.max(0, length - TAIL_BYTES);
            if (start == 0) {
                fromStart = true;
            } else {
                // Starting right after a newline means the first line is whole.
                file.seek(start - 1);
                fromStart = file.read() == '\n';
            }
            bytes = new byte[(int) (length - start)];
            file.seek(start);
            file.readFully(bytes);
        } catch (IOException | RuntimeException e) {
            return List.of();
        }

        Deque<String> lines = new ArrayDeque<>();
        int lineStart = 0;
        boolean first = true;
        for (int i = 0; i <= bytes.length; i++) {
            if (i == bytes.length || bytes[i] == '\n') {
                int end = i;
                if (end > lineStart && bytes[end - 1] == '\r') {
                    end--;
                }
                // A read that started mid-file starts mid-line: that first piece is dropped.
                boolean partialFirst = first && !fromStart;
                boolean trailingEmpty = i == bytes.length && end == lineStart;
                if (!partialFirst && !trailingEmpty) {
                    lines.addLast(decode(bytes, lineStart, end, fallback));
                    if (lines.size() > count) {
                        lines.removeFirst();
                    }
                }
                first = false;
                lineStart = i + 1;
            }
        }
        return List.copyOf(lines);
    }

    private static String decode(byte[] bytes, int from, int to, Charset fallback) {
        ByteBuffer buffer = ByteBuffer.wrap(bytes, from, to - from);
        try {
            return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(buffer)
                    .toString();
        } catch (CharacterCodingException e) {
            return new String(bytes, from, to - from, fallback);
        }
    }
}
