package com.vertexforge.launcher.util;

import java.io.IOException;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.time.Clock;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Objects;

/**
 * The launcher's error log ({@code ~/.vertexforge/launcher-error.log}) with a size cap and a few rotated files.
 * <p>
 * The packaged launcher has no console, so anything printed to {@code System.err} alone is lost: warnings go to
 * both the console and this file. Before an entry would push the file past {@code maxBytes}, the file is rotated
 * ({@code .1} is the newest backup, {@code .backups} the oldest; older ones are deleted), so the log never grows
 * past {@code maxBytes * (1 + backups)}.
 */
public final class LauncherLog {

    public static final String FILE_NAME = "launcher-error.log";
    public static final long DEFAULT_MAX_BYTES = 1024 * 1024;
    public static final int DEFAULT_BACKUPS = 3;
    static final long MIN_MAX_BYTES = 256;
    static final String TRUNCATED_MARKER = "[entry truncated to the log size cap]";

    private static final DateTimeFormatter DATE_FORMAT = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    private final Path file;
    private final Clock clock;
    private final long maxBytes;
    private final int backups;
    private final PrintStream console;

    public LauncherLog(Path file, Clock clock, long maxBytes, int backups, PrintStream console) {
        this.file = Objects.requireNonNull(file, "file");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.console = Objects.requireNonNull(console, "console");
        if (maxBytes < MIN_MAX_BYTES) {
            throw new IllegalArgumentException("maxBytes must be at least " + MIN_MAX_BYTES);
        }
        if (backups < 1) {
            throw new IllegalArgumentException("backups must be at least 1");
        }
        this.maxBytes = maxBytes;
        this.backups = backups;
    }

    /** The default log in the launcher's settings folder, echoing warnings to {@code System.err}. */
    public static LauncherLog forConfigDir(Path configDir, Clock clock) {
        return new LauncherLog(configDir.resolve(FILE_NAME), clock, DEFAULT_MAX_BYTES, DEFAULT_BACKUPS, System.err);
    }

    public Path file() {
        return file;
    }

    /** A problem the launcher recovers from: printed to the console as before, and kept in the log. */
    public synchronized void warn(String message) {
        console.println(message);
        StringWriter text = new StringWriter();
        try (PrintWriter writer = new PrintWriter(text)) {
            writeHeader(writer);
            writer.println("Warning: " + message);
            writer.println();
        }
        String failure = append(text.toString());
        if (failure != null) {
            console.println("Failed to write to error log: " + failure);
        }
    }

    /**
     * An unexpected failure with its stack trace. When the log cannot be written, the stack trace goes to the
     * console instead.
     */
    public synchronized void error(Throwable throwable) {
        StringWriter text = new StringWriter();
        try (PrintWriter writer = new PrintWriter(text)) {
            writeHeader(writer);
            throwable.printStackTrace(writer);
            writer.println();
        }
        String failure = append(text.toString());
        if (failure != null) {
            console.println("Failed to write to error log: " + failure);
            throwable.printStackTrace(console);
        }
    }

    private void writeHeader(PrintWriter writer) {
        writer.println("=".repeat(80));
        writer.println("Timestamp: " + LocalDateTime.now(clock).format(DATE_FORMAT));
        writer.println("Thread: " + Thread.currentThread().getName());
        writer.println("-".repeat(80));
    }

    /** Appends one entry, rotating first when needed. Returns {@code null} on success, else the reason. */
    private String append(String entry) {
        byte[] bytes = fitToCap(entry).getBytes(StandardCharsets.UTF_8);
        try {
            Path parent = file.getParent();
            if (parent != null) {
                Files.createDirectories(parent);
            }
            long size = Files.isRegularFile(file) ? Files.size(file) : 0;
            if (size > 0 && size + bytes.length > maxBytes) {
                rotate();
            }
            Files.write(file, bytes, StandardOpenOption.CREATE, StandardOpenOption.APPEND);
            return null;
        } catch (IOException e) {
            return e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
        }
    }

    private void rotate() {
        try {
            Files.deleteIfExists(backup(backups));
            for (int i = backups - 1; i >= 1; i--) {
                Path from = backup(i);
                if (Files.exists(from)) {
                    Files.move(from, backup(i + 1), StandardCopyOption.REPLACE_EXISTING);
                }
            }
            Files.move(file, backup(1), StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            // Another launcher may hold the file open (Windows). Keep appending rather than lose the entry.
            console.println("Could not rotate " + file + ": " + e.getMessage());
        }
    }

    Path backup(int index) {
        return file.resolveSibling(file.getFileName() + "." + index);
    }

    /** Cuts an entry that alone exceeds the cap, on a code-point boundary, and marks it as truncated. */
    private String fitToCap(String entry) {
        if (utf8Length(entry) <= maxBytes) {
            return entry;
        }
        String suffix = System.lineSeparator() + TRUNCATED_MARKER + System.lineSeparator();
        long budget = maxBytes - utf8Length(suffix);
        StringBuilder kept = new StringBuilder();
        long used = 0;
        for (int i = 0; i < entry.length(); ) {
            int codePoint = entry.codePointAt(i);
            int length = utf8Length(codePoint);
            if (used + length > budget) {
                break;
            }
            kept.appendCodePoint(codePoint);
            used += length;
            i += Character.charCount(codePoint);
        }
        return kept.append(suffix).toString();
    }

    private static long utf8Length(String text) {
        return text.getBytes(StandardCharsets.UTF_8).length;
    }

    /** Upper bound of the UTF-8 size of one code point (a lone surrogate encodes as one byte, '?'). */
    private static int utf8Length(int codePoint) {
        if (codePoint < 0x80) {
            return 1;
        }
        if (codePoint < 0x800) {
            return 2;
        }
        return codePoint < 0x10000 ? 3 : 4;
    }
}
