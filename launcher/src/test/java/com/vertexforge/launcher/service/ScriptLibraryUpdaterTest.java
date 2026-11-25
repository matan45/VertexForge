package com.vertexforge.launcher.service;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.scriptlib.FileDecision;
import com.vertexforge.launcher.scriptlib.FileDecision.Verdict;
import com.vertexforge.launcher.scriptlib.ImportCheck;
import com.vertexforge.launcher.scriptlib.LegacyLibraryHashes;
import com.vertexforge.launcher.scriptlib.LibraryBaseline;
import com.vertexforge.launcher.scriptlib.UpdateJournal;
import com.vertexforge.launcher.service.ScriptLibraryStatusService.LibraryState;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.PreviewOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.RecoveryOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.UpdateOutcome;
import com.vertexforge.launcher.template.LibraryManifest;
import com.vertexforge.launcher.util.TestLinks;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.DosFileAttributes;
import java.nio.file.attribute.FileTime;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiPredicate;
import java.util.function.Supplier;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link ScriptLibraryUpdater} on real folders (VK-1691): previews, updates, every injected failure, and the recovery
 * of every interrupted state. Every project is made as VK-1689 makes one: the engine's library copied byte for byte,
 * with a {@code .vflib.json} over the copied bytes.
 */
class ScriptLibraryUpdaterTest {

    private static final String V1 = "1.0.0";
    private static final String V2 = "1.1.0";
    /** The backup's name for a 1.0.0 library backed up at {@link TestSupport#T0}. */
    private static final String ZIP_V1 = "scripts-lib-1.0.0-20260102-030405Z.zip";
    private static final String ZIP_LEGACY = "scripts-lib-legacy-20260102-030405Z.zip";

    /** The engine's library at 1.0.0 (one file with CRLF line endings, as a Windows checkout has it). */
    private static final Map<String, String> ENGINE_V1 = engineV1();
    private static final String LOG_V2 = "public class Log {\n    public static void info(string text) {\n    }\n}\n";
    private static final String NEW_V2 = "public class New {\n}\n";
    private static final String MINE = "public class Log {\n    // my own change\n}\n";

    @TempDir
    Path tempDir;

    private Path root;
    private Path engineLib;
    private String engineVersion = V1;

    private Path folder;
    private Path projectFile;
    private String wd;
    private Path scripts;
    private Path lib;
    private Path game;

    // The seams, read when an updater is made.
    private SpaceProbe space = target -> Long.MAX_VALUE;
    private DirectoryMover mover = DirectoryMover.SYSTEM;
    private ScriptLibraryUpdater.Checkpoint checkpoint = ScriptLibraryUpdater.Checkpoint.NONE;
    private Supplier<LegacyLibraryHashes> legacy = LegacyLibraryHashes::bundled;
    private int ids;

    private static Map<String, String> engineV1() {
        Map<String, String> files = new LinkedHashMap<>();
        files.put("core/Array.mt", "public class Array {\r\n}\r\n");
        files.put("engine/Log.mt", "public class Log {\n}\n");
        files.put("engine/Entity.mt", "public class Entity {\n}\n");
        return Map.copyOf(files);
    }

    @BeforeEach
    void setUp() throws IOException {
        root = TestSupport.real(tempDir);
        engineLib = root.resolve("engine/assets/scripts/lib");
        for (Map.Entry<String, String> file : ENGINE_V1.entrySet()) {
            write(engineLib.resolve(file.getKey()), file.getValue());
        }
        createProject("Game", "assets");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Fixtures
    // -----------------------------------------------------------------------------------------------------------------

    /** A project made as VK-1689 makes one: the engine's library copied, then its {@code .vflib.json}. */
    private void createProject(String name, String workingDirectory) throws IOException {
        folder = root.resolve("projects").resolve(name);
        wd = workingDirectory;
        projectFile = write(folder.resolve(name + ".vfproj"), """
                {
                  "schemaVersion": "1.0",
                  "projectName": "%s",
                  "version": "0.1.0",
                  "workingDirectory": "%s",
                  "startupScene": "scenes/Main.vfScene",
                  "engineVersion": "VertexForge 1.0.0",
                  "assetFormatVersion": "1.0.0"
                }
                """.formatted(name, workingDirectory));
        scripts = folder.resolve(workingDirectory).normalize().resolve("scripts");
        lib = scripts.resolve("lib");
        game = scripts.resolve("game");
        write(scripts.resolve("scripts.mtproj"), "<Project Name=\"" + name + "\" Version=\"1.0.0\">\n"
                + "  <Output Directory=\"compiled\" />\n  <SearchPath>lib</SearchPath>\n</Project>\n");
        write(game.resolve("Main.mt"), "import * from \"engine/Log.mt\";\nimport * from \"engine/Entity.mt\";\n"
                + "@Script\npublic class Main {\n}\n");
        Map<String, String> hashes = new TreeMap<>();
        try (Stream<Path> files = Files.walk(engineLib)) {
            for (Path file : files.filter(Files::isRegularFile).toList()) {
                String relative = engineLib.relativize(file).toString().replace('\\', '/');
                byte[] bytes = Files.readAllBytes(file);
                Files.createDirectories(lib.resolve(relative).getParent());
                Files.write(lib.resolve(relative), bytes);
                if (LibraryManifest.isLibraryFile(relative)) {
                    hashes.put(relative, LibraryManifest.hash(bytes));
                }
            }
        }
        Files.write(lib.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson(V1, hashes));
    }

    /** The engine at 1.1.0: Log.mt changed, New.mt added. */
    private void evolveEngine() throws IOException {
        write(engineLib.resolve("engine/Log.mt"), LOG_V2);
        write(engineLib.resolve("engine/New.mt"), NEW_V2);
        engineVersion = V2;
    }

    private static Path write(Path file, String content) throws IOException {
        Files.createDirectories(file.getParent());
        return Files.writeString(file, content, StandardCharsets.UTF_8);
    }

    private static String h(String content) {
        return LibraryManifest.hash(content.getBytes(StandardCharsets.UTF_8));
    }

    private UUID nextId() {
        ids++;
        return UUID.fromString(String.format("%08x-0000-4000-8000-000000000000", 0x10000000 + ids));
    }

    private ScriptLibraryUpdater updater() {
        return new ScriptLibraryUpdater(TestSupport.fixedClock(), space, new EditorLockProbe(0, Duration.ZERO,
                duration -> {
                }), mover, this::nextId, legacy, Duration.ZERO, checkpoint);
    }

    private ScriptLibraryUpdater.Request request() {
        return new ScriptLibraryUpdater.Request(projectFile, wd, engineLib, engineVersion);
    }

    private ScriptLibraryUpdater.Preview preview() {
        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);
        return assertInstanceOf(PreviewOutcome.Ready.class, outcome, outcome::toString).preview();
    }

    private UpdateOutcome update(ScriptLibraryUpdater.Preview preview, boolean keepMine) {
        return updater().update(request(), preview.fingerprint(), keepMine, JobMonitor.NONE);
    }

    private static UpdateOutcome.Done done(UpdateOutcome outcome) {
        return assertInstanceOf(UpdateOutcome.Done.class, outcome, outcome::toString);
    }

    private Path backups() {
        return folder.resolve(".vertexforge").resolve("backups");
    }

    private static String sha(byte[] bytes) {
        return ScriptLibraryUpdater.sha256(bytes);
    }

    /** Every folder ("folder") and file (content hash and modification time) below {@code dir}, by relative path. */
    private static SortedMap<String, String> contents(Path dir) {
        return snapshot(dir, true);
    }

    /** As {@link #contents}, without the times: what the files hold. */
    private static SortedMap<String, String> bytes(Path dir) {
        return snapshot(dir, false);
    }

