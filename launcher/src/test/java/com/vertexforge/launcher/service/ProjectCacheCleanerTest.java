package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CacheFolder;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CleanOutcome;
import com.vertexforge.launcher.service.ProjectCacheCleaner.CleanRequest;
import com.vertexforge.launcher.service.ProjectCacheCleaner.PreviewOutcome;
import com.vertexforge.launcher.util.TestLinks;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** "Clean Caches…" (VK-1695): only the folders the engine lists, only when they really are in the project. */
class ProjectCacheCleanerTest {

    private static final String COMPILED = "assets/scripts/compiled";

    @TempDir
    Path tempDir;

    private Path parent;
    private Path game;
    private Path projectFile;

    @BeforeEach
    void setUp() throws IOException {
        parent = Files.createDirectory(TestSupport.real(tempDir).resolve("projects"));
        game = ProjectTreeFixture.game(parent);
        projectFile = game.resolve("Game.vfproj");
    }

    private static ProjectCacheCleaner cleaner() {
        return new ProjectCacheCleaner(new EditorLockProbe(0, Duration.ZERO, duration -> {
        }));
    }

    private CleanRequest request(String... dirs) {
        return new CleanRequest(projectFile, "assets", List.of(dirs));
    }

    private List<CacheFolder> preview(String... dirs) {
        PreviewOutcome outcome = cleaner().preview(request(dirs), JobMonitor.NONE);
        return assertInstanceOf(PreviewOutcome.Ready.class, outcome, outcome::toString).folders();
    }

    private CleanOutcome.Done clean(String... dirs) {
        CleanOutcome outcome = cleaner().clean(request(dirs), JobMonitor.NONE);
        return assertInstanceOf(CleanOutcome.Done.class, outcome, outcome::toString);
    }

    /**
     * Every folder and file (with its content's hash) below the project folder, '/'-separated, except {@code excluded}
     * and what lies in it.
     */
    private Map<String, String> everythingBut(String excluded) {
        return ProjectTreeFixture.contentHashes(game, path -> path.equals(excluded)
                || path.startsWith(excluded + "/"));
    }

    @Test
    void theCompiledScriptsArePreviewedThenCleanedAndNothingElse() throws IOException {
        Files.createDirectories(game.resolve(COMPILED + "/more"));
        Files.writeString(game.resolve(COMPILED + "/more/second.mtcLib"), "x".repeat(10), StandardCharsets.UTF_8);
        Map<String, String> before = TestSupport.treeSnapshot(game);
        Map<String, String> kept = everythingBut(COMPILED);

        List<CacheFolder> folders = preview("scripts/compiled");

        assertEquals(1, folders.size());
        CacheFolder folder = folders.getFirst();
        assertEquals(CacheFolder.State.PRESENT, folder.state());
        assertEquals(COMPILED, folder.relative());
        assertEquals(game.resolve(COMPILED), folder.path());
        assertEquals("compiled".length() + 10, folder.bytes());
        assertEquals(2, folder.files());
        assertEquals(before, TestSupport.treeSnapshot(game), "the preview writes nothing");

        CleanOutcome.Done done = clean("scripts/compiled");

        assertEquals("compiled".length() + 10, done.freedBytes());
        assertEquals(List.of(COMPILED), done.cleaned().stream().map(CacheFolder::relative).toList());
        assertEquals(List.of(), done.failures());
        assertFalse(Files.exists(game.resolve(COMPILED)));
        assertEquals(kept, everythingBut(COMPILED), "only the listed folder is gone");
    }

    @Test
    void aProjectWithoutTheFolderHasNothingToClean() throws IOException {
        TestSupport.treeSnapshot(game);
        Files.delete(game.resolve(COMPILED + "/scripts.mtcLib"));
        Files.delete(game.resolve(COMPILED));

        assertEquals(CacheFolder.State.MISSING, preview("scripts/compiled").getFirst().state());
        CleanOutcome.Done done = clean("scripts/compiled");
        assertEquals(0, done.freedBytes());
        assertEquals(List.of(), done.cleaned());
        assertEquals(List.of(), done.skipped());
    }

