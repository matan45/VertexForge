package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.scriptlib.UpdateJournal;
import com.vertexforge.launcher.service.ScriptLibraryStatusService.EngineLibrary;
import com.vertexforge.launcher.service.ScriptLibraryStatusService.LibraryState;
import com.vertexforge.launcher.template.LibraryManifest;
import com.vertexforge.launcher.util.TestLinks;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Map;
import java.util.Optional;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The project list's script-library tag (VK-1691): each state, the cached engine index and the journal check. */
class ScriptLibraryStatusServiceTest {

    private static final String LOG = "public class Log {\n}\n";

    @TempDir
    Path tempDir;

    private Path root;
    private Path projectFile;
    private Path scripts;
    private Path lib;
    private SortedMap<String, String> engineHashes;

    @BeforeEach
    void setUp() throws IOException {
        root = TestSupport.real(tempDir);
        projectFile = TestSupport.writeProject(root.resolve("Game"), "Game");
        scripts = root.resolve("Game/scripts");
        Files.createDirectories(scripts.resolve("lib/engine"));
        lib = scripts.resolve("lib");
        Files.writeString(lib.resolve("engine/Log.mt"), LOG);
        engineHashes = new TreeMap<>(Map.of("engine/Log.mt", hash(LOG)));
    }

    private static String hash(String content) {
        return LibraryManifest.hash(content.getBytes(StandardCharsets.UTF_8));
    }

    private void record(Map<String, String> files) throws IOException {
        Files.write(lib.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson("1.0.0", files));
    }

