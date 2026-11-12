package com.vertexforge.launcher.repository;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.store.StoreWriteException.Reason;
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
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class RecentProjectsRepositoryTest {

    private static final int LIMIT = 50;
    /** TestSupport.T0 = 2026-01-02T03:04:05Z, the fixed clock's time. */
    private static final String QUARANTINE_NAME = "recent-projects.json.corrupt-20260102T030405Z";

    @TempDir
    Path tempDir;

    private Path root;
    private Path storeFile;
    private CapturedLog log;
    private RecentProjectsRepository repository;

    @BeforeEach
    void setUp() {
        root = TestSupport.real(tempDir);
        // A missing parent folder: the first write must create it.
        storeFile = root.resolve("config").resolve(RecentProjectsRepository.FILE_NAME);
        log = TestSupport.log(root.resolve("logs"));
        repository = newRepository();
    }

    /** A fresh repository on the same file, as another launcher (or the next start) would have. */
    private RecentProjectsRepository newRepository() {
        return new RecentProjectsRepository(
                RecentProjectsRepository.openStore(storeFile, TestSupport.fixedClock(), log.log),
                UUID::randomUUID, TestSupport.fixedClock(), log.log);
    }

    private Path projectFile(String name) throws IOException {
        Path dir = Files.createDirectories(root.resolve("projects").resolve(name));
        return Files.writeString(dir.resolve(name + ".vfproj"), "{}");
    }

    private void writeStore(String json) throws IOException {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, json);
    }

    private JsonObject readStore() throws IOException {
        return JsonParser.parseString(Files.readString(storeFile)).getAsJsonObject();
    }

    private static String quote(Object value) {
        return new JsonPrimitive(value.toString()).toString();
    }

    private static List<String> names(RecentProjectsView view) {
        return view.entries().stream().map(RecentEntry::displayName).toList();
    }

    @Test
    void aMissingFileLoadsEmptyAndWritesNothing() {
        RecentProjectsView view = repository.load();

        assertTrue(view.isEmpty());
        assertEquals(StoreStatus.ABSENT, view.status());
        assertFalse(view.readOnly());
        assertFalse(Files.exists(storeFile.getParent()), "loading must not create the folder or a lock file");
    }

    @Test
    void addThenLoadRoundTripsNewestFirst() throws Exception {
        Path game = projectFile("Game");
        Path demo = projectFile("Demo");

        repository.add(game, "Game", LIMIT);
        RecentProjectsView afterDemo = newRepository().add(demo, "Demo", LIMIT);
        RecentProjectsView loaded = newRepository().load();

        // The fixed clock gives both the same time, so the file order breaks the tie.
        assertEquals(List.of("Game", "Demo"), names(loaded));
        assertEquals(afterDemo.entries(), loaded.entries());
        assertEquals(StoreStatus.OK, loaded.status());
        assertEquals(game, loaded.entries().get(0).path());
        assertEquals(Instant.parse("2026-01-02T03:04:05Z"), loaded.entries().get(0).lastOpened());
    }

    @Test
    void addCreatesTheFolderAndWritesVersionTwoWithAllSixFields() throws Exception {
        Path game = projectFile("Game");

        repository.add(game, "Game", LIMIT);

        JsonObject json = readStore();
        assertEquals(2, json.get("version").getAsInt());
        assertEquals(1, json.getAsJsonArray("projects").size());
        JsonObject entry = json.getAsJsonArray("projects").get(0).getAsJsonObject();
        UUID.fromString(entry.get("id").getAsString());
        assertEquals("Game", entry.get("name").getAsString());
        assertEquals(game.toString(), entry.get("path").getAsString());
        assertEquals("2026-01-02T03:04:05Z", entry.get("lastOpened").getAsString());
        assertFalse(entry.get("pinned").getAsBoolean());
        assertTrue(entry.get("engineId").isJsonNull());
    }

    @Test
    void addingAListedPathAgainKeepsItsIdAndRefreshesNameAndTime() throws Exception {
        Path game = projectFile("Game");
        UUID id = repository.add(game, "Old Name", LIMIT).entries().getFirst().id();

        RecentProjectsView view = repository.add(game, "New Name", LIMIT);

        assertEquals(1, view.entries().size());
        assertEquals(id, view.entries().getFirst().id());
        assertEquals("New Name", view.entries().getFirst().name());
    }

    @Test
    void markOpenedMovesTheProjectToTheTop() throws Exception {
        Path a = projectFile("A");
        Path b = projectFile("B");
        RecentProjectsRepository ticking = new RecentProjectsRepository(
                RecentProjectsRepository.openStore(storeFile, TestSupport.fixedClock(), log.log),
                UUID::randomUUID, new TestSupport.TickingClock(TestSupport.T0), log.log);
        ticking.add(a, "A", LIMIT);
        UUID bId = ticking.add(b, "B", LIMIT).findByPath(b).orElseThrow().id();
        UUID aId = ticking.load().findByPath(a).orElseThrow().id();
        assertEquals(bId, ticking.load().entries().getFirst().id());

        RecentProjectsView view = ticking.markOpened(aId);

        assertEquals(List.of("A", "B"), names(view));
    }

    @Test
    void removeDropsOnlyThatEntryAndAStaleRemoveChangesNothing() throws Exception {
        UUID keep = repository.add(projectFile("Keep"), "Keep", LIMIT).entries().getFirst().id();
        UUID drop = repository.add(projectFile("Drop"), "Drop", LIMIT).findByPath(root.resolve("projects/Drop/Drop.vfproj"))
                .orElseThrow().id();

        repository.remove(drop);
        RecentProjectsView again = newRepository().remove(drop);

        assertEquals(List.of(keep), again.entries().stream().map(RecentEntry::id).toList());
    }

    @Test
    void openingAnEntryAnotherLauncherRemovedNeverBringsItBack() throws Exception {
        UUID gone = repository.add(projectFile("Gone"), "Gone", LIMIT).entries().getFirst().id();
        newRepository().remove(gone);

        RecentProjectsView view = repository.markOpened(gone);

        assertTrue(view.isEmpty());
    }

    @Test
    void theListIsTrimmedOldestFirstButNeverTheNewEntryOrAPinnedOne() throws Exception {
        // Oldest: a pinned entry (kept), then an unpinned one (dropped first), then a newer one.
        writeStore("""
                {"version": 2, "projects": [
                  {"id": "%s", "name": "Pinned", "path": %s, "lastOpened": "2020-01-01T00:00:00Z", "pinned": true},
                  {"id": "%s", "name": "Old", "path": %s, "lastOpened": "2021-01-01T00:00:00Z", "pinned": false},
                  {"id": "%s", "name": "Newer", "path": %s, "lastOpened": "2025-01-01T00:00:00Z", "pinned": false}]}
                """.formatted(UUID.randomUUID(), quote(root.resolve("P.vfproj")), UUID.randomUUID(),
                quote(root.resolve("O.vfproj")), UUID.randomUUID(), quote(root.resolve("N.vfproj"))));

        RecentProjectsView view = repository.add(root.resolve("Added.vfproj"), "Added", 3);

        assertEquals(List.of("Added", "Newer", "Pinned"), names(view));
    }

    // --- AC1: nothing is ever dropped at load ------------------------------------------------------------------------

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void anEntryOnAnAbsentDriveSurvivesLoadSaveLoad() throws Exception {
        Optional<Path> absentRoot = TestSupport.absentDriveRoot();
        assumeTrue(absentRoot.isPresent(), "every drive letter from D to Z is in use");
        Path offline = absentRoot.get().resolve("Projects").resolve("Gone").resolve("Gone.vfproj");
        UUID id = UUID.randomUUID();
        writeStore("""
                {"version": 2, "projects": [{"id": "%s", "name": "Gone", "path": %s,
                  "lastOpened": "2025-05-05T05:05:05Z", "pinned": false, "engineId": null}]}
                """.formatted(id, quote(offline)));

        RecentProjectsView loaded = repository.load();
        repository.add(projectFile("Game"), "Game", LIMIT);
        RecentProjectsView reloaded = newRepository().load();

        assertEquals(1, loaded.entries().size());
        RecentEntry gone = reloaded.find(id).orElseThrow();
        assertEquals("Gone", gone.name());
        assertEquals(Instant.parse("2025-05-05T05:05:05Z"), gone.lastOpened());
        assertEquals(offline, gone.path());
        assertEquals(offline.toString(), readStore().getAsJsonArray("projects").get(0).getAsJsonObject()
                .get("path").getAsString(), "the stored path is written back unchanged");
        assertEquals(2, reloaded.entries().size());
    }

    @Test
    void anEntryWhoseProjectFileIsMissingIsKept() throws Exception {
        Path missing = root.resolve("gone").resolve("Gone.vfproj");
        writeStore("""
                {"version": 1, "projects": [
                  {"name": "Gone", "path": %s, "lastOpened": "2025-01-01T00:00:00Z"}]}
                """.formatted(quote(missing)));

        repository.add(projectFile("Good"), "Good", LIMIT);

        assertEquals(List.of("Good", "Gone"), names(newRepository().load()));
    }

    @Test
    void entriesWithBadFieldsAreKeptAndWrittenBackUnchanged() throws Exception {
        writeStore("""
                {"version": 1, "projects": [
                  {"name": "BadTime", "path": %s, "lastOpened": "yesterday"},
                  {"path": %s, "lastOpened": "2025-01-01T00:00:00Z"},
                  {"name": "NoTime", "path": %s},
                  {"name": "Relative", "path": "projects/Relative/Relative.vfproj", "lastOpened": "2025-01-01T00:00:00Z"}]}
                """.formatted(quote(root.resolve("BadTime.vfproj")), quote(root.resolve("NoName.vfproj")),
                quote(root.resolve("NoTime.vfproj"))));

        RecentProjectsView loaded = repository.load();
        repository.add(projectFile("Good"), "Good", LIMIT);

        assertEquals(4, loaded.entries().size());
        List<JsonObject> stored = readStore().getAsJsonArray("projects").asList().stream()
                .map(element -> element.getAsJsonObject()).toList();
        assertEquals(5, stored.size());
        assertEquals("yesterday", stored.get(0).get("lastOpened").getAsString());
        assertFalse(stored.get(1).has("name"), "a missing name is never made up");
        assertFalse(stored.get(2).has("lastOpened"));
        assertEquals("projects/Relative/Relative.vfproj", stored.get(3).get("path").getAsString());
        assertNull(newRepository().load().entries().stream()
                .filter(entry -> "Relative".equals(entry.name())).findFirst().orElseThrow().path());
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void anEntryWithAnInvalidWindowsPathIsKept() throws Exception {
        writeStore("""
                {"version": 2, "projects": [{"name": "Invalid", "path": "C:\\\\bad<name>\\\\Game.vfproj"}]}
                """);

        RecentEntry entry = repository.load().entries().getFirst();

        assertEquals("Invalid", entry.name());
        assertEquals("C:\\bad<name>\\Game.vfproj", entry.rawPath());
        assertNull(entry.path());
    }

    @Test
    void aNullEntryIsSkippedAndTheOthersLoad() throws IOException {
        writeStore("""
                {"version": 1, "projects": [null, {"name": "Game", "path": %s, "lastOpened": "2025-01-01T00:00:00Z"}]}
                """.formatted(quote(root.resolve("Game.vfproj"))));

        assertEquals(List.of("Game"), names(repository.load()));
        assertTrue(log.console().contains("Skipping 1 empty entry"), log.console());
    }

    @Test
    void aVersionOneFileIsMigratedWithStableIdsAndItsUnknownMembers() throws Exception {
        Path old = root.resolve("Old.vfproj");
        writeStore("""
                {"version": 1, "launcherNote": "kept", "projects": [
                  {"name": "Old", "path": %s, "lastOpened": "2025-01-01T00:00:00Z", "future": {"x": 1}}]}
                """.formatted(quote(old)));

        RecentProjectsView first = repository.load();
        RecentProjectsView second = newRepository().load();
        repository.add(projectFile("New"), "New", LIMIT);

        assertEquals(StoreStatus.LEGACY, first.status());
        UUID oldId = first.entries().getFirst().id();
        assertEquals(oldId, second.entries().getFirst().id(), "read-only loads agree on derived ids");
        JsonObject json = readStore();
        assertEquals(2, json.get("version").getAsInt());
        assertEquals("kept", json.get("launcherNote").getAsString());
        JsonObject oldEntry = json.getAsJsonArray("projects").get(0).getAsJsonObject();
        assertEquals(oldId.toString(), oldEntry.get("id").getAsString(), "the written id is the one shown");
        assertEquals(1, oldEntry.getAsJsonObject("future").get("x").getAsInt());
        assertEquals(oldId, newRepository().load().findByPath(old).orElseThrow().id());
    }

    // --- AC3: a corrupt file is kept and never overwritten -----------------------------------------------------------

    @ParameterizedTest
    @ValueSource(strings = {"{not json", "null", "", "{\"projects\":5}", "[]"})
    void aCorruptFileIsKeptAndNeverOverwritten(String content) throws Exception {
        writeStore(content);
        byte[] original = Files.readAllBytes(storeFile);

        RecentProjectsView loaded = repository.load();
        repository.add(projectFile("Game"), "Game", LIMIT);

        Path quarantined = storeFile.resolveSibling(QUARANTINE_NAME);
        assertEquals(StoreStatus.CORRUPT_QUARANTINED, loaded.status());
        assertEquals(Optional.of(quarantined), loaded.quarantinedTo());
        assertTrue(loaded.isEmpty());
        assertArrayEquals(original, Files.readAllBytes(quarantined));
        assertEquals(List.of("Game"), names(newRepository().load()));
    }

    @Test
    void aCorruptFileIsReportedToTheConsoleAndTheLog() throws IOException {
        writeStore("{not json");

        repository.load();

        assertTrue(log.console().contains(QUARANTINE_NAME), log.console());
        assertTrue(Files.readString(log.log.file()).contains(QUARANTINE_NAME));
    }

    // --- AC4: never overwrite a file from a newer schema -------------------------------------------------------------

    @Test
    void aNewerFileIsListedReadOnlyAndNeverWritten() throws Exception {
        UUID id = UUID.randomUUID();
        writeStore("""
                {"version": 3, "projects": [{"id": "%s", "name": "Future", "path": %s,
                  "lastOpened": "2025-01-01T00:00:00Z", "tags": ["x"]}], "newThing": 1}
                """.formatted(id, quote(root.resolve("Future.vfproj"))));
        byte[] original = Files.readAllBytes(storeFile);

        RecentProjectsView view = repository.load();

        assertEquals(StoreStatus.NEWER_READ_ONLY, view.status());
        assertTrue(view.readOnly());
        assertEquals(List.of("Future"), names(view));
        assertEquals(Reason.NEWER_VERSION, assertThrows(StoreWriteException.class,
                () -> repository.add(root.resolve("New.vfproj"), "New", LIMIT)).reason());
        assertEquals(Reason.NEWER_VERSION, assertThrows(StoreWriteException.class,
                () -> repository.markOpened(id)).reason());
        assertEquals(Reason.NEWER_VERSION, assertThrows(StoreWriteException.class,
                () -> repository.remove(id)).reason());
        assertArrayEquals(original, Files.readAllBytes(storeFile));
    }

    // --- save errors -------------------------------------------------------------------------------------------------

    @Test
    void aFailedSaveIsThrownAndReportedToTheConsoleAndTheLog() throws IOException {
        // The store path is a folder, so the file can be neither read nor written.
        Files.createDirectories(storeFile);

        StoreWriteException e = assertThrows(StoreWriteException.class,
                () -> repository.add(projectFile("Game"), "Game", LIMIT));

        assertEquals(Reason.UNREADABLE, e.reason());
        assertTrue(log.console().contains("Failed to save recent projects"), log.console());
        assertTrue(Files.readString(log.log.file()).contains("Warning: Failed to save recent projects"));
        assertTrue(Files.isDirectory(storeFile));
        assertTrue(repository.load().readOnly());
    }
}
