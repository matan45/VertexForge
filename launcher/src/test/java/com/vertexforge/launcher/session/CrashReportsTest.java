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
import java.util.List;
import java.util.Optional;
import java.util.OptionalLong;

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

    // ----- VK-1732: a folder the editor names, shared by several editors; names tagged with the writer's pid -----

    private static final long PID = 1234;

    private Path userCrashes() throws IOException {
        return Files.createDirectories(tempDir.resolve("profile/.vertexforge/editor/crashes"));
    }

    private static Path file(Path folder, String name, Instant modified, String text) throws IOException {
        Path file = Files.writeString(folder.resolve(name), text);
        Files.setLastModifiedTime(file, FileTime.from(modified));
        return file;
    }

    private static Optional<CrashReports.CrashReport> newestIn(Path folder, OptionalLong pid) {
        return CrashReports.newestIn(folder, START, pid, StandardCharsets.UTF_8);
    }

    @Test
    void newestInReadsTheGivenFolder() throws IOException {
        Path folder = userCrashes();
        report("20261115_120300", START.plusSeconds(180), "in the working directory");
        Path mine = file(folder, "crash_20261115_120100_1234.txt", START.plusSeconds(60), "the session's");
        Path dump = Files.writeString(folder.resolve("crash_20261115_120100_1234.dmp"), "MDMP");

        CrashReports.CrashReport found = newestIn(folder, OptionalLong.of(PID)).orElseThrow();

        assertEquals(mine, found.text());
        assertEquals("the session's", found.content());
        assertEquals(Optional.of(dump), found.dump());
    }

    @Test
    void theSessionsPidWinsOverNewerReportsOfOtherEditors() throws IOException {
        Path folder = userCrashes();
        file(folder, "crash_20261115_120100_1234.txt", START.plusSeconds(60), "older, the session's");
        Path mine = file(folder, "crash_20261115_120200_1234.txt", START.plusSeconds(120), "the session's");
        file(folder, "crash_20261115_120300_5678.txt", START.plusSeconds(180), "another editor's");
        file(folder, "crash_20261115_120400.txt", START.plusSeconds(240), "an older editor's");

        assertEquals(mine, newestIn(folder, OptionalLong.of(PID)).orElseThrow().text());
    }

    @Test
    void anotherEditorsReportNeverCounts() throws IOException {
        Path folder = userCrashes();
        file(folder, "crash_20261115_120100_5678.txt", START.plusSeconds(60), "another editor's");
        file(folder, "crash_20261115_120100_12345.txt", START.plusSeconds(60), "a longer pid");
        file(folder, "crash_20261115_120100_99999999999999999999.txt", START.plusSeconds(60), "no long holds it");

        assertEquals(Optional.empty(), newestIn(folder, OptionalLong.of(PID)));
    }

    /** An editor older than VK-1732 tags nothing; its report counts when none carries the session's pid. */
    @Test
    void anUntaggedReportCountsWhenNoneCarriesThePid() throws IOException {
        Path folder = userCrashes();
        file(folder, "crash_20261115_120100.txt", START.plusSeconds(60), "older, untagged");
        Path untagged = file(folder, "crash_20261115_120200.txt", START.plusSeconds(120), "untagged");
        file(folder, "crash_20261115_120300_5678.txt", START.plusSeconds(180), "another editor's");

        assertEquals(untagged, newestIn(folder, OptionalLong.of(PID)).orElseThrow().text());
    }

    @Test
    void theSessionsOwnReportFromBeforeItStartedIsIgnored() throws IOException {
        Path folder = userCrashes();
        file(folder, "crash_20261115_115000_1234.txt", START.minusSeconds(600), "a reused pid, long ago");
        Path untagged = file(folder, "crash_20261115_120100.txt", START.plusSeconds(60), "untagged");

        assertEquals(untagged, newestIn(folder, OptionalLong.of(PID)).orElseThrow().text());
    }

    @Test
    void withoutAPidTheNewestOfAllCounts() throws IOException {
        Path folder = userCrashes();
        file(folder, "crash_20261115_120100_1234.txt", START.plusSeconds(60), "tagged");
        file(folder, "crash_20261115_120200.txt", START.plusSeconds(120), "untagged");
        Path newest = file(folder, "crash_20261115_120300_99999999999999999999.txt", START.plusSeconds(180),
                "a tag no long holds");

        assertEquals(newest, newestIn(folder, OptionalLong.empty()).orElseThrow().text());
    }

    @Test
    void namesThatAreNotCrashReportsAreIgnored() throws IOException {
        Path folder = userCrashes();
        Path real = file(folder, "crash_20261115_120100_1234.txt", START.plusSeconds(1), "the session's");
        for (String name : List.of("crash_latest.txt", "crash_20261115_1201_1234.txt", "crash_20261115_120100_.txt",
                "crash_20261115_120100_x1234.txt", "crash_20261115_120100_1234_2.txt", "crash_2026111_120100.txt",
                "crash_20261115_120100.txt.txt")) {
            file(folder, name, START.plusSeconds(60), "not a report");
        }

        assertEquals(real, newestIn(folder, OptionalLong.of(PID)).orElseThrow().text());
        assertEquals(real, newestIn(folder, OptionalLong.empty()).orElseThrow().text());
        assertEquals(real, CrashReports.newestSince(folder.getParent(), START, StandardCharsets.UTF_8)
                .orElseThrow().text());
    }

    @Test
    void equalTimesGoToTheGreaterName() throws IOException {
        Path folder = userCrashes();
        file(folder, "crash_20261115_120100_1234.txt", START.plusSeconds(60), "first");
        Path greater = file(folder, "crash_20261115_120101_1234.txt", START.plusSeconds(60), "second");

        assertEquals(greater, newestIn(folder, OptionalLong.of(PID)).orElseThrow().text());
    }

    @Test
    void aMissingFolderIsNoReport() {
        assertEquals(Optional.empty(), newestIn(tempDir.resolve("missing"), OptionalLong.of(PID)));
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
