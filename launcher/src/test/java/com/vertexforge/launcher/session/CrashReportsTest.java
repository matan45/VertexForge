package com.vertexforge.launcher.session;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
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

    private Path report(String stamp, Instant modified, String text) throws IOException {
        Path folder = Files.createDirectories(tempDir.resolve("crashes"));
        Path report = Files.writeString(folder.resolve("crash_" + stamp + ".txt"), text);
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
}
