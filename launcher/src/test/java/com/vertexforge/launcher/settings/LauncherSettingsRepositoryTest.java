package com.vertexforge.launcher.settings;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.session.SessionLogs;
import com.vertexforge.launcher.store.StoreWriteException;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;
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

    private LauncherSettingsRepository repository() {
        return new LauncherSettingsRepository(file, TestSupport.fixedClock(), log.log);
    }

    /** VK-1683: minimize and come back, with the editor's console, unless the file says otherwise. */
    @Test
    void theLaunchBehaviourDefaultsToMinimizeWithTheConsole() {
        LauncherSettings settings = load();

        assertEquals(LauncherSettings.AfterLaunch.MINIMIZE, settings.afterLaunch());
        assertTrue(settings.showEditorConsole());
    }

    @Test
    void theLaunchBehaviourIsRead() throws IOException {
        write("{\"version\": 1, \"afterLaunch\": \"close\", \"showEditorConsole\": false}");

        assertEquals(LauncherSettings.AfterLaunch.CLOSE, load().afterLaunch());
        assertFalse(load().showEditorConsole());
    }

    @Test
    void anInvalidLaunchBehaviourFallsBackWithAWarning() throws IOException {
        write("{\"version\": 1, \"afterLaunch\": \"hide\", \"showEditorConsole\": \"yes\"}");

        LauncherSettings settings = load();

        assertEquals(LauncherSettings.AfterLaunch.MINIMIZE, settings.afterLaunch());
        assertTrue(settings.showEditorConsole());
        assertTrue(log.console().contains("afterLaunch"), log.console());
        assertTrue(log.console().contains("showEditorConsole"), log.console());
    }

    @Test
    void saveWritesEveryMemberItOwnsAndKeepsTheOthers() throws Exception {
        write("{\"version\": 1, \"recentProjectsLimit\": 20, \"theme\": \"dark\"}");
        Path location = tempDir.resolve("Projects").toAbsolutePath();
        LauncherSettings wanted = new LauncherSettings(30, LauncherSettings.AfterLaunch.STAY, false,
                Optional.of(location), 7);

        LauncherSettings saved = repository().save(wanted);

        assertEquals(wanted, saved);
        assertEquals(saved, load());
        JsonObject root = JsonParser.parseString(Files.readString(file)).getAsJsonObject();
        assertEquals("dark", root.get("theme").getAsString());
        assertEquals(1, root.get("version").getAsInt());
        assertEquals(30, root.get("recentProjectsLimit").getAsInt());
        assertEquals("stay", root.get("afterLaunch").getAsString());
        assertFalse(root.get("showEditorConsole").getAsBoolean());
        assertEquals(location.toString(), root.get("defaultProjectLocation").getAsString());
        assertEquals(7, root.get("logRetention").getAsInt());
    }

    @Test
    void saveCreatesTheFile() throws Exception {
        repository().save(new LauncherSettings(LauncherSettings.DEFAULT_RECENT_PROJECTS_LIMIT,
                LauncherSettings.AfterLaunch.CLOSE, true));

        assertEquals(new LauncherSettings(LauncherSettings.DEFAULT_RECENT_PROJECTS_LIMIT,
                LauncherSettings.AfterLaunch.CLOSE, true), load());
    }

    @Test
    void anUnsetLocationRemovesItsMember() throws Exception {
        write("{\"version\": 1, \"defaultProjectLocation\": \"" + jsonText(tempDir.toAbsolutePath()) + "\"}");
        assertTrue(load().defaultProjectLocation().isPresent());

        repository().save(LauncherSettings.DEFAULTS);

        assertFalse(Files.readString(file).contains("defaultProjectLocation"));
        assertEquals(Optional.empty(), load().defaultProjectLocation());
    }

    @Test
    void aNewerFileIsNeverOverwritten() throws IOException {
        write("{\"version\": 9, \"afterLaunch\": \"stay\"}");
        byte[] before = Files.readAllBytes(file);
        LauncherSettingsRepository repository = repository();
        assertFalse(repository.newerFile(), "nothing read yet");

        assertThrows(StoreWriteException.class,
                () -> repository.save(new LauncherSettings(10, LauncherSettings.AfterLaunch.CLOSE, true)));
        assertArrayEquals(before, Files.readAllBytes(file));
        assertTrue(repository.newerFile(), "the refused save says why");
    }

    /** VK-1694: the Settings dialog knows without reading the file again that it cannot save. */
    @Test
    void aNewerFileIsReportedByItsReadAndClearedByASave() throws Exception {
        write("{\"version\": 9}");
        LauncherSettingsRepository repository = repository();

        repository.load();
        assertTrue(repository.newerFile());

        write("{\"version\": 1}");
        repository.save(LauncherSettings.DEFAULTS);
        assertFalse(repository.newerFile());
    }

    // --- VK-1694: default project location and log retention ---------------------------------------------------------

    private static String jsonText(Path path) {
        return path.toString().replace("\\", "\\\\");
    }

    @Test
    void theNewMembersHaveTheirDefaultsWhenAbsent() throws IOException {
        write("{\"version\": 1}");

        LauncherSettings settings = load();

        assertEquals(Optional.empty(), settings.defaultProjectLocation());
        assertEquals(LauncherSettings.DEFAULT_LOG_RETENTION, settings.logRetention());
        assertEquals("", log.console());
    }

    @Test
    void anAbsoluteLocationAndARetentionAreRead() throws IOException {
        Path location = tempDir.resolve("Projects").toAbsolutePath();
        write("{\"version\": 1, \"defaultProjectLocation\": \"" + jsonText(location) + "\", \"logRetention\": 5}");

        assertEquals(Optional.of(location), load().defaultProjectLocation());
        assertEquals(5, load().logRetention());
    }

    @Test
    void aBlankLocationMeansNoneWithoutAWarning() throws IOException {
        write("{\"version\": 1, \"defaultProjectLocation\": \"  \"}");

        assertEquals(Optional.empty(), load().defaultProjectLocation());
        assertEquals("", log.console());
    }

    @ParameterizedTest
    @ValueSource(strings = {"\"relative\\\\folder\"", "\"C:no-separator\"", "\"\\\\no-drive\"", "12", "true",
            "\"bad\\u0000name\""})
    void aLocationThatIsNotAnAbsolutePathIsIgnoredWithAWarning(String value) throws IOException {
        write("{\"version\": 1, \"defaultProjectLocation\": " + value + "}");

        assertEquals(Optional.empty(), load().defaultProjectLocation());
        assertTrue(log.console().contains("defaultProjectLocation"), log.console());
    }

    @ParameterizedTest
    @ValueSource(strings = {"0", "201", "-1", "2.5", "\"10\"", "false"})
    void anInvalidRetentionFallsBackToTheDefaultWithAWarning(String value) throws IOException {
        write("{\"version\": 1, \"logRetention\": " + value + "}");

        assertEquals(LauncherSettings.DEFAULT_LOG_RETENTION, load().logRetention());
        assertTrue(log.console().contains("logRetention"), log.console());
    }

    @Test
    void theRetentionMustBeInRangeAndTheLocationAbsolute() {
        assertThrows(IllegalArgumentException.class, () -> new LauncherSettings(10,
                LauncherSettings.AfterLaunch.STAY, true, Optional.empty(), 0));
        assertThrows(IllegalArgumentException.class, () -> new LauncherSettings(10,
                LauncherSettings.AfterLaunch.STAY, true, Optional.empty(), 201));
        assertThrows(IllegalArgumentException.class, () -> new LauncherSettings(10,
                LauncherSettings.AfterLaunch.STAY, true, Optional.of(Path.of("relative")), 20));
        assertEquals(200, new LauncherSettings(10, LauncherSettings.AfterLaunch.STAY, true, Optional.empty(), 200)
                .logRetention());
    }

    /** The default keeps as many logs as the launcher kept before the setting existed. */
    @Test
    void theDefaultRetentionIsTheOldFixedNumber() {
        assertEquals(SessionLogs.KEEP, LauncherSettings.DEFAULT_LOG_RETENTION);
    }
}