    @Test
    void aFileWhereTheFolderShouldBeIsLeftAlone() throws IOException {
        Files.writeString(game.resolve("assets/scripts/build"), "a file, not a folder");

        CacheFolder folder = preview("scripts/build").getFirst();

        assertEquals(CacheFolder.State.SKIPPED, folder.state());
        assertTrue(folder.reason().contains("a file"), folder.reason());
        clean("scripts/build");
        assertTrue(Files.exists(game.resolve("assets/scripts/build")));
    }

    @Test
    void aJunctionAtTheFolderIsLeftAloneAndSoIsWhatItPointsTo() throws IOException {
        Path outside = Files.createDirectories(parent.resolve("outside"));
        Files.writeString(outside.resolve("precious.txt"), "keep me");
        Path compiled = game.resolve(COMPILED);
        Files.delete(compiled.resolve("scripts.mtcLib"));
        Files.delete(compiled);
        Path link = TestLinks.junction(compiled, outside);
        try {
            CacheFolder folder = preview("scripts/compiled").getFirst();
            assertEquals(CacheFolder.State.SKIPPED, folder.state());
            assertTrue(folder.reason().contains("is a link"), folder.reason());

            CleanOutcome.Done done = clean("scripts/compiled");
            assertEquals(1, done.skipped().size());
            assertTrue(Files.exists(link), "the junction itself stays");
        } finally {
            TestLinks.removeLink(link);
        }
        assertEquals("keep me", Files.readString(outside.resolve("precious.txt")));
    }

    @Test
    void aJunctionOnTheWayToTheFolderIsLeftAlone() throws IOException {
        Path outside = Files.createDirectories(parent.resolve("outside"));
        Files.createDirectories(outside.resolve("compiled"));
        Files.writeString(outside.resolve("compiled/precious.txt"), "keep me");
        Path scripts = game.resolve("assets/scripts");
        TestSupport.treeSnapshot(game);
        // Make assets/scripts a junction to the outside folder.
        Path moved = Files.move(scripts, game.resolve("assets/scripts-old"));
        Path link = TestLinks.junction(scripts, outside);
        try {
            CacheFolder folder = preview("scripts/compiled").getFirst();
            assertEquals(CacheFolder.State.SKIPPED, folder.state());
            assertTrue(folder.reason().contains("reached through a link"), folder.reason());
            clean("scripts/compiled");
        } finally {
            TestLinks.removeLink(link);
        }
        assertEquals("keep me", Files.readString(outside.resolve("compiled/precious.txt")));
        assertTrue(Files.isDirectory(moved));
    }

    @Test
    void entriesOverlappingTheProjectsOwnScriptsAreLeftAlone() throws IOException {
        Files.createDirectories(game.resolve("assets/scripts/lib/compiled"));

        for (String entry : List.of("scripts", "scripts/lib", "scripts/lib/compiled", "scripts/game",
                "scripts/scripts.mtproj")) {
            CacheFolder folder = preview(entry).getFirst();
            assertEquals(CacheFolder.State.SKIPPED, folder.state(), entry);
            assertTrue(folder.reason().contains("own scripts"), entry + ": " + folder.reason());
        }
        clean("scripts", "scripts/lib/compiled");
        assertTrue(Files.isDirectory(game.resolve("assets/scripts/lib/compiled")));
        assertTrue(Files.exists(game.resolve("assets/scripts/game/Main.mt")));
    }

    /** An old project's working directory is its folder: the editor's state folder is then at the same level. */
    @Test
    void theEditorsStateIsNeverCleanedEvenWithTheWorkingDirectoryAsTheFolder() {
        CacheFolder folder = assertInstanceOf(PreviewOutcome.Ready.class, cleaner().preview(
                new CleanRequest(projectFile, ".", List.of(".vertexforge/saves")), JobMonitor.NONE)).folders()
                .getFirst();

        assertEquals(CacheFolder.State.SKIPPED, folder.state());
        assertTrue(Files.exists(game.resolve(".vertexforge/saves/slot1.json")));
    }

