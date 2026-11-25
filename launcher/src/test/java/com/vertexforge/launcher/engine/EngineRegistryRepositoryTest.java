package com.vertexforge.launcher.engine;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.store.StoreWriteException.Reason;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class EngineRegistryRepositoryTest {

    @TempDir
    Path tempDir;

    private Path root;
    private Path storeFile;
    private CapturedLog log;
    private EngineRegistryRepository repository;

    @BeforeEach
    void setUp() {
        root = TestSupport.real(tempDir);
        // A missing parent folder: the first write must create it.
        storeFile = root.resolve("config").resolve(EngineRegistryRepository.FILE_NAME);
        log = TestSupport.log(root.resolve("logs"));
        repository = newRepository();
    }

    /** A fresh repository on the same file, as another launcher (or the next start) would have. */
    private EngineRegistryRepository newRepository() {
        return new EngineRegistryRepository(
                EngineRegistryRepository.openStore(storeFile, TestSupport.fixedClock(), log.log),
                UUID::randomUUID, log.log);
    }

    private Path editor(String name) {
        return TestSupport.fakeEditor(root.resolve(name));
    }

    private JsonObject readStore() throws IOException {
        return JsonParser.parseString(Files.readString(storeFile)).getAsJsonObject();
    }

    @Test
    void anAbsentFileListsNothingAndIsNeverWrittenByALoad() {
        EngineRegistryView view = repository.load();

        assertTrue(view.engines().isEmpty());
        assertEquals(StoreStatus.ABSENT, view.status());
        assertFalse(Files.exists(storeFile));
    }

    @Test
    void anAddedEngineIsStoredAsVersionOneWithANullDefault() throws Exception {
        EngineRegistryRepository.Added added = repository.add(editor("a"));

        assertFalse(added.existed());
        JsonObject stored = readStore();
        assertEquals(List.of("version", "defaultEngineId", "engines"), List.copyOf(stored.keySet()));
        assertEquals(1, stored.get("version").getAsInt());
        assertTrue(stored.get("defaultEngineId").isJsonNull());
        assertEquals(editor("a").toString(), stored.getAsJsonArray("engines").get(0).getAsJsonObject()
                .get("path").getAsString());
        assertEquals(List.of(added.engine().id()), newRepository().load().engines().stream()
                .map(EngineInstall::id).toList());
    }

    @Test
    void addingTheSameEditorAgainWritesNothing() throws Exception {
        UUID id = repository.add(editor("a")).engine().id();
        byte[] before = Files.readAllBytes(storeFile);

        EngineRegistryRepository.Added again = newRepository().add(editor("a"));

        assertTrue(again.existed());
        assertEquals(id, again.engine().id());
        assertArrayEquals(before, Files.readAllBytes(storeFile));
    }

    @Test
    void twoLaunchersAddingAtOnceKeepBothEngines() throws Exception {
        EngineRegistryRepository other = newRepository();

        repository.add(editor("a"));
        other.add(editor("b"));

        assertEquals(Set.of(editor("a"), editor("b")), Set.copyOf(repository.load().engines().stream()
                .map(EngineInstall::executable).toList()));
    }

    @Test
    void theDefaultIsSetAndClearedWhenItsEngineIsRemoved() throws Exception {
        UUID a = repository.add(editor("a")).engine().id();
        UUID b = repository.add(editor("b")).engine().id();

        assertEquals(Optional.of(b), repository.setDefault(b).defaultEngineId());
        assertEquals(Optional.of(b), repository.remove(a).defaultEngineId());
        EngineRegistryView afterRemove = repository.remove(b);

        assertEquals(Optional.empty(), afterRemove.defaultEngineId());
        assertTrue(afterRemove.engines().isEmpty());
        assertTrue(readStore().get("defaultEngineId").isJsonNull());
        assertTrue(Files.isRegularFile(root.resolve("b/Editor.exe")), "removing never touches the engine");
    }

    @Test
    void unknownMembersSurviveAChange() throws Exception {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, "{\"version\": 1, \"launcherNote\": \"kept\", \"engines\": [{\"id\": \""
                + UUID.randomUUID() + "\", \"path\": " + new JsonPrimitive(editor("a").toString())
                + ", \"future\": {\"x\": 1}}]}");

        repository.add(editor("b"));

        JsonObject stored = readStore();
        assertEquals("kept", stored.get("launcherNote").getAsString());
        assertEquals(1, stored.getAsJsonArray("engines").get(0).getAsJsonObject()
                .getAsJsonObject("future").get("x").getAsInt());
        assertEquals(2, stored.getAsJsonArray("engines").size());
    }

    @Test
    void aNewerFileIsReadOnly() throws Exception {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, "{\"version\": 2, \"engines\": []}");
        byte[] newer = Files.readAllBytes(storeFile);

        EngineRegistryView view = repository.load();
        StoreWriteException error = assertThrows(StoreWriteException.class, () -> repository.add(editor("a")));

        assertTrue(view.readOnly());
        assertEquals(StoreStatus.NEWER_READ_ONLY, view.status());
        assertEquals(Reason.NEWER_VERSION, error.reason());
        assertArrayEquals(newer, Files.readAllBytes(storeFile));
    }

    @Test
    void aCorruptFileIsSetAsideAndANewListStarted() throws Exception {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, "{\"version\": 1, \"engines\": {\"not\": \"a list\"}}");

        EngineRegistryView view = repository.load();

        assertEquals(StoreStatus.CORRUPT_QUARANTINED, view.status());
        assertTrue(view.quarantinedTo().isPresent());
        assertTrue(Files.isRegularFile(view.quarantinedTo().get()));
        assertTrue(view.engines().isEmpty());
        assertTrue(log.console().contains("corrupt"), log.console());
    }

    @Test
    void aHandWrittenFileWithoutAVersionIsReadAsVersionOne() throws Exception {
        Files.createDirectories(storeFile.getParent());
        Files.writeString(storeFile, "{\"engines\": [{\"path\": "
                + new JsonPrimitive(editor("a").toString()) + "}]}");

        EngineRegistryView view = repository.load();

        assertEquals(StoreStatus.OK, view.status());
        assertEquals(editor("a"), view.engines().getFirst().executable());
    }
}