    private LibraryState evaluate() throws IOException {
        return ScriptLibraryStatusService.evaluate(projectFile, ".", engineHashes);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Each state
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aRecordOfExactlyTheEnginesLibraryIsUpToDate() throws IOException {
        record(engineHashes);

        assertEquals(LibraryState.UP_TO_DATE, evaluate());
    }

    @Test
    void aRecordOfAnotherLibraryIsOutdated() throws IOException {
        record(Map.of("engine/Log.mt", hash("older\n")));
        assertEquals(LibraryState.OUTDATED, evaluate());

        record(Map.of("engine/Log.mt", hash(LOG), "engine/Gone.mt", hash("x")));
        assertEquals(LibraryState.OUTDATED, evaluate(), "a file the engine removed");

        record(Map.of("ENGINE/log.mt", hash(LOG)));
        assertEquals(LibraryState.OUTDATED, evaluate(), "the record is compared by exact path");
    }

    @Test
    void theUsersOwnEditsDoNotShow() throws IOException {
        record(engineHashes);
        Files.writeString(lib.resolve("engine/Log.mt"), "edited\n");
        Files.writeString(lib.resolve("Mine.mt"), "mine\n");

        assertEquals(LibraryState.UP_TO_DATE, evaluate(), "only the preview hashes the project's files");
    }

    @Test
    void aLibraryWithoutARecordIsLegacy() throws IOException {
        assertEquals(LibraryState.LEGACY, evaluate());
    }

    @Test
    void anUnreadableRecordCountsAsNone() throws IOException {
        Files.writeString(lib.resolve(LibraryManifest.FILE_NAME), "{ broken");
        assertEquals(LibraryState.LEGACY, evaluate());

        Files.delete(lib.resolve(LibraryManifest.FILE_NAME));
        Files.createDirectory(lib.resolve(LibraryManifest.FILE_NAME));
        assertEquals(LibraryState.LEGACY, evaluate(), "a folder of that name is no record");
    }

    @Test
    void aRecordFromANewerLauncherCannotBeJudged() throws IOException {
        Files.writeString(lib.resolve(LibraryManifest.FILE_NAME), "{\"formatVersion\": 2}");

        assertEquals(LibraryState.UNKNOWN, evaluate());
    }

    @Test
    void aProjectWithAScriptsFolderButNoLibraryIsMissingOne() throws IOException {
        deleteTree(lib);

        assertEquals(LibraryState.MISSING, evaluate());
    }

    @Test
    void aProjectWithoutAScriptsFolderIsUnknown() throws IOException {
        deleteTree(scripts);

        assertEquals(LibraryState.UNKNOWN, evaluate());
    }

    @Test
    void anInterruptedUpdateWinsOverEverythingElse() throws IOException {
        record(engineHashes);
        Files.write(ScriptLibraryUpdater.journalFile(scripts), "anything".getBytes(StandardCharsets.UTF_8));

        assertEquals(LibraryState.INTERRUPTED, evaluate());
        deleteTree(lib);
        assertEquals(LibraryState.INTERRUPTED, evaluate(), "even with the library gone");
    }

    @Test
    void aWorkingDirectoryOutsideTheProjectIsUnknown() throws IOException {
        Path outside = Files.createDirectories(root.resolve("outside/scripts/lib"));
        Files.write(outside.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson("1.0.0", engineHashes));

        assertEquals(LibraryState.UNKNOWN, ScriptLibraryStatusService.evaluate(projectFile,
                root.resolve("outside").toString(), engineHashes));
        assertEquals(LibraryState.UNKNOWN, ScriptLibraryStatusService.evaluate(projectFile, null, engineHashes));
    }

    @Test
    void aLibraryThatIsAJunctionIsUnknown() throws IOException {
        Path elsewhere = root.resolve("elsewhere");
        Files.move(lib, elsewhere);
        Files.write(elsewhere.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson("1.0.0", engineHashes));
        Path junction = TestLinks.junction(lib, elsewhere);
        try {
            assertEquals(LibraryState.UNKNOWN, evaluate());
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The service
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theCheckHashesTheEngineLibraryAndNoticesWhenItChanges() throws Exception {
        Path engineLib = root.resolve("engine/lib");
        Files.createDirectories(engineLib.resolve("engine"));
        Files.writeString(engineLib.resolve("engine/Log.mt"), LOG);
        Files.writeString(engineLib.resolve("engine/Log.mt.vfmeta"), "{\"guid\": \"engine's own\"}");
        Files.writeString(engineLib.resolve(LibraryManifest.FILE_NAME), "{}");
        record(engineHashes);
        EngineLibrary engine = new EngineLibrary(engineLib, "1.0.0");

        try (ScriptLibraryStatusService service = new ScriptLibraryStatusService(Duration.ofSeconds(30))) {
            assertEquals(LibraryState.UP_TO_DATE, service.check(projectFile, ".", engine).get(30, TimeUnit.SECONDS),
                    "the engine's sidecars and record are not library files");

            Files.writeString(engineLib.resolve("engine/New.mt"), "public class New {\n}\n");
            assertEquals(LibraryState.OUTDATED, service.check(projectFile, ".", engine).get(30, TimeUnit.SECONDS),
                    "a file added to the engine's library is noticed");

            Files.delete(engineLib.resolve("engine/New.mt"));
            Files.writeString(engineLib.resolve("engine/Log.mt"), "public class Log {\n    // longer now\n}\n");
            assertEquals(LibraryState.OUTDATED, service.check(projectFile, ".", engine).get(30, TimeUnit.SECONDS),
                    "a file changed in the engine's library is noticed");

            assertEquals(LibraryState.UNKNOWN, service.check(projectFile, ".",
                    new EngineLibrary(root.resolve("no-such-lib"), "1.0.0")).get(30, TimeUnit.SECONDS));
        }
    }

    /**
     * A library a newer engine installed (its record names 2.0.0), listed with a 1.0.0 engine: the update refuses it
     * ("which is newer than this engine"), so the row must not invite the user to update it.
     */
    @Test
    void aLibraryFromANewerEngineIsNotShownAsOutdated() throws Exception {
        Path engineLib = root.resolve("engine/lib");
        Files.createDirectories(engineLib.resolve("engine"));
        Files.writeString(engineLib.resolve("engine/Log.mt"), "public class Log {\n    // 1.0.0's\n}\n");
        Files.write(lib.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson("2.0.0", engineHashes));
        EngineLibrary engine = new EngineLibrary(engineLib, "1.0.0");

        ScriptLibraryUpdater updater = new ScriptLibraryUpdater(TestSupport.fixedClock(), target -> Long.MAX_VALUE,
                new com.vertexforge.launcher.project.EditorLockProbe(0, Duration.ZERO, duration -> {
                }));
        ScriptLibraryUpdater.PreviewOutcome preview = updater.preview(new ScriptLibraryUpdater.Request(projectFile,
                ".", engineLib, "1.0.0"), JobMonitor.NONE);
        assertTrue(preview instanceof ScriptLibraryUpdater.PreviewOutcome.Refused refused
                && refused.reason().contains("newer than this engine"), preview.toString());

        try (ScriptLibraryStatusService service = new ScriptLibraryStatusService(Duration.ofSeconds(30))) {
            LibraryState state = service.check(projectFile, ".", engine).get(30, TimeUnit.SECONDS);

            assertEquals("", state.tag(), "the row offers an update the updater refuses: " + state);
        }
    }

    /** The engine's library as the updater and project creation take it: a junction to another folder is left out. */
    @Test
    void theEngineIndexLeavesOutAJunctionInTheEnginesLibrary() throws Exception {
        Path engineLib = root.resolve("engine/lib");
        Files.createDirectories(engineLib.resolve("engine"));
        Files.writeString(engineLib.resolve("engine/Log.mt"), LOG);
        Path elsewhere = Files.createDirectories(root.resolve("elsewhere"));
        Files.writeString(elsewhere.resolve("Extra.mt"), "public class Extra {\n}\n");
        record(engineHashes);
        Path junction = TestLinks.junction(engineLib.resolve("linked"), elsewhere);
        try (ScriptLibraryStatusService service = new ScriptLibraryStatusService(Duration.ofSeconds(30))) {
            assertEquals(LibraryState.UP_TO_DATE, service.check(projectFile, ".",
                    new EngineLibrary(engineLib, "1.0.0")).get(30, TimeUnit.SECONDS));
        } finally {
            TestLinks.removeLink(junction);
        }
    }

    @Test
    void theJournalCheckAnswersWhetherAnUpdateWasInterrupted() throws Exception {
        try (ScriptLibraryStatusService service = new ScriptLibraryStatusService(Duration.ofSeconds(30))) {
            assertFalse(service.interrupted(projectFile, ".").get(30, TimeUnit.SECONDS));

            Files.write(ScriptLibraryUpdater.journalFile(scripts), new UpdateJournal(1, "1a2b3c4d", "lib",
                    "lib.new-1a2b3c4d", "lib.old-1a2b3c4d", true, "0".repeat(64), null, "1.0.0", "t").toJson());

            assertTrue(service.interrupted(projectFile, ".").get(30, TimeUnit.SECONDS));
            assertFalse(service.interrupted(projectFile, root.resolve("outside").toString()).get(30, TimeUnit.SECONDS),
                    "a project the update cannot cover never counts as interrupted");
        }
    }

    @Test
    void anEngineLibraryIsTheOneItsManifestNamesBesideTheEditor() throws Exception {
        Path exe = root.resolve("bin/Editor/Development/x64/Editor.exe");
        EngineManifest withLibrary = manifest(", \"scriptLibDir\": \"../../../../assets/scripts/lib\"");
        EngineManifest without = manifest("");

        Optional<EngineLibrary> library = EngineLibrary.of(engine(exe, EngineHealth.ok(withLibrary, exe.getParent())));

        assertEquals(Optional.of(new EngineLibrary(root.resolve("assets/scripts/lib"), "1.2.3")), library);
        assertEquals(Optional.empty(), EngineLibrary.of(engine(exe, EngineHealth.ok(without, exe.getParent()))));
        assertEquals(Optional.empty(), EngineLibrary.of(engine(exe, EngineHealth.missing("gone"))));
        assertEquals(Optional.empty(), EngineLibrary.of(engine(exe, EngineHealth.broken("broken", withLibrary))));
        assertEquals(Optional.empty(), EngineLibrary.of(null));
    }

    @Test
    void everyStateButTheQuietOnesHasATagAndATooltip() {
        assertEquals("", LibraryState.UP_TO_DATE.tag());
        assertEquals("", LibraryState.UNKNOWN.tag());
        assertEquals("", LibraryState.UP_TO_DATE.tooltip());
        assertEquals("", LibraryState.UNKNOWN.tooltip());
        assertEquals("Library outdated", LibraryState.OUTDATED.tag());
        assertEquals("Library outdated", LibraryState.LEGACY.tag());
        assertEquals("Library missing", LibraryState.MISSING.tag());
        assertEquals("Library update interrupted", LibraryState.INTERRUPTED.tag());
        for (LibraryState state : new LibraryState[]{LibraryState.OUTDATED, LibraryState.LEGACY,
                LibraryState.MISSING, LibraryState.INTERRUPTED}) {
            assertTrue(state.tooltip().contains("Update Script Library"), state + ": " + state.tooltip());
        }
    }

    private static EngineManifest manifest(String extra) throws Exception {
        return EngineManifest.parse("{\"manifestVersion\": 1, \"engineVersion\": \"1.2.3\", "
                + "\"projectSchemaVersion\": \"1.0\", \"assetFormatVersion\": \"1.0.0\", "
                + "\"configuration\": \"Development\", \"editorExe\": \"Editor.exe\", \"workingDirectory\": \".\""
                + extra + "}");
    }

    private static EngineOption engine(Path exe, EngineHealth health) {
        return new EngineOption(Optional.of(UUID.randomUUID()), exe, exe.toString(), health, false, false, false);
    }

    private static void deleteTree(Path path) throws IOException {
        java.util.List<IOException> failures = new java.util.ArrayList<>();
        com.vertexforge.launcher.util.FileTrees.deleteTree(path, failures);
        if (!failures.isEmpty()) {
            throw failures.getFirst();
        }
    }
}
