package com.vertexforge.launcher.store;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.store.StoreWriteException.Reason;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.UnaryOperator;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class AtomicJsonStoreTest {

    /** A small format: version 2, with an optional "items" list. */
    private static final DocumentFormat FORMAT = new DocumentFormat(2, OptionalInt.of(1), root -> {
        JsonElement items = root.get("items");
        if (items != null && !items.isJsonNull() && !items.isJsonArray()) {
            throw new CorruptDocumentException("\"items\" is not a list");
        }
    });
    /** TestSupport.T0 = 2026-01-02T03:04:05Z. */
    private static final String QUARANTINE_NAME = "data.json.corrupt-20260102T030405Z";

    @TempDir
    Path tempDir;

    private Path dir;
    private Path file;
    private final List<String> warnings = new CopyOnWriteArrayList<>();

    @BeforeEach
    void setUp() {
        dir = TestSupport.real(tempDir).resolve("config");
        file = dir.resolve("data.json");
    }

    private AtomicJsonStore store() {
        return store(AtomicMover.SYSTEM);
    }

    private AtomicJsonStore store(AtomicMover mover) {
        return store(mover, Duration.ofSeconds(5));
    }

    /**
     * The production retry delay: most of these stores end with the real move, which an antivirus scanner on the CI
     * runner may briefly block, and the retry is what absorbs that.
     */
    private AtomicJsonStore store(AtomicMover mover, Duration lockTimeout) {
        return new AtomicJsonStore(file, FORMAT, TestSupport.fixedClock(), lockTimeout, warnings::add, mover,
                AtomicJsonStore.DEFAULT_RETRY_DELAY);
    }

    private static UnaryOperator<JsonObject> addItem(int value) {
        return root -> {
            JsonArray items = root.has("items") ? root.getAsJsonArray("items") : new JsonArray();
            items.add(value);
            root.add("items", items);
            return root;
        };
    }

    private void writeFile(String content) throws IOException {
        Files.createDirectories(dir);
        Files.writeString(file, content, StandardCharsets.UTF_8);
    }

    private JsonObject readFile() throws IOException {
        return JsonParser.parseString(Files.readString(file)).getAsJsonObject();
    }

    private static List<Integer> items(JsonObject root) {
        return root.getAsJsonArray("items").asList().stream().map(JsonElement::getAsInt).toList();
    }

    /** Counts calls and then does the real move. */
    private static final class CountingMover implements AtomicMover {
        final AtomicInteger calls = new AtomicInteger();

        @Override
        public void move(Path source, Path target) throws IOException {
            calls.incrementAndGet();
            AtomicMover.SYSTEM.move(source, target);
        }
    }

    // --- reading -----------------------------------------------------------------------------------------------------

    @Test
    void anAbsentFileReadsEmptyAndCreatesNothing() {
        StoreSnapshot read = store().read();
        StoreSnapshot inspected = store().inspect();

        assertEquals(StoreStatus.ABSENT, read.status());
        assertEquals(StoreStatus.ABSENT, inspected.status());
        assertEquals(new JsonObject(), read.root());
        assertFalse(Files.exists(dir), "reading must not create anything");
    }

    @Test
    void aMissingVersionMeansTheFormatsDefault() throws IOException {
        writeFile("{\"items\": []}");

        StoreSnapshot snapshot = store().read();

        assertEquals(StoreStatus.LEGACY, snapshot.status());
        assertEquals(1, snapshot.fileVersion());
    }

    @Test
    void aMissingVersionIsCorruptWhenTheFormatHasNoDefault() throws IOException {
        writeFile("{}");
        AtomicJsonStore strict = new AtomicJsonStore(file, new DocumentFormat(1, OptionalInt.empty(), root -> {
        }), TestSupport.fixedClock(), Duration.ofSeconds(5), warnings::add);

        assertEquals(StoreStatus.CORRUPT, strict.inspect().status());
    }

    @Test
    void aLeadingByteOrderMarkIsAccepted() throws IOException {
        writeFile("﻿{\"version\": 2, \"items\": [7]}");

        StoreSnapshot snapshot = store().read();

        assertEquals(StoreStatus.OK, snapshot.status());
        assertEquals(List.of(7), items(snapshot.root()));
    }

    @Test
    void theSnapshotIsACopy() throws IOException {
        writeFile("{\"version\": 2, \"items\": [1]}");
        StoreSnapshot snapshot = store().read();

        snapshot.root().add("items", new JsonArray());

        assertEquals(List.of(1), items(snapshot.root()));
    }

    // --- writing -----------------------------------------------------------------------------------------------------

    @Test
    void anUpdateCreatesTheFolderAndWritesTheVersionFirst() throws Exception {
        StoreSnapshot written = store().update(addItem(1));

        assertEquals(StoreStatus.OK, written.status());
        String text = Files.readString(file);
        assertTrue(text.startsWith("{\n  \"version\": 2,"), text);
        assertEquals(List.of(1), items(readFile()));
        assertEquals(Set.of("data.json", "data.json.lock"), TestSupport.fileNames(dir));
    }

    @Test
    void anUpdateAppliesItsChangeToWhatTheFileHoldsNow() throws Exception {
        AtomicJsonStore first = store();
        AtomicJsonStore second = store();

        first.update(addItem(1));
        second.update(addItem(2));
        first.update(addItem(3));

        assertEquals(List.of(1, 2, 3), items(readFile()));
    }

    @Test
    void unknownAndNullMembersSurviveARewrite() throws Exception {
        writeFile("{\"version\": 2, \"note\": null, \"nested\": {\"a\": null, \"b\": [1, null]}, \"items\": []}");

        store().update(addItem(1));

        JsonObject root = readFile();
        assertEquals(List.of("version", "note", "nested", "items"), List.copyOf(root.keySet()));
        assertTrue(root.get("note").isJsonNull());
        assertTrue(root.getAsJsonObject("nested").get("a").isJsonNull());
        assertEquals(JsonParser.parseString("[1, null]"), root.getAsJsonObject("nested").get("b"));
        assertTrue(Files.readString(file).contains("\"note\": null"));
    }

    @Test
    void anUnchangedCurrentDocumentIsNotRewritten() throws Exception {
        store().update(addItem(1));
        CountingMover mover = new CountingMover();

        store(mover).update(root -> root);

        assertEquals(0, mover.calls.get());
    }

    @Test
    void anOlderDocumentIsRewrittenWithTheCurrentVersionEvenWhenUnchanged() throws Exception {
        writeFile("{\"version\": 1, \"items\": []}");
        CountingMover mover = new CountingMover();

        store(mover).update(root -> root);

        assertEquals(1, mover.calls.get());
        assertEquals(2, readFile().get("version").getAsInt());
    }

    @Test
    void leftoverTempFilesAreDeletedButNothingElse() throws Exception {
        store().update(addItem(1));
        Files.writeString(dir.resolve("data.json.123456789.tmp"), "stale");
        Files.writeString(dir.resolve("data.json.backup.tmp"), "user file");
        Files.writeString(dir.resolve("other.json.1.tmp"), "another store");

        store().update(addItem(2));

        assertEquals(Set.of("data.json", "data.json.lock", "data.json.backup.tmp", "other.json.1.tmp"),
                TestSupport.fileNames(dir));
    }

    // --- AC2: a failure before the move leaves the previous file intact ----------------------------------------------

    @Test
    void aFailureBeforeTheMoveLeavesThePreviousFileIntact() throws Exception {
        store().update(addItem(1));
        byte[] before = Files.readAllBytes(file);
        List<Path> sources = new CopyOnWriteArrayList<>();
        AtomicJsonStore failing = store((source, target) -> {
            sources.add(source);
            throw new IOException("injected");
        });

        StoreWriteException e = assertThrows(StoreWriteException.class, () -> failing.update(addItem(2)));

        assertEquals(Reason.IO, e.reason());
        assertArrayEquals(before, Files.readAllBytes(file));
        assertEquals(1, sources.size(), "a plain IOException is not retried");
        assertEquals(file.getParent(), sources.getFirst().getParent(), "the temp file is in the store's folder");
        assertEquals(Set.of("data.json", "data.json.lock"), TestSupport.fileNames(dir), "no temp file is left");
    }

    @Test
    void anAtomicMoveThatIsNotSupportedNeverFallsBackToACopy() throws Exception {
        store().update(addItem(1));
        byte[] before = Files.readAllBytes(file);
        AtomicInteger calls = new AtomicInteger();
        AtomicJsonStore failing = store((source, target) -> {
            calls.incrementAndGet();
            throw new AtomicMoveNotSupportedException(source.toString(), target.toString(), "not the same device");
        });

        StoreWriteException e = assertThrows(StoreWriteException.class, () -> failing.update(addItem(2)));

        assertEquals(Reason.IO, e.reason());
        assertEquals(1, calls.get());
        assertArrayEquals(before, Files.readAllBytes(file));
    }

    @Test
    void accessDeniedOnTheMoveIsRetried() throws Exception {
        store().update(addItem(1));
        AtomicInteger calls = new AtomicInteger();
        AtomicJsonStore flaky = store((source, target) -> {
            if (calls.incrementAndGet() <= 2) {
                throw new AccessDeniedException(target.toString());
            }
            AtomicMover.SYSTEM.move(source, target);
        });

        flaky.update(addItem(2));

        assertEquals(3, calls.get());
        assertEquals(List.of(1, 2), items(readFile()));
    }

    @Test
    void aSharingViolationOnTheMoveIsRetried() throws Exception {
        store().update(addItem(1));
        AtomicInteger calls = new AtomicInteger();
        AtomicJsonStore flaky = store((source, target) -> {
            if (calls.incrementAndGet() <= 2) {
                // How Windows reports "the file is being used by another process".
                throw new FileSystemException(source.toString(), target.toString(), "in use by another process");
            }
            AtomicMover.SYSTEM.move(source, target);
        });

        flaky.update(addItem(2));

        assertEquals(3, calls.get());
        assertEquals(List.of(1, 2), items(readFile()));
    }

    @Test
    void theMoveGivesUpAfterFiveAttemptsAndKeepsTheOldFile() throws Exception {
        store().update(addItem(1));
        byte[] before = Files.readAllBytes(file);
        AtomicInteger calls = new AtomicInteger();
        AtomicJsonStore denied = store((source, target) -> {
            calls.incrementAndGet();
            throw new AccessDeniedException(target.toString());
        });

        StoreWriteException e = assertThrows(StoreWriteException.class, () -> denied.update(addItem(2)));

        assertEquals(Reason.IO, e.reason());
        assertEquals(AtomicJsonStore.MOVE_ATTEMPTS, calls.get());
        assertArrayEquals(before, Files.readAllBytes(file));
        assertEquals(Set.of("data.json", "data.json.lock"), TestSupport.fileNames(dir));
    }

    @Test
    void onlyAccessDeniedAndPlainFileSystemExceptionsCountAsTransient() {
        assertTrue(AtomicJsonStore.isTransient(new AccessDeniedException("f")));
        assertTrue(AtomicJsonStore.isTransient(new FileSystemException("f")));
        assertFalse(AtomicJsonStore.isTransient(new AtomicMoveNotSupportedException("a", "b", "r")));
        assertFalse(AtomicJsonStore.isTransient(new IOException("disk full")));
    }

    // --- AC3: a corrupt file is kept and never overwritten -----------------------------------------------------------

    @ParameterizedTest
    @ValueSource(strings = {"{not json", "null", "", "   ", "[]", "\"text\"", "{\"items\": 5}", "{} {}",
            "{\"version\": 2} trailing", "{\"version\": \"two\"}", "{\"version\": 0}", "{\"version\": 1.5}"})
    void corruptContentIsMovedAsideByteForByteAndNeverOverwritten(String content) throws Exception {
        writeFile(content);
        byte[] original = Files.readAllBytes(file);

        StoreSnapshot snapshot = store().read();

        Path quarantined = dir.resolve(QUARANTINE_NAME);
        assertEquals(StoreStatus.CORRUPT_QUARANTINED, snapshot.status());
        assertEquals(Optional.of(quarantined), snapshot.quarantinedTo());
        assertEquals(new JsonObject(), snapshot.root());
        assertFalse(Files.exists(file));
        assertArrayEquals(original, Files.readAllBytes(quarantined));
        assertTrue(warnings.stream().anyMatch(w -> w.contains(QUARANTINE_NAME)), warnings.toString());

        store().update(addItem(1));

        assertArrayEquals(original, Files.readAllBytes(quarantined), "the corrupt copy is never touched again");
        assertEquals(List.of(1), items(readFile()));
    }

    @Test
    void invalidUtf8IsCorrupt() throws IOException {
        Files.createDirectories(dir);
        Files.write(file, new byte[]{'{', '"', (byte) 0xC3, (byte) 0x28, '"', ':', '1', '}'});

        assertEquals(StoreStatus.CORRUPT, store().inspect().status());
    }

    @Test
    void inspectReportsACorruptFileButNeverMovesIt() throws IOException {
        writeFile("{oops");

        StoreSnapshot snapshot = store().inspect();

        assertEquals(StoreStatus.CORRUPT, snapshot.status());
        assertTrue(snapshot.readOnly());
        assertTrue(Files.exists(file));
        assertEquals(Set.of("data.json"), TestSupport.fileNames(dir));
    }

    @Test
    void twoCorruptionsInTheSameSecondGetTheirOwnNames() throws IOException {
        writeFile("{first");
        store().read();
        writeFile("{second");

        StoreSnapshot snapshot = store().read();

        assertEquals(Optional.of(dir.resolve(QUARANTINE_NAME + "-1")), snapshot.quarantinedTo());
        assertEquals("{first", Files.readString(dir.resolve(QUARANTINE_NAME)));
        assertEquals("{second", Files.readString(dir.resolve(QUARANTINE_NAME + "-1")));
    }

    @Test
    void aFileCorruptedAfterReadingIsQuarantinedByTheNextUpdate() throws Exception {
        store().update(addItem(1));
        assertEquals(StoreStatus.OK, store().read().status());
        writeFile("{garbage");

        StoreSnapshot written = store().update(addItem(2));

        assertEquals(StoreStatus.CORRUPT_QUARANTINED, written.status());
        assertEquals(Optional.of(dir.resolve(QUARANTINE_NAME)), written.quarantinedTo());
        assertEquals("{garbage", Files.readString(dir.resolve(QUARANTINE_NAME)));
        assertEquals(List.of(2), items(readFile()));
    }

    @Test
    void aWriteThatFailsAfterMovingACorruptFileAsideStillSaysWhereItWent() throws Exception {
        writeFile("{garbage");
        AtomicJsonStore failing = store((source, target) -> {
            throw new IOException("injected");
        });

        StoreWriteException e = assertThrows(StoreWriteException.class, () -> failing.update(addItem(1)));

        assertEquals(Reason.IO, e.reason());
        assertEquals(Optional.of(dir.resolve(QUARANTINE_NAME)), e.quarantinedTo());
        assertEquals("{garbage", Files.readString(dir.resolve(QUARANTINE_NAME)));
        assertFalse(Files.exists(file));
    }

    @Test
    @Timeout(value = 30, unit = TimeUnit.SECONDS)
    void aCorruptFileThatCannotBeLockedIsReadOnlyAndKept() throws Exception {
        writeFile("{garbage");
        byte[] original = Files.readAllBytes(file);
        AtomicJsonStore impatient = store(AtomicMover.SYSTEM, Duration.ofMillis(100));

        // Another holder of the lock file: in this JVM, so the store's tryLock sees an overlapping lock.
        try (FileChannel channel = FileChannel.open(dir.resolve("data.json.lock"),
                StandardOpenOption.CREATE, StandardOpenOption.WRITE);
             FileLock ignored = channel.lock()) {

            StoreSnapshot snapshot = impatient.read();
            StoreWriteException e = assertThrows(StoreWriteException.class, () -> impatient.update(addItem(1)));

            assertEquals(StoreStatus.CORRUPT_UNMOVABLE, snapshot.status());
            assertTrue(snapshot.readOnly());
            assertEquals(Reason.LOCK_TIMEOUT, e.reason());
            assertArrayEquals(original, Files.readAllBytes(file));
        }
    }

    // --- AC4: this build never overwrites a file written by a newer schema -------------------------------------------

    @Test
    void aNewerVersionIsReadOnlyAndNeverQuarantinedWhateverItsShape() throws Exception {
        writeFile("{\"version\": 3, \"items\": 5, \"future\": true}");
        byte[] original = Files.readAllBytes(file);
        CountingMover mover = new CountingMover();
        AtomicJsonStore store = store(mover);

        StoreSnapshot snapshot = store.read();
        StoreWriteException e = assertThrows(StoreWriteException.class, () -> store.update(addItem(1)));

        assertEquals(StoreStatus.NEWER_READ_ONLY, snapshot.status());
        assertEquals(3, snapshot.fileVersion());
        assertTrue(snapshot.readOnly());
        assertTrue(snapshot.root().get("future").getAsBoolean());
        assertEquals(Reason.NEWER_VERSION, e.reason());
        assertEquals(0, mover.calls.get());
        assertArrayEquals(original, Files.readAllBytes(file));
        assertEquals(Set.of("data.json", "data.json.lock"), TestSupport.fileNames(dir));
    }

    @Test
    void aFileUpgradedAfterLoadingIsNeverOverwritten() throws Exception {
        store().update(addItem(1));
        assertEquals(StoreStatus.OK, store().read().status());
        writeFile("{\"version\": 3, \"items\": [1]}");
        byte[] upgraded = Files.readAllBytes(file);

        StoreWriteException e = assertThrows(StoreWriteException.class, () -> store().update(addItem(2)));

        assertEquals(Reason.NEWER_VERSION, e.reason());
        assertArrayEquals(upgraded, Files.readAllBytes(file));
    }

    @Test
    void anUnreadableFileIsNeverWritten() throws Exception {
        // A folder where the file should be: reading it fails with an I/O error, not a parse error.
        Files.createDirectories(file);

        StoreSnapshot snapshot = store().read();
        StoreWriteException e = assertThrows(StoreWriteException.class, () -> store().update(addItem(1)));

        assertEquals(StoreStatus.UNREADABLE, snapshot.status());
        assertTrue(snapshot.readOnly());
        assertEquals(Reason.UNREADABLE, e.reason());
        assertTrue(Files.isDirectory(file));
    }

    // --- locking -----------------------------------------------------------------------------------------------------

    @Test
    void anUpdateFromInsideAnUpdateOfTheSameFileThrows() {
        AtomicJsonStore store = store();

        assertThrows(IllegalStateException.class, () -> store.update(root -> {
            try {
                store().update(addItem(1));
            } catch (StoreWriteException e) {
                throw new AssertionError(e);
            }
            return root;
        }));
    }

    @Test
    @Timeout(value = 60, unit = TimeUnit.SECONDS)
    void twoThreadsUpdatingAtOnceKeepEveryChange() throws Exception {
        int perThread = 25;
        CyclicBarrier start = new CyclicBarrier(2);
        ExecutorService threads = Executors.newFixedThreadPool(2);
        try {
            List<Future<Void>> done = List.of(
                    threads.submit(() -> addMany(store(), start, 0, perThread)),
                    threads.submit(() -> addMany(store(), start, 1000, perThread)));
            for (Future<Void> future : done) {
                // Rethrows an OverlappingFileLockException or a failed write from either thread.
                future.get();
            }
        } finally {
            threads.shutdownNow();
        }

        List<Integer> items = items(readFile());
        assertEquals(2 * perThread, items.size());
        assertEquals(2 * perThread, Set.copyOf(items).size());
    }

    private static Void addMany(AtomicJsonStore store, CyclicBarrier start, int base, int count) throws Exception {
        start.await();
        for (int i = 0; i < count; i++) {
            store.update(addItem(base + i));
        }
        return null;
    }
}
