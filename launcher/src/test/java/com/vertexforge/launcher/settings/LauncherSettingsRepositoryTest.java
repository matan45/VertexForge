package com.vertexforge.launcher.settings;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class LauncherSettingsRepositoryTest {

    @TempDir
    Path tempDir;

    private Path file;
    private CapturedLog log;

    @BeforeEach
    void setUp() {
        file = tempDir.resolve("config").resolve(LauncherSettingsRepository.FILE_NAME);
        log = TestSupport.log(tempDir.resolve("logs"));
    }

    private LauncherSettings load() {
        return new LauncherSettingsRepository(file, TestSupport.fixedClock(), log.log).load();
    }

    private void write(String content) throws IOException {
        Files.createDirectories(file.getParent());
        Files.writeString(file, content);
    }

    @Test
    void anAbsentFileGivesTheDefaultsAndCreatesNothing() {
        assertEquals(LauncherSettings.DEFAULTS, load());
        assertEquals(50, load().recentProjectsLimit());
        assertFalse(Files.exists(file.getParent()));
        assertEquals("", log.console());
    }

    @Test
    void aValidLimitIsUsed() throws IOException {
        write("{\"version\": 1, \"recentProjectsLimit\": 20}");

        assertEquals(20, load().recentProjectsLimit());
    }

    @Test
    void aFileWithoutAVersionIsReadAsVersionOne() throws IOException {
        write("{\"recentProjectsLimit\": 7}");

        assertEquals(7, load().recentProjectsLimit());
    }

    @Test
    void aMissingLimitIsTheDefaultWithoutAWarning() throws IOException {
        write("{\"version\": 1, \"other\": true}");

        assertEquals(50, load().recentProjectsLimit());
        assertEquals("", log.console());
    }

    @ParameterizedTest
    @ValueSource(strings = {"0", "501", "-3", "12.5", "\"20\"", "true"})
    void anInvalidLimitFallsBackToTheDefaultWithAWarning(String value) throws IOException {
        write("{\"version\": 1, \"recentProjectsLimit\": " + value + "}");

        assertEquals(50, load().recentProjectsLimit());
        assertTrue(log.console().contains("recentProjectsLimit"), log.console());
    }

    @Test
    void aCorruptFileFallsBackToTheDefaultsAndIsNeverMoved() throws IOException {
        write("{oops");
        byte[] original = Files.readAllBytes(file);

        assertEquals(LauncherSettings.DEFAULTS, load());

        assertArrayEquals(original, Files.readAllBytes(file));
        assertEquals(Set.of(LauncherSettingsRepository.FILE_NAME), TestSupport.fileNames(file.getParent()));
        assertTrue(log.console().contains("Ignoring the launcher settings file"), log.console());
    }

    @Test
    void aNewerFileStillGivesTheMembersThisVersionKnows() throws IOException {
        write("{\"version\": 9, \"recentProjectsLimit\": 30, \"theme\": \"dark\"}");

        assertEquals(30, load().recentProjectsLimit());
    }

    @Test
    void theLimitMustBeInRange() {
        assertThrows(IllegalArgumentException.class, () -> new LauncherSettings(0));
        assertThrows(IllegalArgumentException.class, () -> new LauncherSettings(501));
        assertEquals(500, new LauncherSettings(500).recentProjectsLimit());
    }
}