    private static SortedMap<String, String> snapshot(Path dir, boolean times) {
        SortedMap<String, String> snapshot = new TreeMap<>();
        try (Stream<Path> paths = Files.walk(dir)) {
            for (Path path : paths.toList()) {
                if (path.equals(dir)) {
                    continue;
                }
                String relative = dir.relativize(path).toString().replace('\\', '/');
                if (Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) {
                    snapshot.put(relative, "folder");
                } else {
                    snapshot.put(relative, sha(Files.readAllBytes(path))
                            + (times ? " " + Files.getLastModifiedTime(path).toMillis() : ""));
                }
            }
            return snapshot;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /** What the engine's library would make of the project: its files, and a record of them, without times. */
    private SortedMap<String, String> engineLibraryWithRecord(String version) throws IOException {
        SortedMap<String, String> expected = new TreeMap<>();
        Map<String, String> hashes = new TreeMap<>();
        try (Stream<Path> paths = Files.walk(engineLib)) {
            for (Path path : paths.toList()) {
                if (path.equals(engineLib)) {
                    continue;
                }
                String relative = engineLib.relativize(path).toString().replace('\\', '/');
                if (Files.isDirectory(path)) {
                    expected.put(relative, "folder");
                } else if (LibraryManifest.isLibraryFile(relative)) {
                    byte[] content = Files.readAllBytes(path);
                    expected.put(relative, sha(content));
                    hashes.put(relative, LibraryManifest.hash(content));
                }
            }
        }
        expected.put(LibraryManifest.FILE_NAME, sha(LibraryManifest.toJson(version, hashes)));
        return expected;
    }

    /** The names directly in {@code dir}. */
    private static Set<String> names(Path dir) {
        return TestSupport.fileNames(dir);
    }

    /** No journal, staging or previous folder is left beside the library. */
    private void assertNoLeftovers() {
        assertEquals(Set.of("game", "lib", "scripts.mtproj"), names(scripts));
    }

    private static Map<String, byte[]> zipFiles(Path zip) throws IOException {
        Map<String, byte[]> files = new TreeMap<>();
        try (ZipFile file = new ZipFile(zip.toFile())) {
            for (Enumeration<? extends ZipEntry> entries = file.entries(); entries.hasMoreElements(); ) {
                ZipEntry entry = entries.nextElement();
                try (InputStream in = file.getInputStream(entry)) {
                    files.put(entry.getName(), in.readAllBytes());
                }
            }
        }
        return files;
    }

    private static LegacyLibraryHashes legacyHashes(Map<String, List<String>> known) {
        JsonObject files = new JsonObject();
        known.forEach((path, list) -> {
            JsonArray array = new JsonArray();
            list.forEach(array::add);
            files.add(path, array);
        });
        return LegacyLibraryHashes.parse("{\"formatVersion\": 1, \"hashAlgorithm\": \"SHA-256\", \"lineEndings\": "
                + "\"lf\", \"through\": \"" + "0".repeat(40) + "\", \"files\": " + files + "}");
    }

    private static Verdict verdictOf(ScriptLibraryUpdater.Preview preview, String path) {
        return preview.plan().decisions().stream()
                .filter(decision -> decision.key().equals(LibraryManifest.pathKey(path)))
                .findFirst().map(FileDecision::verdict).orElseThrow(() -> new AssertionError("no decision for " + path));
    }

    /** A mover that fails, while {@code active}, every rename {@code when} picks; the rest are real. */
    private static DirectoryMover failing(BiPredicate<String, String> when, AtomicBoolean active) {
        return (source, target) -> {
            if (active.get() && when.test(source.getFileName().toString(), target.getFileName().toString())) {
                throw new IOException("simulated rename failure");
            }
            DirectoryMover.SYSTEM.move(source, target);
        };
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The acceptance paths
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aNewProjectsLibraryIsUpToDateAndNothingIsWritten() {
        ScriptLibraryUpdater.Preview preview = preview();

        assertTrue(preview.plan().upToDate());
        assertTrue(preview.hadLibrary());
        assertInstanceOf(LibraryBaseline.Recorded.class, preview.baseline());
        assertEquals(List.of(), preview.brokenImports());
        assertEquals(lib, preview.library());
        assertEquals(backups(), preview.backupFolder());

        assertInstanceOf(UpdateOutcome.UpToDate.class, update(preview, false));
        assertFalse(Files.exists(backups()), "no backup for an update that changes nothing");
        assertNoLeftovers();
    }

    @Test
    void aFileBothChangedIsAConflictAndTheUsersCopyIsInTheBackup() throws IOException {
        write(lib.resolve("engine/Log.mt"), MINE);
        byte[] mine = Files.readAllBytes(lib.resolve("engine/Log.mt"));
        evolveEngine();

        ScriptLibraryUpdater.Preview preview = preview();
        assertEquals(1, preview.conflicts());
        assertEquals(List.of("engine/Log.mt"), preview.plan().decisions(Verdict.CONFLICT).stream()
                .map(FileDecision::displayPath).toList());
        assertEquals(Verdict.ADD, verdictOf(preview, "engine/New.mt"));
        assertEquals(Verdict.NOOP, verdictOf(preview, "core/Array.mt"));
        assertEquals(Verdict.NOOP, verdictOf(preview, "engine/Entity.mt"));

        UpdateOutcome.Done done = done(update(preview, false));

        assertEquals(backups().resolve(ZIP_V1), done.backup());
        assertEquals(LOG_V2, Files.readString(lib.resolve("engine/Log.mt")), "the engine's copy replaced it");
        Map<String, byte[]> zip = zipFiles(done.backup());
        assertArrayEquals(mine, zip.get("lib/engine/Log.mt"), "the user's copy is in the backup");
        assertTrue(zip.containsKey("lib/"));
        assertTrue(zip.containsKey("lib/" + LibraryManifest.FILE_NAME));
        assertTrue(zip.containsKey("lib/core/Array.mt"));
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
        assertNoLeftovers();
        assertEquals(Set.of(ZIP_V1), names(backups()));
    }

    @Test
    void theNewRecordHoldsTheEnginesHashesAndVersion() throws Exception {
        write(lib.resolve("engine/Log.mt"), MINE);
        evolveEngine();

        done(update(preview(), true));

        LibraryManifest.Contents record = LibraryManifest.parse(Files.readAllBytes(lib.resolve(".vflib.json")));
        assertEquals(V2, record.engineVersion());
        assertEquals(h(LOG_V2), record.files().get("engine/Log.mt"), "the engine's hash, even for a kept file");
        assertEquals(Set.of("core/Array.mt", "engine/Entity.mt", "engine/Log.mt", "engine/New.mt"),
                record.files().keySet());
    }

    @Test
    void theGameScriptsAndTheProjectFilesAreOnlyRead() throws IOException {
        Map<String, String> gameBefore = TestSupport.treeSnapshot(game);
        String mtproj = Files.readString(scripts.resolve("scripts.mtproj"));
        String vfproj = Files.readString(projectFile);
        evolveEngine();

        done(update(preview(), false));

        assertEquals(gameBefore, TestSupport.treeSnapshot(game), "byte- and time-identical");
        assertEquals(mtproj, Files.readString(scripts.resolve("scripts.mtproj")));
        assertEquals(vfproj, Files.readString(projectFile));
    }

    @Test
    void aSecondRunFindsNothingToDoAndWritesNoSecondBackup() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview first = preview();
        done(update(first, false));
        SortedMap<String, String> after = contents(lib);

        ScriptLibraryUpdater.Preview second = preview();

        assertTrue(second.plan().upToDate(), second.plan().decisions().toString());
        assertInstanceOf(UpdateOutcome.UpToDate.class, update(second, false));
        UpdateOutcome stale = update(first, false);
        UpdateOutcome.Changed changed = assertInstanceOf(UpdateOutcome.Changed.class, stale, stale::toString);
        assertTrue(changed.preview().plan().upToDate(), "a stale confirmation is asked again, about nothing");
        assertEquals(Set.of(ZIP_V1), names(backups()));
        assertEquals(after, contents(lib));
    }

    @Test
    void keepMineKeepsTheUsersCopyAndTheNextRunIsUpToDate() throws IOException {
        write(lib.resolve("engine/Log.mt"), MINE);
        evolveEngine();

        UpdateOutcome.Done done = done(update(preview(), true));

        assertTrue(done.plan().keepMine());
        assertEquals(MINE, Files.readString(lib.resolve("engine/Log.mt")));
        assertEquals(NEW_V2, Files.readString(lib.resolve("engine/New.mt")));
        assertArrayEquals(MINE.getBytes(StandardCharsets.UTF_8), zipFiles(done.backup()).get("lib/engine/Log.mt"),
                "the library is backed up whatever the choice");

        ScriptLibraryUpdater.Preview next = preview();
        assertTrue(next.plan().upToDate());
        assertEquals(Verdict.KEEP_EDIT, verdictOf(next, "engine/Log.mt"), "now a change of the user's only");
    }

    @Test
    void aKeptFileThatTheEngineChangesAgainIsAConflictAgain() throws IOException {
        write(lib.resolve("engine/Log.mt"), MINE);
        evolveEngine();
        done(update(preview(), true));

        write(engineLib.resolve("engine/Log.mt"), "public class Log {\n    // 1.2.0\n}\n");
        engineVersion = "1.2.0";

        assertEquals(Verdict.CONFLICT, verdictOf(preview(), "engine/Log.mt"));
    }

    @Test
    void aFileTheUserDeletedIsRestored() throws IOException {
        Files.delete(lib.resolve("engine/Entity.mt"));

        ScriptLibraryUpdater.Preview preview = preview();
        assertEquals(Verdict.RESTORE, verdictOf(preview, "engine/Entity.mt"));
        assertFalse(preview.plan().upToDate());

        done(update(preview, false));

        assertEquals(ENGINE_V1.get("engine/Entity.mt"), Files.readString(lib.resolve("engine/Entity.mt")));
        assertTrue(preview().plan().upToDate());
    }

    @Test
    void aFileTheEngineRemovedIsDeletedWithItsSidecar() throws IOException {
        write(lib.resolve("engine/Entity.mt.vfmeta"), "{\"guid\": \"entity\"}\n");
        Files.delete(engineLib.resolve("engine/Entity.mt"));
        engineVersion = V2;

        ScriptLibraryUpdater.Preview preview = preview();
        assertEquals(Verdict.DELETE, verdictOf(preview, "engine/Entity.mt"));
        assertEquals(List.of("engine/Entity.mt.vfmeta"), preview.plan().droppedSidecars());

        UpdateOutcome.Done done = done(update(preview, false));

        assertFalse(Files.exists(lib.resolve("engine/Entity.mt")));
        assertFalse(Files.exists(lib.resolve("engine/Entity.mt.vfmeta")));
        Map<String, byte[]> zip = zipFiles(done.backup());
        assertTrue(zip.containsKey("lib/engine/Entity.mt"));
        assertTrue(zip.containsKey("lib/engine/Entity.mt.vfmeta"));
    }

    @Test
    void anUpdatedFileKeepsItsSidecarAndItsGuid() throws IOException {
        Path sidecar = write(lib.resolve("engine/Log.mt.vfmeta"), "{\"guid\": \"0f3c\"}\n");
        Files.setLastModifiedTime(sidecar, FileTime.from(Instant.parse("2025-05-05T05:05:05Z")));
        String before = contents(lib).get("engine/Log.mt.vfmeta");
        evolveEngine();

        ScriptLibraryUpdater.Preview preview = preview();
        assertEquals(Verdict.UPDATE, verdictOf(preview, "engine/Log.mt"));
        done(update(preview, false));

        assertEquals(LOG_V2, Files.readString(lib.resolve("engine/Log.mt")));
        assertEquals(before, contents(lib).get("engine/Log.mt.vfmeta"), "same bytes, same time");
    }

    @Test
    void aFileOfTheUsersOwnIsKept() throws IOException {
        write(lib.resolve("mine/Tool.mt"), "public class Tool {\n}\n");
        write(lib.resolve("notes.txt"), "remember\n");
        Files.createDirectories(lib.resolve("scratch/empty"));
        SortedMap<String, String> before = contents(lib);
        evolveEngine();

        ScriptLibraryUpdater.Preview preview = preview();
        assertEquals(Verdict.USER_FILE, verdictOf(preview, "mine/Tool.mt"));
        assertEquals(Verdict.USER_FILE, verdictOf(preview, "notes.txt"));
        done(update(preview, false));

        SortedMap<String, String> after = contents(lib);
        for (String kept : List.of("mine", "mine/Tool.mt", "notes.txt", "scratch", "scratch/empty")) {
            assertEquals(before.get(kept), after.get(kept), kept);
        }
    }

    @Test
    void aLegacyProjectsKnownFilesAreUpdatedAndOnlyUnknownOnesAreConflicts() throws Exception {
        Files.delete(lib.resolve(LibraryManifest.FILE_NAME));
        write(lib.resolve("core/Array.mt"), "old array\n");
        write(lib.resolve("engine/Log.mt"), "old log\r\n");
        write(lib.resolve("engine/Entity.mt"), "my entity\n");
        legacy = () -> legacyHashes(Map.of("core/Array.mt", List.of(h("old array\n")),
                "engine/Log.mt", List.of(h("old log\n")), "engine/Entity.mt", List.of(h("another entity\n"))));

        ScriptLibraryUpdater.Preview preview = preview();

        LibraryBaseline.Legacy baseline = assertInstanceOf(LibraryBaseline.Legacy.class, preview.baseline());
        assertTrue(baseline.note().contains("made before script libraries were tracked"), baseline.note());
        assertEquals(Verdict.UPDATE, verdictOf(preview, "core/Array.mt"));
        assertEquals(Verdict.UPDATE, verdictOf(preview, "engine/Log.mt"), "line endings never count");
        assertEquals(Verdict.CONFLICT, verdictOf(preview, "engine/Entity.mt"));

        UpdateOutcome.Done done = done(update(preview, false));

        assertEquals(backups().resolve(ZIP_LEGACY), done.backup());
        assertEquals(engineLibraryWithRecord(V1), bytes(lib));
        assertInstanceOf(LibraryBaseline.Recorded.class, preview().baseline(), "a baseline is recorded now");
        assertTrue(preview().plan().upToDate());
    }

    @Test
    void aProjectWithoutALibraryGetsTheEnginesAndNoBackup() throws IOException {
        deleteTree(lib);
        evolveEngine();

        ScriptLibraryUpdater.Preview preview = preview();
        assertFalse(preview.hadLibrary());
        assertEquals(4, preview.plan().counts().get(Verdict.ADD));
        assertTrue(((LibraryBaseline.Legacy) preview.baseline()).note().contains("has no script library"));

        UpdateOutcome.Done done = done(update(preview, false));

        assertNull(done.backup());
        assertFalse(Files.exists(backups()));
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
        assertNoLeftovers();
        assertTrue(preview().plan().upToDate());
    }

    @Test
    void theEnginesOwnSidecarsAndRecordAreNeverCopied() throws IOException {
        write(engineLib.resolve("engine/Log.mt.vfmeta"), "{\"guid\": \"engine's\"}\n");
        write(engineLib.resolve(LibraryManifest.FILE_NAME), "{}");
        deleteTree(lib);

        done(update(preview(), false));

        assertFalse(Files.exists(lib.resolve("engine/Log.mt.vfmeta")));
        assertNotEquals("{}", Files.readString(lib.resolve(LibraryManifest.FILE_NAME)));
    }

    @Test
    void theProjectsSpellingIsFixedOnDisk() throws IOException {
        // Two steps each: a rename that only changes the case is a no-op for Files.move on Windows.
        Files.move(lib.resolve("engine"), lib.resolve("engine-tmp"));
        Files.move(lib.resolve("engine-tmp"), lib.resolve("Engine"));
        Files.move(lib.resolve("core/Array.mt"), lib.resolve("core/tmp.mt"));
        Files.move(lib.resolve("core/tmp.mt"), lib.resolve("core/array.MT"));
        assertTrue(names(lib).contains("Engine") && names(lib.resolve("core")).contains("array.MT"));

        ScriptLibraryUpdater.Preview preview = preview();
        assertFalse(preview.plan().upToDate());
        assertEquals(Set.of("Engine/Entity.mt", "Engine/Log.mt", "core/array.MT"),
                Set.copyOf(preview.plan().renamed()));

        done(update(preview, false));

        assertTrue(names(lib).contains("engine"), names(lib).toString());
        assertFalse(names(lib).contains("Engine"));
        assertTrue(names(lib.resolve("core")).contains("Array.mt"), names(lib.resolve("core")).toString());
        assertTrue(preview().plan().upToDate());
    }

    @Test
    void aProjectWhoseWorkingDirectoryIsItsFolderIsUpdatedToo() throws IOException {
        createProject("Flat", ".");
        evolveEngine();

        UpdateOutcome.Done done = done(update(preview(), false));

        assertEquals(root.resolve("projects/Flat/.vertexforge/backups").resolve(ZIP_V1), done.backup());
        assertEquals(engineLibraryWithRecord(V2), bytes(root.resolve("projects/Flat/scripts/lib")));
    }

    @Test
    void thePreviewWarnsAboutAnImportTheUpdateWouldBreak() throws IOException {
        Files.delete(engineLib.resolve("engine/Entity.mt"));
        engineVersion = V2;

        assertEquals(List.of(new ImportCheck.BrokenImport("game/Main.mt", "engine/Entity.mt", null)),
                preview().brokenImports());

        write(engineLib.resolve("engine/ecs/Entity.mt"), "public class Entity {\n}\n");

        assertEquals(List.of(new ImportCheck.BrokenImport("game/Main.mt", "engine/Entity.mt", "engine/ecs/Entity.mt")),
                preview().brokenImports(), "the only file of that name is suggested");
    }

    @Test
    void anImportOfAKeptLibraryFileIsCheckedToo() throws IOException {
        write(lib.resolve("mine/Helper.mt"), "import * from \"../engine/Entity.mt\";\npublic class Helper {\n}\n");
        Files.delete(engineLib.resolve("engine/Entity.mt"));
        engineVersion = V2;

        List<ImportCheck.BrokenImport> broken = preview().brokenImports();

        assertTrue(broken.contains(new ImportCheck.BrokenImport("lib/mine/Helper.mt", "../engine/Entity.mt", null)),
                broken.toString());
    }

    @Test
    void withoutAScriptsProjectImportsAreNotCheckedAndTheUserIsTold() throws IOException {
        Files.delete(scripts.resolve("scripts.mtproj"));
        Files.delete(engineLib.resolve("engine/Entity.mt"));
        engineVersion = V2;

        ScriptLibraryUpdater.Preview preview = preview();

        assertEquals(List.of(), preview.brokenImports());
        assertTrue(preview.notes().stream().anyMatch(note -> note.contains("imports were not checked")),
                preview.notes().toString());
    }

    @Test
    void anUnreadableRecordFallsBackToTheKnownVersions() throws IOException {
        write(lib.resolve(LibraryManifest.FILE_NAME), "{ not json");

        ScriptLibraryUpdater.Preview preview = preview();

        LibraryBaseline.Legacy baseline = assertInstanceOf(LibraryBaseline.Legacy.class, preview.baseline());
        assertTrue(baseline.note().contains("cannot be read"), baseline.note());
        assertFalse(preview.plan().upToDate());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Refusals
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aRecordFromANewerLauncherIsRefused() throws IOException {
        write(lib.resolve(LibraryManifest.FILE_NAME), "{\"formatVersion\": 2, \"files\": []}");
        SortedMap<String, String> before = contents(lib);

        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);
        PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("written by a newer launcher"), refused.reason());

        UpdateOutcome update = updater().update(request(), "0".repeat(64), false, JobMonitor.NONE);
        assertInstanceOf(UpdateOutcome.Refused.class, update, update::toString);
        assertEquals(before, contents(lib));
        assertFalse(Files.exists(backups()));
    }

    @Test
    void aLibraryFromANewerEngineIsRefused() throws IOException {
        Files.write(lib.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson("2.0.0", Map.of()));

        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);

        PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("engine 2.0.0, which is newer than this engine (1.0.0)"),
                refused.reason());
    }

    @Test
    void anEngineVersionThatIsNotMajorMinorPatchIsRefused() {
        engineVersion = "1.0";

        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);

        assertInstanceOf(PreviewOutcome.Refused.class, outcome, outcome::toString);
    }

    @Test
    void aMissingOrEmptyEngineLibraryIsRefused() throws IOException {
        engineLib = root.resolve("no-such-lib");
        assertInstanceOf(PreviewOutcome.Refused.class, updater().preview(request(), JobMonitor.NONE));

        engineLib = Files.createDirectories(root.resolve("empty-lib"));
        write(engineLib.resolve("only.vfmeta"), "{}");
        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);
        PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("has no files"), refused.reason());
    }

    @Test
    void aProjectWithoutAScriptsFolderIsRefused() throws IOException {
        deleteTree(scripts);

        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);

        PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("has no scripts folder"), refused.reason());
    }

    @Test
    void aLibraryThatIsAJunctionIsRefusedAndWhatItLeadsToIsUntouched() throws IOException {
        Path elsewhere = root.resolve("elsewhere-lib");
        Files.move(lib, elsewhere);
        Path junction = TestLinks.junction(lib, elsewhere);
        try {
            SortedMap<String, String> before = contents(elsewhere);

            PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);
            PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome,
                    outcome::toString);
            assertTrue(refused.reason().contains("is not a plain folder"), refused.reason());

            UpdateOutcome update = updater().update(request(), "0".repeat(64), false, JobMonitor.NONE);
            UpdateOutcome.Refused updateRefused = assertInstanceOf(UpdateOutcome.Refused.class, update,
                    update::toString);
            assertTrue(updateRefused.reason().contains("is not a plain folder"), updateRefused.reason());

            assertEquals(before, contents(elsewhere));
            assertFalse(Files.exists(backups()));
            assertEquals(Set.of("game", "lib", "scripts.mtproj"), names(scripts));
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    @Test
    void aLinkInsideTheLibraryIsRefused() throws IOException {
        Path target = write(root.resolve("outside.mt"), "public class Outside {\n}\n");
        TestLinks.symlink(lib.resolve("engine/Linked.mt"), target);

        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);

        PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("holds something an update would lose"), refused.reason());
    }

    @Test
    void aJunctionInsideTheLibraryIsRefusedAndNeverEntered() throws IOException {
        Path elsewhere = Files.createDirectories(root.resolve("shared-scripts"));
        write(elsewhere.resolve("Shared.mt"), "public class Shared {\n}\n");
        Path junction = TestLinks.junction(lib.resolve("shared"), elsewhere);
        try {
            evolveEngine();

            PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);

            PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome,
                    outcome::toString);
            assertTrue(refused.reason().contains("holds something an update would lose")
                    && refused.reason().contains("shared is a link to another folder"), refused.reason());
            UpdateOutcome update = updater().update(request(), "0".repeat(64), false, JobMonitor.NONE);
            assertInstanceOf(UpdateOutcome.Refused.class, update, update::toString);
            assertTrue(Files.exists(elsewhere.resolve("Shared.mt")));
            assertFalse(Files.exists(backups()));
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    @Test
    void aWorkingDirectoryOutsideTheProjectFolderIsRefused() throws IOException {
        Path outside = Files.createDirectories(root.resolve("outside/assets"));
        wd = outside.toString();
        Files.createDirectories(outside.resolve("scripts/lib"));

        PreviewOutcome preview = updater().preview(request(), JobMonitor.NONE);
        PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, preview, preview::toString);
        assertTrue(refused.reason().contains("lies outside the project folder"), refused.reason());

        assertInstanceOf(UpdateOutcome.Refused.class,
                updater().update(request(), "0".repeat(64), false, JobMonitor.NONE));
        assertInstanceOf(RecoveryOutcome.Refused.class, updater().recover(projectFile, wd));
        assertFalse(Files.exists(folder.resolve(".vertexforge")), "refused before the lock is even taken");
    }

    @Test
    void aMissingWorkingDirectoryIsRefused() {
        wd = null;

        assertInstanceOf(PreviewOutcome.Refused.class, updater().preview(request(), JobMonitor.NONE));
        wd = "  ";
        assertInstanceOf(PreviewOutcome.Refused.class, updater().preview(request(), JobMonitor.NONE));
    }

    @Test
    void tooLittleSpaceIsRefusedBeforeAnythingIsWritten() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);
        space = target -> 1024;

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Refused refused = assertInstanceOf(UpdateOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("Not enough free space"), refused.reason());
        assertEquals(before, contents(lib));
        assertFalse(Files.exists(backups()));
        assertNoLeftovers();
    }

    @Test
    void aSpaceCheckThatCannotAnswerDoesNotStopTheUpdate() throws IOException {
        evolveEngine();
        space = target -> {
            throw new IOException("no answer");
        };

        UpdateOutcome.Done done = done(update(preview(), false));

        assertTrue(done.notes().stream().anyMatch(note -> note.contains("Could not check the free space")),
                done.notes().toString());
    }

    @Test
    void anUpdateConfirmedForAnotherPlanChangesNothing() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        write(lib.resolve("core/Array.mt"), "public class Array {\n    // edited after the preview\n}\n");
        SortedMap<String, String> before = contents(lib);

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Changed changed = assertInstanceOf(UpdateOutcome.Changed.class, outcome, outcome::toString);
        assertNotEquals(preview.fingerprint(), changed.preview().fingerprint());
        assertEquals(Verdict.KEEP_EDIT, verdictOf(changed.preview(), "core/Array.mt"));
        assertEquals(before, contents(lib));
        assertFalse(Files.exists(backups()));
        assertNoLeftovers();
    }

    @Test
    void anEngineChangedAfterThePreviewChangesNothing() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        write(engineLib.resolve("engine/Later.mt"), "public class Later {\n}\n");

        assertInstanceOf(UpdateOutcome.Changed.class, update(preview, false));
        assertFalse(Files.exists(lib.resolve("engine/Later.mt")));
        assertFalse(Files.exists(backups()));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Failures on the way
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aFailureAtAnyPointLeavesTheLibraryAsItWas() throws IOException {
        evolveEngine();
        write(lib.resolve("engine/Log.mt.vfmeta"), "{\"guid\": \"g\"}\n");
        SortedMap<String, String> before = contents(lib);
        Map<String, String> expected = Map.of(
                "journal", "Could not start the update",
                "built", "Could not build the new library",
                "verified", "Could not build the new library",
                "aside", "Could not move the old library aside",
                "swap", "Could not put the new library in place");

        for (String point : List.of("journal", "built", "verified", "aside", "swap")) {
            ScriptLibraryUpdater.Preview preview = preview();
            checkpoint = reached -> {
                if (reached.equals(point)) {
                    throw new IOException("injected at " + point);
                }
            };

            UpdateOutcome outcome = update(preview, false);
            checkpoint = ScriptLibraryUpdater.Checkpoint.NONE;

            UpdateOutcome.Failed failed = assertInstanceOf(UpdateOutcome.Failed.class, outcome, point + ": " + outcome);
            assertTrue(failed.message().contains(expected.get(point)), point + ": " + failed.message());
            assertTrue(failed.message().contains("injected at " + point), failed.message());
            assertTrue(failed.message().contains("stays"), "the backup is named: " + failed.message());
            assertEquals(before, contents(lib), point);
            assertNoLeftovers();
        }
        assertEquals(5, names(backups()).size(), "every backup made stays: " + names(backups()));
    }

    @Test
    void aFailedFirstRenameChangesNothing() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);
        mover = failing((source, target) -> target.startsWith("lib.old-"), new AtomicBoolean(true));

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Failed failed = assertInstanceOf(UpdateOutcome.Failed.class, outcome, outcome::toString);
        assertTrue(failed.message().contains("Could not move the old library aside"), failed.message());
        assertTrue(failed.message().contains("Nothing was changed"), failed.message());
        assertEquals(before, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void aFailedSecondRenamePutsTheOldLibraryBack() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);
        mover = failing((source, target) -> source.startsWith("lib.new-"), new AtomicBoolean(true));

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Failed failed = assertInstanceOf(UpdateOutcome.Failed.class, outcome, outcome::toString);
        assertTrue(failed.message().contains("Could not put the new library in place"), failed.message());
        assertTrue(failed.message().contains("Nothing was changed"), failed.message());
        assertEquals(before, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void whenNeitherRenameWorksTheNextRecoveryFinishesTheUpdate() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        AtomicBoolean broken = new AtomicBoolean(true);
        mover = failing((source, target) -> source.startsWith("lib.new-")
                || (source.startsWith("lib.old-") && target.equals("lib")), broken);

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Interrupted interrupted = assertInstanceOf(UpdateOutcome.Interrupted.class, outcome,
                outcome::toString);
        assertTrue(interrupted.message().contains("nor the old one back"), interrupted.message());
        assertEquals(Set.of("game", "lib.new-10000001", "lib.old-10000001", "scripts.mtproj", ".vflib-update.json"),
                names(scripts), "the first id the update took names its folders");
        PreviewOutcome again = updater().preview(request(), JobMonitor.NONE);
        assertInstanceOf(PreviewOutcome.Interrupted.class, again, again::toString);
        assertEquals(LibraryState.INTERRUPTED, ScriptLibraryStatusService.evaluate(projectFile, wd, new TreeMap<>()));

        broken.set(false);
        RecoveryOutcome recovered = updater().recover(projectFile, wd);

        RecoveryOutcome.Recovered done = assertInstanceOf(RecoveryOutcome.Recovered.class, recovered,
                recovered::toString);
        assertTrue(done.summary().contains("was finished"), done.summary());
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
        assertNoLeftovers();
        assertTrue(preview().plan().upToDate());
    }

    @Test
    void aLibraryChangedWhileTheUpdateRunsIsNotReplaced() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        checkpoint = point -> {
            if (point.equals("verified")) {
                write(lib.resolve("core/Array.mt"), "public class Array {\n    // saved meanwhile\n}\n");
            }
        };

        UpdateOutcome outcome = update(preview, false);

        assertInstanceOf(UpdateOutcome.Changed.class, outcome, outcome::toString);
        assertTrue(Files.readString(lib.resolve("core/Array.mt")).contains("saved meanwhile"));
        assertEquals(ENGINE_V1.get("engine/Log.mt"), Files.readString(lib.resolve("engine/Log.mt")));
        assertNoLeftovers();
    }

    @Test
    void anotherUpdatesJournalAppearingMeanwhileStopsThisOneAndIsLeftAlone() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);
        byte[] theirs = journal(true, null).toJson();
        checkpoint = point -> {
            if (point.equals("journal")) {
                Files.write(scripts.resolve(UpdateJournal.FILE_NAME), theirs);
            }
        };

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Refused refused = assertInstanceOf(UpdateOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("Another update of this library started meanwhile"), refused.reason());
        assertArrayEquals(theirs, Files.readAllBytes(scripts.resolve(UpdateJournal.FILE_NAME)), "not ours to undo");
        assertEquals(before, contents(lib));
        assertEquals(Set.of("game", "lib", "scripts.mtproj", UpdateJournal.FILE_NAME), names(scripts));
    }

    @Test
    void aPathThatIsAFileInOneLibraryAndAFolderInTheOtherIsRefused() throws IOException {
        write(lib.resolve("engine/oop"), "a file of the user's, without an extension\n");
        write(engineLib.resolve("engine/oop/Behaviour.mt"), "public class Behaviour {\n}\n");
        engineVersion = V2;

        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);

        PreviewOutcome.Refused refused = assertInstanceOf(PreviewOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("engine/oop would have to be both a file and a folder"),
                refused.reason());
        // The update refuses it too. A stale confirmation is answered with a new preview today (see the disabled test
        // below); asked with that preview's fingerprint, the update refuses.
        UpdateOutcome update = updater().update(request(), "0".repeat(64), false, JobMonitor.NONE);
        if (update instanceof UpdateOutcome.Changed changed) {
            update = updater().update(request(), changed.preview().fingerprint(), false, JobMonitor.NONE);
        }
        UpdateOutcome.Refused updateRefused = assertInstanceOf(UpdateOutcome.Refused.class, update, update::toString);
        assertTrue(updateRefused.reason().contains("would have to be both a file and a folder"),
                updateRefused.reason());
        assertFalse(Files.exists(backups()));
        assertNoLeftovers();
    }

    @Test
    void aStaleConfirmationOfAPlanThatCannotBeBuiltIsRefusedRatherThanAskedAgain() throws IOException {
        write(lib.resolve("engine/oop"), "a file of the user's, without an extension\n");
        write(engineLib.resolve("engine/oop/Behaviour.mt"), "public class Behaviour {\n}\n");
        engineVersion = V2;

        UpdateOutcome update = updater().update(request(), "0".repeat(64), false, JobMonitor.NONE);

        UpdateOutcome.Refused refused = assertInstanceOf(UpdateOutcome.Refused.class, update, update::toString);
        assertTrue(refused.reason().contains("would have to be both a file and a folder"), refused.reason());
    }

    @Test
    void aFileChangedDuringTheSwapKeepsTheOldLibraryBesideTheNewOne() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        checkpoint = point -> {
            if (point.equals("aside")) {
                // Saved by another program after the last check, just before the old library is moved aside.
                write(lib.resolve("core/Array.mt"), "public class Array {\n    // saved during the swap\n}\n");
            }
        };

        UpdateOutcome.Done done = done(update(preview, false));

        Path previous = scripts.resolve("lib.old-10000001");
        assertTrue(done.notes().stream().anyMatch(note -> note.contains("The old library was kept in " + previous)
                && note.contains("core/Array.mt differs from its backup")), done.notes().toString());
        assertTrue(Files.readString(previous.resolve("core/Array.mt")).contains("saved during the swap"),
                "the change is not lost");
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
        assertFalse(Files.exists(scripts.resolve(UpdateJournal.FILE_NAME)));
        assertTrue(preview().notes().stream().anyMatch(note -> note.contains("lib.old-10000001")),
                "the next preview names the leftover");
    }

    /** A rename that happened although it was reported as failed (a share whose answer got lost, say). */
    private static DirectoryMover movesThenFails(BiPredicate<String, String> when) {
        return (source, target) -> {
            DirectoryMover.SYSTEM.move(source, target);
            if (when.test(source.getFileName().toString(), target.getFileName().toString())) {
                throw new IOException("reported as failed after the rename");
            }
        };
    }

    @Test
    void anUndoThatFindsTheSwapDoneFinishesTheUpdate() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        mover = movesThenFails((source, target) -> target.startsWith("lib.old-"));

        // Whether apply() sees the rename done on disk, or its undo finishes the swap ("The update had gone through"),
        // the update is done and nothing is left over.
        UpdateOutcome.Done done = done(update(preview, false));

        assertEquals(backups().resolve(ZIP_V1), done.backup());
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
        assertNoLeftovers();
    }

    @Test
    void aSwapThatHappenedIntoAProjectWithoutALibraryIsReportedDone() throws IOException {
        deleteTree(lib);
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        mover = movesThenFails((source, target) -> source.startsWith("lib.new-"));

        UpdateOutcome outcome = update(preview, false);

        assertEquals(engineLibraryWithRecord(V2), bytes(lib), "the new library IS in place");
        assertInstanceOf(UpdateOutcome.Done.class, outcome, outcome::toString);
    }

    @Test
    void anUnexpectedErrorBeforeTheJournalIsAFailureWithNothingChanged() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);
        checkpoint = point -> {
            if (point.equals("journal")) {
                throw new IllegalStateException("a bug before the journal");
            }
        };

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Failed failed = assertInstanceOf(UpdateOutcome.Failed.class, outcome, outcome::toString);
        assertTrue(failed.message().contains("stopped unexpectedly"), failed.message());
        assertEquals(before, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void anUnexpectedErrorAfterTheJournalIsAnInterruptionTheRecoveryUndoes() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);
        checkpoint = point -> {
            if (point.equals("aside")) {
                throw new IllegalStateException("a bug before the swap");
            }
        };

        UpdateOutcome outcome = update(preview, false);

        UpdateOutcome.Interrupted interrupted = assertInstanceOf(UpdateOutcome.Interrupted.class, outcome,
                outcome::toString);
        assertTrue(interrupted.message().contains("stopped unexpectedly"), interrupted.message());
        assertTrue(Files.exists(scripts.resolve(UpdateJournal.FILE_NAME)));
        checkpoint = ScriptLibraryUpdater.Checkpoint.NONE;

        RecoveryOutcome recovered = updater().recover(projectFile, wd);

        assertFalse(assertInstanceOf(RecoveryOutcome.Recovered.class, recovered, recovered::toString)
                .completedUpdate());
        assertEquals(before, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void anUnexpectedErrorInTheMiddleOfTheSwapIsAnInterruptionTheRecoveryFinishes() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        checkpoint = point -> {
            if (point.equals("swap")) {
                throw new IllegalStateException("a bug between the renames");
            }
        };

        UpdateOutcome outcome = update(preview, false);

        assertInstanceOf(UpdateOutcome.Interrupted.class, outcome, outcome::toString);
        assertFalse(Files.exists(lib), "the old library is aside, the new one not yet in place");
        checkpoint = ScriptLibraryUpdater.Checkpoint.NONE;

        RecoveryOutcome recovered = updater().recover(projectFile, wd);

        RecoveryOutcome.Recovered done = assertInstanceOf(RecoveryOutcome.Recovered.class, recovered,
                recovered::toString);
        assertTrue(done.completedUpdate());
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
        assertNoLeftovers();
    }

    @Test
    void aLinkAddedToTheLibraryWhileTheUpdateRunsStopsIt() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        Path elsewhere = Files.createDirectories(root.resolve("linked-in"));
        write(elsewhere.resolve("Theirs.mt"), "public class Theirs {\n}\n");
        Path junction = lib.resolve("linked");
        checkpoint = point -> {
            if (point.equals("verified")) {
                TestLinks.junction(junction, elsewhere);
            }
        };
        try {
            UpdateOutcome outcome = update(preview, false);

            UpdateOutcome.Failed failed = assertInstanceOf(UpdateOutcome.Failed.class, outcome, outcome::toString);
            assertTrue(failed.message().contains("The library changed while the update ran; nothing was changed."),
                    failed.message());
            assertEquals(ENGINE_V1.get("engine/Log.mt"), Files.readString(lib.resolve("engine/Log.mt")));
            assertTrue(Files.exists(elsewhere.resolve("Theirs.mt")));
            assertNoLeftovers();
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // backupMismatch
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aFolderEqualToItsBackupMatches() throws IOException {
        Path zip = zipOf(lib);

        assertNull(ScriptLibraryUpdater.backupMismatch(lib, zip, "lib"));
    }

    @Test
    void aFolderThatDiffersFromItsBackupSaysHow() throws IOException {
        Path zip = zipOf(lib);
        assertEquals("it has no backup", ScriptLibraryUpdater.backupMismatch(lib, null, "lib"));
        assertEquals("it holds 4 files and its backup 0", ScriptLibraryUpdater.backupMismatch(lib, zip, "other"),
                "only the entries under the root entry count");

        // Same size, other bytes: the CRC tells.
        String array = Files.readString(lib.resolve("core/Array.mt"));
        write(lib.resolve("core/Array.mt"), array.replace("Array", "Brray"));
        assertEquals("core/Array.mt differs from its backup", ScriptLibraryUpdater.backupMismatch(lib, zip, "lib"));
        write(lib.resolve("core/Array.mt"), array);

        Files.move(lib.resolve("engine/Log.mt"), lib.resolve("engine/Renamed.mt"));
        assertEquals("engine/Renamed.mt is not in its backup", ScriptLibraryUpdater.backupMismatch(lib, zip, "lib"));
        Files.move(lib.resolve("engine/Renamed.mt"), lib.resolve("engine/Log.mt"));
        assertNull(ScriptLibraryUpdater.backupMismatch(lib, zip, "lib"));

        Files.delete(lib.resolve("engine/Entity.mt"));
        assertEquals("it holds 3 files and its backup 4", ScriptLibraryUpdater.backupMismatch(lib, zip, "lib"));
    }

    @Test
    void aFolderWithALinkOrABackupThatCannotBeReadNeverMatches() throws IOException {
        Path zip = zipOf(lib);
        Path missing = backups().resolve("scripts-lib-gone.zip");
        assertTrue(ScriptLibraryUpdater.backupMismatch(lib, missing, "lib").contains("could not be read"));
        assertTrue(ScriptLibraryUpdater.backupMismatch(root.resolve("no-such-folder"), zip, "lib")
                .startsWith("it could not be read"));

        Path elsewhere = Files.createDirectories(root.resolve("elsewhere"));
        Path junction = TestLinks.junction(lib.resolve("linked"), elsewhere);
        try {
            assertTrue(ScriptLibraryUpdater.backupMismatch(lib, zip, "lib").startsWith("it holds a link"));
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    @Test
    void theBackupAnUpdateWritesMatchesTheLibraryItBackedUp() throws IOException {
        write(lib.resolve("engine/Log.mt.vfmeta"), "{\"guid\": \"g\"}\n");
        Files.createDirectories(lib.resolve("scratch/empty"));
        Path copy = root.resolve("copy-of-old");
        copyTree(lib, copy);
        evolveEngine();

        UpdateOutcome.Done done = done(update(preview(), false));

        assertNull(ScriptLibraryUpdater.backupMismatch(copy, done.backup(), "lib"));
    }

    private static void copyTree(Path from, Path to) throws IOException {
        try (Stream<Path> paths = Files.walk(from)) {
            for (Path path : paths.toList()) {
                Path target = to.resolve(from.relativize(path).toString());
                if (Files.isDirectory(path)) {
                    Files.createDirectories(target);
                } else {
                    Files.write(target, Files.readAllBytes(path));
                }
            }
        }
    }

    @Test
    void aCancelWhileBuildingLeavesTheLibraryAsItWas() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);

        UpdateOutcome outcome = updater().update(request(), preview.fingerprint(), false,
                cancellingAt(JobMonitor.Phase.COPYING));

        assertInstanceOf(UpdateOutcome.Cancelled.class, outcome, outcome::toString);
        assertEquals(before, contents(lib));
        assertNoLeftovers();
        assertEquals(Set.of(ZIP_V1), names(backups()), "the backup made stays");
    }

    @Test
    void aCancelWhileBackingUpLeavesNothingBehind() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        SortedMap<String, String> before = contents(lib);

        UpdateOutcome outcome = updater().update(request(), preview.fingerprint(), false,
                cancellingAt(JobMonitor.Phase.WRITING));

        assertInstanceOf(UpdateOutcome.Cancelled.class, outcome, outcome::toString);
        assertEquals(before, contents(lib));
        assertNoLeftovers();
        assertEquals(Set.of(), names(backups()), "no zip and no .partial");
    }

    @Test
    void theSwapReportsItsStepsAndCannotBeCancelled() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        List<String> committing = new ArrayList<>();
        JobMonitor monitor = new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                if (phase == Phase.COMMITTING) {
                    committing.add(done + "/" + total);
                }
            }

            @Override
            public boolean cancelRequested() {
                return !committing.isEmpty();
            }
        };

        UpdateOutcome outcome = updater().update(request(), preview.fingerprint(), false, monitor);

        done(outcome);
        assertEquals(List.of("0/2", "1/2", "2/2"), committing);
    }

    private static JobMonitor cancellingAt(JobMonitor.Phase phase) {
        return new JobMonitor() {
            private boolean cancel;

            @Override
            public void progress(Phase reported, long done, long total) {
                cancel |= reported == phase;
            }

            @Override
            public boolean cancelRequested() {
                return cancel;
            }
        };
    }

    @Test
    void aReadOnlyOldLibraryIsStillReplacedAndRemoved() throws IOException {
        evolveEngine();
        for (String path : List.of("core/Array.mt", "engine/Log.mt", "engine/Entity.mt", "engine")) {
            Files.setAttribute(lib.resolve(path), "dos:readonly", true);
        }
        ScriptLibraryUpdater.Preview preview = preview();

        UpdateOutcome.Done done = done(update(preview, false));

        assertTrue(done.notes().stream().noneMatch(note -> note.contains("still in")), done.notes().toString());
        assertNoLeftovers();
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
        assertFalse(Files.readAttributes(lib.resolve("core/Array.mt"), DosFileAttributes.class).isReadOnly(),
                "the new library's files are written new, never copied with their flags");
    }

    @Test
    void anUpdateFinishesAnInterruptedOneFirst() throws IOException {
        evolveEngine();
        ScriptLibraryUpdater.Preview preview = preview();
        // An update that died after the swap, with only its journal left.
        Files.write(scripts.resolve(UpdateJournal.FILE_NAME), journal(true, null).toJson());

        UpdateOutcome.Done done = done(update(preview, false));

        assertTrue(done.notes().stream().anyMatch(note -> note.contains("was cleaned up")), done.notes().toString());
        assertNoLeftovers();
        assertEquals(engineLibraryWithRecord(V2), bytes(lib));
    }

    @Test
    void aPreviewOfAnInterruptedUpdateSaysSo() throws IOException {
        Files.write(scripts.resolve(UpdateJournal.FILE_NAME), journal(true, null).toJson());

        PreviewOutcome outcome = updater().preview(request(), JobMonitor.NONE);

        assertInstanceOf(PreviewOutcome.Interrupted.class, outcome, outcome::toString);
    }

    @Test
    void leftoverUpdateFoldersAreNamedButNotTouched() throws IOException {
        Path stray = Files.createDirectories(scripts.resolve("lib.old-deadbeef"));
        write(stray.resolve("keep.mt"), "x");

        ScriptLibraryUpdater.Preview preview = preview();

        assertTrue(preview.notes().stream().anyMatch(note -> note.contains("lib.old-deadbeef")
                && note.contains("leftover")), preview.notes().toString());
        assertTrue(Files.exists(stray.resolve("keep.mt")));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Recovery from every interrupted state (built by hand)
    // -----------------------------------------------------------------------------------------------------------------

    private static final String ID = "1a2b3c4d";
    private static final byte[] NEW_MANIFEST = LibraryManifest.toJson(V2, Map.of("engine/Log.mt", h(LOG_V2)));

    private Path staging() {
        return scripts.resolve("lib.new-" + ID);
    }

    private Path previous() {
        return scripts.resolve("lib.old-" + ID);
    }

    /** The backup a hand-built journal names, as the update names its backups. */
    private static final String HAND_BACKUP = "scripts-lib-1.0.0-hand.zip";

    /** A real backup of {@code folder}: every file under {@code lib/}, as the update's backup holds it. */
    private Path zipOf(Path folder) throws IOException {
        Path zip = backups().resolve(HAND_BACKUP);
        Files.createDirectories(zip.getParent());
        try (java.util.zip.ZipOutputStream out = new java.util.zip.ZipOutputStream(Files.newOutputStream(zip))) {
            out.putNextEntry(new ZipEntry("lib/"));
            out.closeEntry();
            try (Stream<Path> paths = Files.walk(folder)) {
                for (Path path : paths.filter(Files::isRegularFile).sorted().toList()) {
                    out.putNextEntry(new ZipEntry("lib/" + folder.relativize(path).toString().replace('\\', '/')));
                    out.write(Files.readAllBytes(path));
                    out.closeEntry();
                }
            }
        }
        return zip;
    }

    private static UpdateJournal journal(boolean hadLibrary, String backup) {
        return new UpdateJournal(UpdateJournal.FORMAT_VERSION, ID, "lib", "lib.new-" + ID, "lib.old-" + ID,
                hadLibrary, ScriptLibraryUpdater.sha256(NEW_MANIFEST), backup, V2, "2026-01-02T03:04:05Z");
    }

    /**
     * The journal of an update of the library as it is now, with a real backup of it (or none, for
     * {@code withBackup} false).
     */
    private void writeJournal(boolean hadLibrary, boolean withBackup) throws IOException {
        String backup = null;
        if (withBackup) {
            zipOf(Files.isDirectory(lib) ? lib : Files.createDirectories(root.resolve("nothing")));
            backup = ".vertexforge/backups/" + HAND_BACKUP;
        }
        Files.write(scripts.resolve(UpdateJournal.FILE_NAME), journal(hadLibrary, backup).toJson());
    }

    /** A staging folder as the update builds it; complete when its .vflib.json is the journal's. */
    private void writeStaging(boolean complete) throws IOException {
        write(staging().resolve("engine/Log.mt"), LOG_V2);
        if (complete) {
            Files.write(staging().resolve(LibraryManifest.FILE_NAME), NEW_MANIFEST);
        }
    }

    private RecoveryOutcome recover() {
        return updater().recover(projectFile, wd);
    }

    private static String summary(RecoveryOutcome outcome) {
        return assertInstanceOf(RecoveryOutcome.Recovered.class, outcome, outcome::toString).summary();
    }

    private static String attention(RecoveryOutcome outcome) {
        return assertInstanceOf(RecoveryOutcome.NeedsAttention.class, outcome, outcome::toString).reason();
    }

    @Test
    void recoveryDeletesAStagingFolderBesideTheLibrary() throws IOException {
        SortedMap<String, String> old = contents(lib);
        writeJournal(true, true);
        writeStaging(true);

        RecoveryOutcome outcome = recover();

        assertTrue(summary(outcome).contains("was undone"));
        assertFalse(((RecoveryOutcome.Recovered) outcome).completedUpdate());
        assertEquals(old, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void recoveryFinishesASwapWhoseNewLibraryIsComplete() throws IOException {
        writeJournal(true, true);
        writeStaging(true);
        Files.move(lib, previous());
        SortedMap<String, String> staged = contents(staging());

        RecoveryOutcome outcome = recover();

        assertTrue(summary(outcome).contains("was finished"));
        assertTrue(((RecoveryOutcome.Recovered) outcome).completedUpdate());
        assertEquals(staged, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void recoveryKeepsTheOldLibraryWhenItsBackupIsGone() throws IOException {
        writeJournal(true, false);
        writeStaging(true);
        Files.move(lib, previous());
        SortedMap<String, String> old = contents(previous());

        String reason = attention(recover());

        assertTrue(reason.contains("its backup is gone"), reason);
        assertTrue(Files.isRegularFile(lib.resolve(LibraryManifest.FILE_NAME)), "the swap itself was finished");
        assertEquals(old, contents(previous()), "the only copy of the old library is never deleted");
        assertTrue(Files.exists(scripts.resolve(UpdateJournal.FILE_NAME)));
    }

    @Test
    void recoveryRollsBackASwapWhoseNewLibraryIsIncomplete() throws IOException {
        SortedMap<String, String> old = contents(lib);
        writeJournal(true, true);
        writeStaging(false);
        Files.move(lib, previous());

        assertTrue(summary(recover()).contains("was undone"));

        assertEquals(old, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void recoveryRollsBackWhenOnlyTheOldLibraryIsLeft() throws IOException {
        SortedMap<String, String> old = contents(lib);
        writeJournal(true, true);
        Files.move(lib, previous());

        assertTrue(summary(recover()).contains("was undone"));

        assertEquals(old, contents(lib));
        assertNoLeftovers();
    }

    /** The old library aside, a real backup of it, and the journal naming that backup. */
    private void writeOldLibraryWithBackup() throws IOException {
        write(previous().resolve("engine/Log.mt"), "old");
        write(previous().resolve(LibraryManifest.FILE_NAME), "{}");
        zipOf(previous());
        Files.write(scripts.resolve(UpdateJournal.FILE_NAME),
                journal(true, ".vertexforge/backups/" + HAND_BACKUP).toJson());
    }

    @Test
    void recoveryDeletesTheOldLibraryWhileItsBackupHoldsIt() throws IOException {
        writeOldLibraryWithBackup();
        Files.setAttribute(previous().resolve("engine/Log.mt"), "dos:readonly", true);
        SortedMap<String, String> current = contents(lib);

        RecoveryOutcome outcome = recover();

        RecoveryOutcome.Recovered recovered = assertInstanceOf(RecoveryOutcome.Recovered.class, outcome,
                outcome::toString);
        assertTrue(recovered.completedUpdate(), "the swap had happened: the library is the new one");
        assertTrue(recovered.summary().contains("was finished"), recovered.summary());
        assertEquals(current, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void recoveryKeepsAnOldLibraryThatDiffersFromItsBackup() throws IOException {
        writeOldLibraryWithBackup();
        write(previous().resolve("engine/Log.mt"), "changed after the backup");

        String reason = attention(recover());

        assertTrue(reason.contains("engine/Log.mt differs from its backup"), reason);
        assertTrue(Files.exists(previous().resolve("engine/Log.mt")));
        assertTrue(Files.exists(scripts.resolve(UpdateJournal.FILE_NAME)));
    }

    @Test
    void recoveryKeepsAnOldLibraryWithAFileItsBackupLacks() throws IOException {
        writeOldLibraryWithBackup();
        write(previous().resolve("engine/Added.mt"), "added after the backup");

        String reason = attention(recover());

        assertTrue(reason.contains("holds 3 files and its backup 2"), reason);
        assertTrue(Files.exists(previous().resolve("engine/Added.mt")));
    }

    @Test
    void recoveryKeepsAnOldLibraryWhoseBackupCannotBeRead() throws IOException {
        writeOldLibraryWithBackup();
        Files.writeString(backups().resolve(HAND_BACKUP), "not a zip");

        String reason = attention(recover());

        assertTrue(reason.contains("could not be read"), reason);
        assertTrue(Files.exists(previous().resolve("engine/Log.mt")));
    }

    @Test
    void recoveryFinishesAnUpdateWhoseOldLibraryIsInItsBackup() throws IOException {
        writeOldLibraryWithBackup();
        deleteTree(lib);
        writeStaging(true);
        SortedMap<String, String> staged = contents(staging());

        RecoveryOutcome outcome = recover();

        RecoveryOutcome.Recovered recovered = assertInstanceOf(RecoveryOutcome.Recovered.class, outcome,
                outcome::toString);
        assertTrue(recovered.completedUpdate());
        assertEquals(staged, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void recoveryKeepsAnOldLibraryWithoutABackup() throws IOException {
        writeJournal(true, false);
        write(previous().resolve("engine/Log.mt"), "old");

        assertTrue(attention(recover()).contains("its backup is gone"));

        assertTrue(Files.exists(previous().resolve("engine/Log.mt")));
        assertTrue(Files.exists(scripts.resolve(UpdateJournal.FILE_NAME)));
    }

    @Test
    void recoveryDeletesAJournalWhoseUpdateIsOver() throws IOException {
        SortedMap<String, String> current = contents(lib);
        writeJournal(true, true);

        assertTrue(summary(recover()).contains("was cleaned up"));

        assertEquals(current, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void recoveryTouchesNothingWhenAllThreeFoldersAreThere() throws IOException {
        writeJournal(true, true);
        writeStaging(true);
        write(previous().resolve("engine/Log.mt"), "old");
        Map<String, String> before = contents(scripts);

        assertTrue(attention(recover()).contains("are all there"));

        assertEquals(before, contents(scripts));
    }

    @Test
    void recoveryTouchesNothingWhenEveryFolderIsGone() throws IOException {
        writeJournal(true, true);
        deleteTree(lib);
        Map<String, String> before = contents(scripts);

        assertTrue(attention(recover()).contains("The library folder is gone"));

        assertEquals(before, contents(scripts));
    }

    @Test
    void recoveryUndoesAnUpdateOfAProjectThatHadNoLibrary() throws IOException {
        deleteTree(lib);
        writeJournal(false, false);
        writeStaging(true);

        assertTrue(summary(recover()).contains("was undone"));

        assertEquals(Set.of("game", "scripts.mtproj"), names(scripts));
    }

    @Test
    void recoveryDeletesAnUnreadableJournalWhenNoUpdateFolderIsAround() throws IOException {
        SortedMap<String, String> current = contents(lib);
        write(scripts.resolve(UpdateJournal.FILE_NAME), "{ half written");

        assertTrue(summary(recover()).contains("was cleaned up"));

        assertEquals(current, contents(lib));
        assertNoLeftovers();
    }

    @Test
    void recoveryLeavesAnUnreadableJournalBesideUpdateFoldersToTheUser() throws IOException {
        write(scripts.resolve(UpdateJournal.FILE_NAME), "{ half written");
        write(scripts.resolve("lib.new-deadbeef/engine/Log.mt"), "x");
        Map<String, String> before = contents(scripts);

        assertTrue(attention(recover()).contains("cannot be read"));

        assertEquals(before, contents(scripts));
    }

    @Test
    void recoveryLeavesANewerJournalToTheNewerLauncher() throws IOException {
        write(scripts.resolve(UpdateJournal.FILE_NAME), "{\"formatVersion\": 2}");
        Map<String, String> before = contents(scripts);

        assertTrue(attention(recover()).contains("newer launcher"));

        assertEquals(before, contents(scripts));
    }

    @Test
    void recoveryWithoutAJournalHasNothingToDo() {
        assertInstanceOf(RecoveryOutcome.NothingToDo.class, recover());
        assertFalse(Files.exists(folder.resolve(".vertexforge")), "no lock is taken for nothing");
    }

    @Test
    void recoveryNeverEntersAStagingFolderThatIsAJunction() throws IOException {
        Path elsewhere = Files.createDirectories(root.resolve("elsewhere"));
        write(elsewhere.resolve("precious.mt"), "keep me");
        writeJournal(true, true);
        Path junction = TestLinks.junction(staging(), elsewhere);
        try {
            assertTrue(attention(recover()).contains("is not a plain folder"));
            assertTrue(Files.exists(elsewhere.resolve("precious.mt")));
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    private static void deleteTree(Path path) throws IOException {
        List<IOException> failures = new ArrayList<>();
        com.vertexforge.launcher.util.FileTrees.deleteTreeClearingReadOnly(path, failures);
        if (!failures.isEmpty()) {
            throw failures.getFirst();
        }
    }
}
