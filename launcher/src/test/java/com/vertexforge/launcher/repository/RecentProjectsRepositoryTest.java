package com.vertexforge.launcher.repository;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.model.RecentProjectsData;
import com.vertexforge.launcher.model.RecentProjectsData.ProjectData;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RecentProjectsRepositoryTest {

    private static final Instant T0 = Instant.parse("2026-01-01T00:00:00Z");

    @TempDir
    Path tempDir;

    private Path storeFile;
    private CapturedLog log;
    private RecentProjectsRepository repository;

    @BeforeEach
    void setUp() {
        // A missing parent folder: save() must create it.
        storeFile = tempDir.resolve("config").resolve("recent-projects.json");
        log = TestSupport.log(tempDir.resolve("logs"));
        repository = new RecentProjectsRepository(storeFile, log.log);
    }

    /** Creates a real .vfproj, so the entry gets past ProjectValidator at load. */
    private Path projectFile(String name) throws IOException {
        Path dir = Files.createDirectories(tempDir.resolve("projects").resolve(name));
        return Files.writeString(dir.resolve(name + ".vfproj"), "{}");
    }

    private void writeStore(ProjectData... entries) throws IOException {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, new Gson().toJson(new RecentProjectsData(1, Arrays.asList(entries))));
    }

    private static List<String> names(RecentProjects projects) {
        return projects.getAll().stream().map(ProjectInfo::name).toList();
    }

    @Test
    void missingFileLoadsEmptyAndWritesNothing() {
        RecentProjects loaded = repository.load();

        assertTrue(loaded.isEmpty());
        assertFalse(Files.exists(storeFile));
    }

    @Test
    void saveThenLoadRoundTrips() throws IOException {
        Path game = projectFile("Game");
        Path demo = projectFile("Demo");
        RecentProjects projects = new RecentProjects()
                .add(new ProjectInfo("Game", game, T0))
                .add(new ProjectInfo("Demo", demo, T0.plusSeconds(90)));

        repository.save(projects);
        List<ProjectInfo> loaded = repository.load().getAll();

        assertEquals(List.of(
                new ProjectInfo("Demo", demo, T0.plusSeconds(90)),
                new ProjectInfo("Game", game, T0)), loaded);
    }

    @Test
    void saveCreatesTheFolderAndWritesVersionOne() throws IOException {
        Path game = projectFile("Game");

        repository.save(new RecentProjects().add(new ProjectInfo("Game", game, T0)));

        assertTrue(Files.isRegularFile(storeFile));
        JsonObject json = JsonParser.parseString(Files.readString(storeFile)).getAsJsonObject();
        assertEquals(1, json.get("version").getAsInt());
        assertEquals(1, json.getAsJsonArray("projects").size());
        JsonObject entry = json.getAsJsonArray("projects").get(0).getAsJsonObject();
        assertEquals("Game", entry.get("name").getAsString());
        assertEquals(game.toString(), entry.get("path").getAsString());
        assertEquals("2026-01-01T00:00:00Z", entry.get("lastOpened").getAsString());
    }

    @ParameterizedTest
    @ValueSource(strings = {"{not json", "null", "", "{\"projects\":5}", "[]"})
    void unreadableContentLoadsEmptyAndLeavesTheFileUntouched(String content) throws IOException {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, content);
        byte[] before = Files.readAllBytes(storeFile);

        RecentProjects loaded = repository.load();

        assertTrue(loaded.isEmpty());
        assertArrayEquals(before, Files.readAllBytes(storeFile));
    }

    @ParameterizedTest
    @ValueSource(strings = {"{not json", "{\"projects\":5}", "[]"})
    void invalidJsonIsReportedToTheConsoleAndTheLog(String content) throws IOException {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, content);

        repository.load();

        assertTrue(log.console().contains("Invalid JSON in recent projects file"), log.console());
        assertTrue(Files.readString(log.log.file()).contains("Warning: Invalid JSON in recent projects file"));
    }

    @Test
    void aNullEntryIsSkippedAndTheOthersLoad() throws IOException {
        Path game = projectFile("Game");
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, """
                {"version": 1, "projects": [null, {"name": "Game", "path": %s, "lastOpened": "%s"}]}
                """.formatted(new Gson().toJson(game.toString()), T0));

        assertEquals(List.of("Game"), names(repository.load()));
        assertTrue(log.console().contains("Skipping an empty entry"), log.console());
    }

    @Test
    void aFailedSaveIsReportedToTheConsoleAndTheLog() throws IOException {
        // The store path is a folder, so the write fails.
        Files.createDirectories(storeFile);

        repository.save(new RecentProjects().add(new ProjectInfo("Game", projectFile("Game"), T0)));

        assertTrue(log.console().contains("Failed to save recent projects"), log.console());
        assertTrue(Files.readString(log.log.file()).contains("Warning: Failed to save recent projects"));
    }

    @Test
    void entryWithABadTimestampIsSkipped() throws IOException {
        writeStore(
                new ProjectData("Bad", projectFile("Bad").toString(), "yesterday"),
                new ProjectData("Good", projectFile("Good").toString(), T0.toString()));

        assertEquals(List.of("Good"), names(repository.load()));
    }

    @Test
    void entryWithAMissingFieldIsSkipped() throws IOException {
        writeStore(
                new ProjectData(null, projectFile("NoName").toString(), T0.toString()),
                new ProjectData("NoTime", projectFile("NoTime").toString(), null),
                new ProjectData("Good", projectFile("Good").toString(), T0.toString()));

        assertEquals(List.of("Good"), names(repository.load()));
    }

    @Test
    void entryWithARelativePathIsSkipped() throws IOException {
        writeStore(
                new ProjectData("Relative", "projects/Relative/Relative.vfproj", T0.toString()),
                new ProjectData("Good", projectFile("Good").toString(), T0.toString()));

        assertEquals(List.of("Good"), names(repository.load()));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void entryWithAnInvalidWindowsPathIsSkipped() throws IOException {
        writeStore(
                new ProjectData("Invalid", "C:\\bad<name>\\Game.vfproj", T0.toString()),
                new ProjectData("Good", projectFile("Good").toString(), T0.toString()));

        assertEquals(List.of("Good"), names(repository.load()));
    }

    /**
     * Characterization of today's behaviour: an entry whose project file is missing is dropped at load,
     * so the next save loses it for good. VK-1678 changes this (status instead of filtering) and must
     * replace this test deliberately.
     */
    @Test
    void entryWhoseProjectFileIsMissingIsDroppedAtLoad() throws IOException {
        Path missing = tempDir.resolve("gone").resolve("Gone.vfproj");
        writeStore(
                new ProjectData("Gone", missing.toString(), T0.toString()),
                new ProjectData("Good", projectFile("Good").toString(), T0.toString()));

        assertEquals(List.of("Good"), names(repository.load()));
    }
}
