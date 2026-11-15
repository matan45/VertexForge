package com.vertexforge.launcher.session;

import com.sun.nio.file.ExtendedOpenOption;
import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.FileTime;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class SessionLogsTest {

    private static final Charset WINDOWS_1252 = Charset.forName("windows-1252");

    @TempDir
    Path tempDir;

    private final List<String> warnings = new ArrayList<>();

    private SessionLogs logs() {
        return new SessionLogs(tempDir.resolve("logs"), warnings::add);
    }

    @Test
    void namesKeepOnlyPlainAsciiLettersDigitsUnderscoresAndDashes() {
        assertEquals("My_Game", SessionLogs.fileNamePart("My Game"));
        assertEquals("a_b_c", SessionLogs.fileNamePart("a, b. c"));
        assertEquals("CON_x", SessionLogs.fileNamePart("CON.x"), "no dot, so never a device name");
        assertEquals("Jeu", SessionLogs.fileNamePart("Jeu…"));
        assertEquals("project", SessionLogs.fileNamePart("日本語"));
        assertEquals("project", SessionLogs.fileNamePart(""));
        assertEquals("project", SessionLogs.fileNamePart(null));
        assertEquals("x-y_z", SessionLogs.fileNamePart("  x-y z  "));
        assertEquals(SessionLogs.MAX_NAME_LENGTH, SessionLogs.fileNamePart("a".repeat(200)).length());
    }

    @Test
    void eachLaunchGetsItsOwnLogEvenWithinTheSameMillisecond() throws IOException {
        Instant now = Instant.parse("2026-11-15T12:00:00.123Z");
        SessionLogs logs = logs();

        SessionLogs.LaunchFiles first = logs.create("My Game", now);
        SessionLogs.LaunchFiles second = logs.create("My Game", now);

        assertEquals("My_Game-20261115T120000123Z.log", first.log().getFileName().toString());
        assertEquals("My_Game-20261115T120000123Z.status.json", first.status().getFileName().toString());
        assertEquals("My_Game-20261115T120000123Z-2.log", second.log().getFileName().toString());
        assertNotEquals(first.status(), second.status());
        assertTrue(Files.exists(first.log()));
        assertEquals(0, Files.size(first.log()));
        assertFalse(Files.exists(first.status()), "the editor creates its status file itself");
    }

    @Test
    void aStaleStatusFileWithoutItsLogIsRemoved() throws IOException {
        Path dir = Files.createDirectories(tempDir.resolve("logs"));
        Path stale = Files.writeString(dir.resolve("Game-20261115T120000000Z.status.json"), "{}");

        SessionLogs.LaunchFiles files = logs().create("Game", Instant.parse("2026-11-15T12:00:00Z"));

        assertEquals(stale, files.status());
        assertFalse(Files.exists(stale));
    }

    @Test
    void headerAndFooterAreAppendedWithWindowsLineEnds() throws IOException {
        SessionLogs.LaunchFiles files = logs().create("Game", TestSupport.T0);

        SessionLogs.writeHeader(files.log(), List.of("one", "two"));
        Files.writeString(files.log(), "editor output\n", StandardOpenOption.APPEND);
        SessionLogs.appendFooter(files.log(), List.of("----", "end"));

        assertEquals("one\r\ntwo\r\neditor output\n----\r\nend\r\n", Files.readString(files.log()));
    }

    @Test
    void pruneKeepsTheNewestLogsWithTheirStatusFiles() throws IOException {
        Path dir = Files.createDirectories(tempDir.resolve("logs"));
        for (int i = 0; i < 5; i++) {
            Path log = Files.writeString(dir.resolve("P-" + i + ".log"), "log " + i);
            Files.writeString(dir.resolve("P-" + i + ".status.json"), "{}");
            Files.setLastModifiedTime(log, FileTime.fromMillis(1_000_000L + i * 1000L));
        }
        Files.writeString(dir.resolve("P-0.status.json.123.tmp"), "x");
        Files.writeString(dir.resolve("P-0.status.json.123.crash.tmp"), "x");
        Files.writeString(dir.resolve("notes.txt"), "not a log");

        logs().prune(3);

        assertEquals(Set.of("P-2.log", "P-2.status.json", "P-3.log", "P-3.status.json", "P-4.log",
                "P-4.status.json", "notes.txt"), TestSupport.fileNames(dir));
        assertTrue(warnings.isEmpty(), warnings.toString());
    }

    /** A running editor's log has no delete sharing; it and its status file stay. */
    @Test
    void pruneSkipsALogThatIsStillWritten() throws IOException {
        assumeTrue(TestSupport.isWindows(), "delete sharing is a Windows notion");
        Path dir = Files.createDirectories(tempDir.resolve("logs"));
        Path old = Files.writeString(dir.resolve("Old.log"), "running");
        Files.writeString(dir.resolve("Old.status.json"), "{}");
        Files.setLastModifiedTime(old, FileTime.fromMillis(1_000L));
        Path newer = Files.writeString(dir.resolve("New.log"), "new");
        Files.setLastModifiedTime(newer, FileTime.fromMillis(2_000L));

        try (FileChannel held = FileChannel.open(old, StandardOpenOption.APPEND, ExtendedOpenOption.NOSHARE_DELETE)) {
            logs().prune(1);
            assertTrue(held.isOpen());
        }

        assertEquals(Set.of("Old.log", "Old.status.json", "New.log"), TestSupport.fileNames(dir));
    }

    @Test
    void pruneOfAMissingFolderOnlyWarns() {
        logs().prune(20);

        assertEquals(1, warnings.size(), warnings.toString());
    }

    @Test
    void tailGivesTheLastLinesWithoutLineEnds() throws IOException {
        Path log = Files.writeString(tempDir.resolve("a.log"), "1\r\n2\n3\r\n4\r\n");

        assertEquals(List.of("3", "4"), SessionLogs.tail(log, 2, WINDOWS_1252));
        assertEquals(List.of("1", "2", "3", "4"), SessionLogs.tail(log, 30, WINDOWS_1252));
    }

    @Test
    void tailKeepsALastLineWithoutANewlineAndEmptyLines() throws IOException {
        Path log = Files.writeString(tempDir.resolve("a.log"), "a\n\nb");

        assertEquals(List.of("a", "", "b"), SessionLogs.tail(log, 30, WINDOWS_1252));
    }

    @Test
    void tailDecodesEachLineAsUtf8OrElseInTheCodePage() throws IOException {
        byte[] utf8 = "é UTF-8\n".getBytes(StandardCharsets.UTF_8);
        byte[] ansi = "é ANSI\n".getBytes(WINDOWS_1252);
        byte[] both = new byte[utf8.length + ansi.length];
        System.arraycopy(utf8, 0, both, 0, utf8.length);
        System.arraycopy(ansi, 0, both, utf8.length, ansi.length);
        Path log = Files.write(tempDir.resolve("a.log"), both);

        assertEquals(List.of("é UTF-8", "é ANSI"), SessionLogs.tail(log, 30, WINDOWS_1252));
    }

    @Test
    void tailReadsOnlyTheEndOfABigLogAndDropsTheCutLine() throws IOException {
        StringBuilder text = new StringBuilder();
        text.append("x".repeat(1024 * 1024)).append('\n');
        for (int i = 0; i < 40; i++) {
            text.append("line ").append(i).append('\n');
        }
        Path log = Files.writeString(tempDir.resolve("big.log"), text);

        List<String> tail = SessionLogs.tail(log, 30, WINDOWS_1252);

        assertEquals(30, tail.size());
        assertEquals("line 10", tail.getFirst());
        assertEquals("line 39", tail.getLast());
    }

    @Test
    void tailOfAHugeSingleLineDropsItsCutStart() throws IOException {
        Path log = Files.writeString(tempDir.resolve("one.log"), "y".repeat(200_000));

        assertEquals(List.of(), SessionLogs.tail(log, 30, WINDOWS_1252),
                "the read starts mid-line, so the only line is cut and dropped");
    }

    @Test
    void tailOfAMissingLogIsEmpty() {
        assertEquals(List.of(), SessionLogs.tail(tempDir.resolve("none.log"), 30, WINDOWS_1252));
    }
}
