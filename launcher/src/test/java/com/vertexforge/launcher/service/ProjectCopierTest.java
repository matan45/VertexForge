package com.vertexforge.launcher.service;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.project.ProjectFileReader;
import com.vertexforge.launcher.service.ProjectCopier.CopyOutcome;
import com.vertexforge.launcher.service.ProjectCopier.CopyRequest;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Predicate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class ProjectCopierTest {

    private static final UUID STAGING_ID = UUID.fromString("00000000-0000-0000-0000-000000001690");
    private static final String COPY = "Game-1.1.0";

    @TempDir
    Path tempDir;

    /** Where the project lives; the copy goes here too. Nothing else is in it. */
    private Path parent;
    private Path game;
    private Path projectFile;

    @BeforeEach
    void setUp() throws IOException {
        parent = Files.createDirectory(TestSupport.real(tempDir).resolve("projects"));
        game = ProjectTreeFixture.game(parent);
        projectFile = game.resolve("Game.vfproj");
    }

    private ProjectCopier copier() {
        return copier(DirectoryMover.SYSTEM, folder -> Long.MAX_VALUE);
    }

    private ProjectCopier copier(DirectoryMover mover, SpaceProbe space) {
        return new ProjectCopier(() -> STAGING_ID, mover, space, Duration.ZERO);
    }

    private CopyRequest request(String name) {
        return new CopyRequest(projectFile, "assets", name);
    }

    private CopyOutcome copy(String name) {
        return copier().copy(request(name), JobMonitor.NONE);
    }

    private static CopyOutcome.Done done(CopyOutcome outcome) {
        return assertInstanceOf(CopyOutcome.Done.class, outcome, outcome::toString);
    }

    private Path staging(String name) {
        return parent.resolve("." + name + ProjectCreator.STAGING_INFIX + STAGING_ID);
    }

    /** What a copy must hold byte for byte: the original minus its exclusions and its project file. */
    private Map<String, String> copiedContent(Path root) {
        Predicate<String> excluded = ProjectContent.of(projectFile, "assets").excluded();
        return ProjectTreeFixture.contentHashes(root, relative -> excluded.test(relative)
                || relative.equals("Game.vfproj"));
    }

    /** A monitor whose {@code cancelRequested} turns true at the first report of {@code phase}. */
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

    // -----------------------------------------------------------------------------------------------------------------
    // What a copy is (VK-1690 AC3)
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theCopyIsBesideTheOriginalAndTheOriginalIsUntouched() throws Exception {
        Map<String, String> before = TestSupport.treeSnapshot(game);

        CopyOutcome.Done done = done(copy(COPY));

        Path copy = parent.resolve(COPY);
        assertEquals(copy, done.folder());
        assertEquals(copy.resolve("Game.vfproj"), done.projectFile(), "the project file keeps its name");
        assertEquals(Set.of("Game", COPY), TestSupport.fileNames(parent), "no staging folder is left");
        assertEquals(before, TestSupport.treeSnapshot(game), "the original is only read");
        assertEquals(COPY, ProjectFileReader.read(done.projectFile()).projectName());
        assertEquals(copiedContent(game), copiedContent(copy));
        assertEquals(List.of(), done.notes());
    }

    @Test
    void theCopiesProjectFileChangesOnlyItsName() throws IOException {
        Path copied = done(copy(COPY)).projectFile();

        String text = Files.readString(copied, StandardCharsets.UTF_8);
        JsonObject original = JsonParser.parseString(ProjectTreeFixture.PROJECT_JSON).getAsJsonObject();
        original.addProperty("projectName", COPY);
        assertEquals(original, JsonParser.parseString(text).getAsJsonObject());
        assertEquals(ProjectTreeFixture.PROJECT_JSON.replace("\"projectName\": \"Game\"",
                "\"projectName\": \"" + COPY + "\""), text, "same order, same CRLF line ends");
    }

    @Test
    void theEditorsStateAndTheCompiledScriptsAreNotCopiedButEmptyFoldersAre() {
        Path copy = done(copy(COPY)).folder();

        assertFalse(Files.exists(copy.resolve(".vertexforge")));
        assertFalse(Files.exists(copy.resolve("assets/scripts/compiled")));
        assertTrue(Files.isDirectory(copy.resolve("assets/scenes")));
        assertTrue(Files.isRegularFile(copy.resolve("assets/scripts/game/Main.mt.vfmeta")));
    }

    @Test
    void copiedFilesKeepTheirModificationTimes() throws IOException {
        Path copy = done(copy(COPY)).folder();

        for (String file : List.of(".gitignore", "assets/scripts/game/Main.mt", "assets/scripts/game/Main.mt.vfmeta",
                "assets/textures/big.bin")) {
            assertEquals(Files.getLastModifiedTime(game.resolve(file)), Files.getLastModifiedTime(copy.resolve(file)),
                    file);
        }
    }

    @Test
    void absolutePathsIntoTheOriginalArePointedIntoTheCopy() throws IOException {
        String workingDirectory = game.resolve("assets").toString();
        String scene = game.resolve("assets/scenes/Main.vfScene").toString();
        String json = "{\n  \"schemaVersion\": \"1.0\",\n  \"projectName\": \"Game\",\n  \"version\": \"0.1.0\",\n"
                + "  \"workingDirectory\": " + quoted(workingDirectory) + ",\n  \"startupScene\": " + quoted(scene)
                + ",\n  \"engineVersion\": \"VertexForge 1.0.0\"\n}\n";
        Files.writeString(projectFile, json);

        CopyOutcome.Done done = done(copier().copy(new CopyRequest(projectFile, workingDirectory, COPY),
                JobMonitor.NONE));

        JsonObject copied = JsonParser.parseString(Files.readString(done.projectFile())).getAsJsonObject();
        assertEquals("assets", copied.get("workingDirectory").getAsString());
        assertEquals(done.folder().resolve("assets").resolve("scenes").resolve("Main.vfScene").toString(),
                copied.get("startupScene").getAsString());
        assertEquals(json, Files.readString(projectFile), "the original's file is untouched");
    }

    /** The fixture's project file with one more member, given as its JSON text. */
    private void writeProjectWith(String member) throws IOException {
        Files.writeString(projectFile, "{\n  \"schemaVersion\": \"1.0\",\n  \"projectName\": \"Game\",\n"
                + "  \"version\": \"0.1.0\",\n  \"workingDirectory\": \"assets\",\n"
                + "  \"startupScene\": \"scenes/Main.vfScene\",\n  " + member + "\n}\n");
    }

    /**
     * A hand-written spelling of the original folder that its text does not show: Windows drops a trailing dot from a
     * path's names, so the copy's editor would write the ORIGINAL's input mapping.
     */
    @Test
    void aMemberThatReachesTheOriginalByAnotherSpellingIsRefused() throws IOException {
        assumeTrue(TestSupport.isWindows(), "only Windows drops a trailing dot from a path's names");
        Files.writeString(game.resolve("input.json"), "{}");
        writeProjectWith("\"inputMapping\": \"../../Game./input.json\"");
        Map<String, String> before = TestSupport.treeSnapshot(game);

        CopyOutcome outcome = copy(COPY);

        CopyOutcome.Refused refused = assertInstanceOf(CopyOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("inputMapping"), refused.reason());
        assertTrue(refused.reason().contains("into the original project"), refused.reason());
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent), "nothing is written");
        assertEquals(before, TestSupport.treeSnapshot(game));
    }

    /** A path the editor reads but the launcher cannot (a trailing space) cannot be checked, so it is refused. */
    @Test
    void aMemberThatIsNotAPathTheLauncherCanCheckIsRefused() throws IOException {
        assumeTrue(TestSupport.isWindows(), "only Windows paths refuse a trailing space in a name");
        writeProjectWith("\"inputMapping\": \"../../Game /input.json\"");

        CopyOutcome outcome = copy(COPY);

        CopyOutcome.Refused refused = assertInstanceOf(CopyOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("not a path the launcher can check"), refused.reason());
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent), "nothing is written");
    }

    @Test
    void membersThatLeadIntoTheCopyOrOutsideBothAreFine() throws IOException {
        Path shared = Files.createDirectories(parent.resolve("Shared"));
        writeProjectWith("\"exeIconPath\": " + quoted(shared.resolve("icon.png").toString()) + ",\n"
                + "  \"inputMapping\": \"input/actions.json\"");

        CopyOutcome.Done done = done(copy(COPY));

        assertTrue(Files.isRegularFile(done.projectFile()));
    }

    @Test
    void aReadOnlySourceFileIsCopiedWritable() throws IOException {
        Path readOnly = game.resolve("assets/scripts/game/Main.mt");
        assumeTrue(readOnly.toFile().setReadOnly(), "this file system cannot make a file read-only");
        try {
            Path copy = done(copy(COPY)).folder();

            assertTrue(Files.isWritable(copy.resolve("assets/scripts/game/Main.mt")));
        } finally {
            readOnly.toFile().setWritable(true);
        }
    }

    @Test
    void reportsTheBytesItCopies() {
        List<long[]> reports = new ArrayList<>();

        done(copier().copy(request(COPY), new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                if (phase == Phase.COPYING) {
                    reports.add(new long[]{done, total});
                }
            }
        }));

        long[] last = reports.getLast();
        assertEquals(last[1], last[0], "the bar ends full");
        for (long[] report : reports) {
            assertTrue(report[0] <= report[1]);
        }
        assertTrue(reports.size() >= 5, "a report per file at least");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Refusals: nothing is written
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anExistingFileWithTheNameIsRefused() throws IOException {
        Files.writeString(parent.resolve(COPY), "a file");
        Map<String, String> before = TestSupport.treeSnapshot(parent);

        CopyOutcome outcome = copy(COPY);

        assertEquals(new CopyOutcome.Refused("A file named \"" + COPY + "\" already exists in " + parent + "."),
                outcome);
        assertEquals(before, TestSupport.treeSnapshot(parent));
    }

    @Test
    void anExistingEmptyFolderWithTheNameIsRefused() throws IOException {
        Files.createDirectory(parent.resolve(COPY));
        Map<String, String> before = TestSupport.treeSnapshot(parent);

        CopyOutcome outcome = copy(COPY);

        assertEquals(new CopyOutcome.Refused("A folder named \"" + COPY + "\" already exists in " + parent + "."),
                outcome);
        assertEquals(before, TestSupport.treeSnapshot(parent));
    }

    @Test
    void theOriginalsOwnNameInAnotherCaseIsRefused() {
        assumeTrue(TestSupport.isWindows(), "only Windows ignores case in names");

        CopyOutcome outcome = copy("GAME");

        assertEquals(new CopyOutcome.Refused("A folder named \"GAME\" already exists in " + parent + "."), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aBadNameIsRefusedWithTheNameRulesWords() {
        for (String name : List.of("", "CON", "a/b", "Game.", " Game")) {
            assertEquals(new CopyOutcome.Refused(ProjectNameRules.checkName(name).orElseThrow().message()), copy(name),
                    name);
        }
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aPathTooLongForWindowsIsRefused() {
        CopyOutcome outcome = copy("N".repeat(200));

        CopyOutcome.Refused refused = assertInstanceOf(CopyOutcome.Refused.class, outcome);
        assertTrue(refused.reason().startsWith("This location and name make a path too long for Windows"),
                refused.reason());
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aWorkingDirectoryOutsideTheFolderIsRefused() {
        CopyOutcome outcome = copier().copy(new CopyRequest(projectFile, "../Shared", COPY), JobMonitor.NONE);

        assertEquals(new CopyOutcome.Refused(ProjectContent.of(projectFile, "../Shared").unavailableReason()
                .orElseThrow()), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void notEnoughFreeSpaceIsRefused() {
        List<Path> asked = new ArrayList<>();

        CopyOutcome outcome = copier(DirectoryMover.SYSTEM, folder -> {
            asked.add(folder);
            return 4096;
        }).copy(request(COPY), JobMonitor.NONE);

        CopyOutcome.Refused refused = assertInstanceOf(CopyOutcome.Refused.class, outcome);
        assertTrue(refused.reason().startsWith("Not enough free space for the copy in " + parent + ": it needs about "),
                refused.reason());
        assertTrue(refused.reason().endsWith(", but only 4.0 KB is free."), refused.reason());
        assertEquals(List.of(parent), asked);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aFreeSpaceCheckThatCannotRunLetsTheCopyGoAheadWithANote() {
        CopyOutcome.Done done = done(copier(DirectoryMover.SYSTEM, folder -> {
            throw new IOException("no answer");
        }).copy(request(COPY), JobMonitor.NONE));

        assertEquals(List.of("Could not check the free space in " + parent + " (no answer); the copy went ahead."),
                done.notes());
    }

    @Test
    void aProjectFileTheLauncherCannotReadIsRefusedBeforeAnythingIsWritten() throws IOException {
        Files.writeString(projectFile, "{\"projectName\": \"Game\", // a comment\n}");

        CopyOutcome outcome = copy(COPY);

        CopyOutcome.Refused refused = assertInstanceOf(CopyOutcome.Refused.class, outcome);
        assertTrue(refused.reason().startsWith("The project file can't be copied: Not valid JSON"), refused.reason());
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // A cancel or a failure deletes the staging folder and nothing else
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aCancelWhileCopyingRemovesTheStagingFolderOnly() {
        Map<String, String> before = TestSupport.treeSnapshot(game);

        CopyOutcome outcome = copier().copy(request(COPY), cancellingAt(JobMonitor.Phase.COPYING));

        assertEquals(new CopyOutcome.Cancelled(), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
        assertEquals(before, TestSupport.treeSnapshot(game));
    }

    @Test
    void aCancelWhileScanningWritesNothing() {
        CopyOutcome outcome = copier().copy(request(COPY), cancellingAt(JobMonitor.Phase.SCANNING));

        assertEquals(new CopyOutcome.Cancelled(), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void anInjectedFailureRemovesTheStagingFolderOnly() {
        Map<String, String> before = TestSupport.treeSnapshot(game);

        CopyOutcome outcome = copier().copy(request(COPY), new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) throws IOException {
                if (phase == Phase.COPYING && done > FolderZipWriter.CHUNK_BYTES) {
                    throw new IOException("injected failure");
                }
            }
        });

        assertEquals(new CopyOutcome.Failed("Could not copy the project: injected failure"), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
        assertEquals(before, TestSupport.treeSnapshot(game));
    }

    @Test
    void aReadOnlySourceFileDoesNotStopAFailedCopyFromCleaningUp() {
        // Main.mt is copied before big.bin, whose last chunk fails: the staging folder then holds the copy of the
        // read-only file, and must still go.
        Path readOnly = game.resolve("assets/scripts/game/Main.mt");
        assumeTrue(readOnly.toFile().setReadOnly(), "this file system cannot make a file read-only");
        try {
            CopyOutcome outcome = copier().copy(request(COPY), new JobMonitor() {
                @Override
                public void progress(Phase phase, long done, long total) throws IOException {
                    if (phase == Phase.COPYING && done == total) {
                        throw new IOException("injected failure");
                    }
                }
            });

            assertEquals(new CopyOutcome.Failed("Could not copy the project: injected failure"), outcome);
            assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
        } finally {
            readOnly.toFile().setWritable(true);
        }
    }

    @Test
    void aFailedRenameRemovesTheStagingFolderAndLeavesTheOriginal() {
        Map<String, String> before = TestSupport.treeSnapshot(game);
        AtomicInteger calls = new AtomicInteger();

        CopyOutcome outcome = copier((source, target) -> {
            calls.incrementAndGet();
            assertEquals(staging(COPY), source);
            assertEquals(parent.resolve(COPY), target);
            assertTrue(Files.isRegularFile(source.resolve("Game.vfproj")), "the copy was complete");
            throw new IOException("the disk is on fire");
        }, folder -> Long.MAX_VALUE).copy(request(COPY), JobMonitor.NONE);

        CopyOutcome.Failed failed = assertInstanceOf(CopyOutcome.Failed.class, outcome);
        assertTrue(failed.message().startsWith("Could not move the copy into place: the disk is on fire"),
                failed.message());
        assertEquals(1, calls.get(), "a failure that is not transient is not retried");
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
        assertEquals(before, TestSupport.treeSnapshot(game));
    }

    @Test
    void aRenameBlockedForAMomentIsRetried() {
        AtomicInteger calls = new AtomicInteger();

        done(copier((source, target) -> {
            if (calls.incrementAndGet() < 3) {
                throw new AccessDeniedException(source.toString(), null, "used by another process");
            }
            DirectoryMover.SYSTEM.move(source, target);
        }, folder -> Long.MAX_VALUE).copy(request(COPY), JobMonitor.NONE));

        assertEquals(3, calls.get());
        assertEquals(Set.of("Game", COPY), TestSupport.fileNames(parent));
    }

    @Test
    void aTargetThatAppearsJustBeforeTheRenameIsNeverReplaced() throws IOException {
        CopyOutcome outcome = copier((source, target) -> {
            Files.createDirectory(target);
            Files.writeString(target.resolve("intruder.txt"), "theirs");
            DirectoryMover.SYSTEM.move(source, target);
        }, folder -> Long.MAX_VALUE).copy(request(COPY), JobMonitor.NONE);

        assertEquals(new CopyOutcome.Failed("Something else created \"" + COPY + "\" in " + parent
                + " while the copy was being made. Nothing was changed."), outcome);
        assertEquals(Set.of("intruder.txt"), TestSupport.fileNames(parent.resolve(COPY)));
        assertEquals(Set.of("Game", COPY), TestSupport.fileNames(parent), "no staging folder is left");
    }

    /** A JSON string literal holding {@code value}. */
    private static String quoted(String value) {
        return "\"" + value.replace("\\", "\\\\") + "\"";
    }
}