    @Test
    void aWorkingDirectoryOutsideTheFolderIsRefused() {
        PreviewOutcome outcome = cleaner().preview(new CleanRequest(projectFile, "../elsewhere",
                List.of("scripts/compiled")), JobMonitor.NONE);

        assertTrue(assertInstanceOf(PreviewOutcome.Refused.class, outcome).reason().contains("outside"));
        assertInstanceOf(CleanOutcome.Refused.class, cleaner().clean(new CleanRequest(projectFile, "../elsewhere",
                List.of("scripts/compiled")), JobMonitor.NONE));
        assertTrue(Files.exists(game.resolve(COMPILED)));
    }

    @Test
    void aProjectThisLauncherHoldsIsRefused() {
        EditorLockProbe locks = new EditorLockProbe(0, Duration.ZERO, duration -> {
        });
        EditorLockProbe.Hold hold = assertInstanceOf(EditorLockProbe.HoldResult.Holding.class, locks.hold(game))
                .hold();
        try {
            assertInstanceOf(PreviewOutcome.Refused.class, cleaner().preview(request("scripts/compiled"),
                    JobMonitor.NONE));
        } finally {
            hold.close();
        }
        assertTrue(Files.exists(game.resolve(COMPILED)));
    }

    @Test
    void aReadOnlyFileThatCannotBeDeletedIsReported() throws IOException {
        Path locked = game.resolve(COMPILED + "/scripts.mtcLib");
        Files.setAttribute(locked, "dos:readonly", true);
        try {
            CleanOutcome.Done done = clean("scripts/compiled");

            assertEquals(1, done.failures().size(), done.failures()::toString);
            assertTrue(done.failures().getFirst().contains(COMPILED), done.failures().getFirst());
            assertEquals(0, done.freedBytes());
            assertTrue(Files.exists(locked), "the user's read-only flag is respected");
        } finally {
            Files.setAttribute(locked, "dos:readonly", false);
        }
    }

    @Test
    void aCancelStopsBetweenFolders() throws IOException {
        Files.createDirectories(game.resolve("assets/build/a"));
        JobMonitor cancelled = new JobMonitor() {
            @Override
            public boolean cancelRequested() {
                return true;
            }
        };

        CleanOutcome outcome = cleaner().clean(request("scripts/compiled", "build"), cancelled);

        assertEquals(new CleanOutcome.Cancelled(0, List.of()), outcome);
        assertTrue(Files.exists(game.resolve(COMPILED)));
        assertTrue(Files.exists(game.resolve("assets/build/a")));
    }

    /** Review L3: the folders were found with the working directory as first read. */
    @Test
    void aWorkingDirectoryChangedSinceThePreviewCleansNothing() throws IOException {
        Files.writeString(projectFile, ProjectTreeFixture.PROJECT_JSON.replace("\"assets\"", "\"elsewhere\""));

        CleanOutcome outcome = cleaner().clean(request("scripts/compiled"), JobMonitor.NONE);

        assertTrue(assertInstanceOf(CleanOutcome.Refused.class, outcome, outcome::toString).reason()
                .contains("working directory changed"));
        assertTrue(Files.exists(game.resolve(COMPILED)));
    }

    @Test
    void severalListedFoldersAreEachCleaned() throws IOException {
        Files.createDirectories(game.resolve("assets/build/a"));
        Files.writeString(game.resolve("assets/build/a/out.bin"), "12345");

        CleanOutcome.Done done = clean("scripts/compiled", "build");

        assertEquals(2, done.cleaned().size());
        assertEquals("compiled".length() + 5, done.freedBytes());
        assertFalse(Files.exists(game.resolve("assets/build")));
    }
}
