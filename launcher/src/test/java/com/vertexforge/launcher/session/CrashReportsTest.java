package com.vertexforge.launcher.session;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.time.Instant;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CrashReportsTest {

    private static final Instant START = Instant.parse("2026-11-15T12:00:00Z");

    @TempDir
    Path tempDir;

    private static final Charset WINDOWS_1252 = Charset.forName("windows-1252");

    private Path report(String stamp, Instant modified, String text) throws IOException {
        return report(stamp, modified, text.getBytes(StandardCharsets.UTF_8));
    }

    private Path report(String stamp, Instant modified, byte[] bytes) throws IOException {
        Path folder = Files.createDirectories(tempDir.resolve("crashes"));
        Path report = Files.write(folder.resolve("crash_" + stamp + ".txt"), bytes);
        Files.setLastModifiedTime(report, FileTime.from(modified));
        return report;
    }

    @Test
    void theNewestReportSinceTheStartIsTheSessions() throws IOException {
        report("20261115_115000", START.minusSeconds(600), "older, another project's");
        report("20261115_120100", START.plusSeconds(60), "first");
        Path newest = report("20261115_120200", START.plusSeconds(120), "VertexForge crash report\nsecond");
        Path dump = Files.writeString(newest.resolveSibling("crash_20261115_120200.dmp"), "MDMP");

        CrashReports.CrashReport found = CrashReports.newestSince(tempDir, START, StandardCharsets.UTF_8)
                .orElseThrow();

        assertEquals(newest, found.text());
        assertEquals("VertexForge crash report\nsecond", found.content());
        assertEquals(Optional.of(dump), found.dump());
    }

    @Test
    void reportsFromBeforeTheSessionAreIgnored() throws IOException {
        report("20261115_115000", START.minusSeconds(5), "older");

        assertEquals(Optional.empty(), CrashReports.newestSince(tempDir, START, StandardCharsets.UTF_8));
    }

    @Test
    void aReportWithinTheClockSlackStillCounts() throws IOException {
        report("20261115_115959", START.minusMillis(500), "just now");

        assertTrue(CrashReports.newestSince(tempDir, START, StandardCharsets.UTF_8).isPresent());
    }

    @Test
    void noFolderOrOnlyDumpsIsNoReport() throws IOException {
        assertEquals(Optional.empty(), CrashReports.newestSince(tempDir, START, StandardCharsets.UTF_8));

        Path folder = Files.createDirectories(tempDir.resolve("crashes"));
        Files.setLastModifiedTime(Files.writeString(folder.resolve("crash_20261115_120100.dmp"), "MDMP"),
                FileTime.from(START.plusSeconds(1)));
        assertEquals(Optional.empty(), CrashReports.newestSince(tempDir, START, StandardCharsets.UTF_8));
    }

    @Test
    void aLongReportIsCut() throws IOException {
        report("20261115_120100", START.plusSeconds(1), "z".repeat(CrashReports.MAX_REPORT_BYTES + 100));

        CrashReports.CrashReport found = CrashReports.newestSince(tempDir, START, StandardCharsets.UTF_8)
                .orElseThrow();

        assertEquals(CrashReports.MAX_REPORT_BYTES, found.content().length());
        assertEquals(Optional.empty(), found.dump());
    }

    /** VK-1684: the editor writes UTF-8; a line an older editor wrote in the code page still reads in it. */
    @Test
    void eachLineIsUtf8OrInTheCodePage() throws IOException {
        String utf8 = "VertexForge crash report\nLast context    : שלום\n";
        String ansi = "  [ 0] Café.exe!main\n";
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        bytes.writeBytes(utf8.getBytes(StandardCharsets.UTF_8));
        bytes.writeBytes(ansi.getBytes(WINDOWS_1252));
        report("20261115_120100", START.plusSeconds(1), bytes.toByteArray());

        CrashReports.CrashReport found = CrashReports.newestSince(tempDir, START, WINDOWS_1252).orElseThrow();

        assertEquals(utf8 + ansi, found.content());
    }

    /** A cut inside the last character spoils only the last line. */
    @Test
    void aCharacterCutAtTheLimitSpoilsOnlyTheLastLine() throws IOException {
        String first = "Last context    : שלום\n";
        byte[] head = first.getBytes(StandardCharsets.UTF_8);
        // Fill up to one byte before the limit, then a two-byte letter that the limit cuts in half.
        String filler = "z".repeat(CrashReports.MAX_REPORT_BYTES - head.length - 1);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        bytes.writeBytes(head);
        bytes.writeBytes((filler + "ש tail\n").getBytes(StandardCharsets.UTF_8));
        report("20261115_120100", START.plusSeconds(1), bytes.toByteArray());

        CrashReports.CrashReport found = CrashReports.newestSince(tempDir, START, WINDOWS_1252).orElseThrow();

        assertTrue(found.content().startsWith(first), found.content().substring(0, 40));
    }
}
