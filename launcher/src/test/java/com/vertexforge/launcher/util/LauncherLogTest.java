package com.vertexforge.launcher.util;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class LauncherLogTest {

    private static final long CAP = 1024;

    @TempDir
    Path tempDir;

    private CapturedLog cappedLog() {
        return new CapturedLog(tempDir.resolve("logs").resolve(LauncherLog.FILE_NAME), CAP);
    }

    private List<Path> logFiles() throws IOException {
        try (Stream<Path> files = Files.list(tempDir.resolve("logs"))) {
            return files.sorted().toList();
        }
    }

    @Test
    void warnWritesTheEntryFormatAndEchoesToTheConsole() throws IOException {
        CapturedLog log = TestSupport.log(tempDir);

        log.log.warn("Invalid JSON in recent projects file: boom");

        String nl = System.lineSeparator();
        String expected = "=".repeat(80) + nl
                + "Timestamp: 2026-01-02 03:04:05" + nl
                + "Thread: " + Thread.currentThread().getName() + nl
                + "-".repeat(80) + nl
                + "Warning: Invalid JSON in recent projects file: boom" + nl
                + nl;
        assertEquals(expected, Files.readString(log.log.file()));
        assertEquals("Invalid JSON in recent projects file: boom" + nl, log.console());
    }

    @Test
    void errorWritesTheStackTraceToTheFileOnly() throws IOException {
        CapturedLog log = TestSupport.log(tempDir);

        log.log.error(new IllegalStateException("kaboom"));

        String text = Files.readString(log.log.file());
        assertTrue(text.contains("java.lang.IllegalStateException: kaboom"), text);
        assertTrue(text.contains("at com.vertexforge.launcher.util.LauncherLogTest"), text);
        assertEquals("", log.console());
    }

    @Test
    void appendsWhileUnderTheCap() throws IOException {
        CapturedLog log = TestSupport.log(tempDir);

        log.log.warn("first");
        log.log.warn("second");

        String text = Files.readString(log.log.file());
        assertTrue(text.indexOf("Warning: first") < text.indexOf("Warning: second"));
        assertFalse(Files.exists(log.log.backup(1)));
    }

    @Test
    void rotationKeepsEveryFileUnderTheCapAndAtMostThreeBackups() throws IOException {
        CapturedLog log = cappedLog();

        for (int i = 0; i < 200; i++) {
            log.log.warn("message number " + i + " " + "x".repeat(40));
        }

        List<Path> files = logFiles();
        assertEquals(List.of(
                log.log.file(), log.log.backup(1), log.log.backup(2), log.log.backup(3)), files);
        for (Path file : files) {
            assertTrue(Files.size(file) <= CAP, file + " is " + Files.size(file) + " bytes");
        }
        // The newest entry is in the live file; the oldest ones were dropped with the oldest backup.
        assertTrue(Files.readString(log.log.file()).contains("message number 199 "));
        for (Path file : files) {
            assertFalse(Files.readString(file).contains("message number 0 "), file.toString());
        }
    }

    @Test
    void backupsAreOrderedNewestFirst() throws IOException {
        CapturedLog log = cappedLog();

        for (int i = 0; i < 200; i++) {
            log.log.warn("message number " + i + " " + "x".repeat(40));
        }

        int newestInBackup1 = lastMessageNumber(log.log.backup(1));
        int newestInBackup2 = lastMessageNumber(log.log.backup(2));
        int newestInBackup3 = lastMessageNumber(log.log.backup(3));
        assertTrue(newestInBackup1 > newestInBackup2 && newestInBackup2 > newestInBackup3);
    }

    private static int lastMessageNumber(Path file) throws IOException {
        String text = Files.readString(file);
        int at = text.lastIndexOf("message number ") + "message number ".length();
        return Integer.parseInt(text.substring(at, text.indexOf(' ', at)));
    }

    @Test
    void anOversizedEntryIsTruncatedToTheCap() throws IOException {
        CapturedLog log = cappedLog();

        log.log.warn("ü".repeat(2000));

        Path file = log.log.file();
        assertTrue(Files.size(file) <= CAP, "size " + Files.size(file));
        String text = Files.readString(file, StandardCharsets.UTF_8);
        assertTrue(text.contains(LauncherLog.TRUNCATED_MARKER));
        assertFalse(text.contains("�"), "a multi-byte character was split");
    }

    @Test
    void aWriteFailureFallsBackToTheConsole() throws IOException {
        Path folderInTheWay = Files.createDirectories(tempDir.resolve("logs").resolve(LauncherLog.FILE_NAME));
        CapturedLog log = new CapturedLog(folderInTheWay, CAP);

        log.log.error(new IllegalStateException("kaboom"));

        assertTrue(log.console().contains("Failed to write to error log"), log.console());
        assertTrue(log.console().contains("java.lang.IllegalStateException: kaboom"), log.console());
    }

    @Test
    void rejectsACapTooSmallForTheTruncationMarker() {
        assertThrows(IllegalArgumentException.class, () -> new CapturedLog(tempDir.resolve("x.log"), 10));
    }
}
