package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.env.RecycleBin;
import com.vertexforge.launcher.env.RecycleCheck;
import com.vertexforge.launcher.env.RecycleResult;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.LockHolderMain;
import com.vertexforge.launcher.service.ProjectDeleter.CheckOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.DeleteOutcome;
import com.vertexforge.launcher.service.ProjectDeleter.DeleteRequest;
import com.vertexforge.launcher.util.TestLinks;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.BooleanSupplier;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** "Move to Recycle Bin…" (VK-1695) against a fake Recycle Bin, which "recycles" by moving into a folder of its own. */
class ProjectDeleterTest {

    @TempDir
    Path tempDir;

    private Path parent;
    private Path game;
    private Path projectFile;
    private Path bin;
    private final FakeBin fakeBin = new FakeBin();
    private DriveTypes drives = root -> DriveTypes.Kind.LOCAL;
    private final List<Duration> sleeps = new ArrayList<>();

    /** A Recycle Bin that answers what the test says, and moves a folder into {@link #bin} for a real recycle. */
    private final class FakeBin implements RecycleBin {
        RecycleCheck check = new RecycleCheck.Recyclable();
        /** What recycle answers, one per call; when empty, it really moves the folder and answers Recycled. */
        final Deque<RecycleResult> results = new ArrayDeque<>();
        final List<Path> checked = new ArrayList<>();
        final List<Path> recycled = new ArrayList<>();
        /** Whether a scripted result moves the folder anyway. */
        boolean moveAnyway;

        @Override
        public boolean available() {
            return true;
        }

        @Override
        public RecycleCheck check(Path folder, BooleanSupplier cancelRequested) {
            checked.add(folder);
            return check;
        }

        @Override
        public RecycleResult recycle(Path folder) {
            recycled.add(folder);
            RecycleResult scripted = results.pollFirst();
            if (scripted == null || moveAnyway) {
                try {
                    Files.move(folder, bin.resolve(folder.getFileName()));
                } catch (IOException e) {
                    return new RecycleResult.Failed(e.toString());
                }
            }
            return scripted == null ? new RecycleResult.Recycled() : scripted;
        }
    }

    @BeforeEach
    void setUp() throws IOException {
        Path root = TestSupport.real(tempDir);
        parent = Files.createDirectory(root.resolve("projects"));
        game = ProjectTreeFixture.game(parent);
        projectFile = game.resolve("Game.vfproj");
        bin = Files.createDirectory(root.resolve("bin"));
    }

    private ProjectDeleter deleter() {
        return new ProjectDeleter(fakeBin, drives, new EditorLockProbe(0, Duration.ZERO, duration -> {
        }), sleeps::add, Duration.ofMillis(300));
    }

    private DeleteRequest request(DeleteGuards.DeleteContext context) {
        return new DeleteRequest(projectFile, "assets", "Game", context);
    }

    private DeleteRequest request() {
        return request(DeleteGuards.DeleteContext.none());
    }

    private CheckOutcome check() {
        return deleter().check(request(), JobMonitor.NONE);
    }

    private static String refusal(CheckOutcome outcome) {
        return assertInstanceOf(CheckOutcome.Refused.class, outcome, outcome::toString).reason();
    }

    private static String refusal(DeleteOutcome outcome) {
        return assertInstanceOf(DeleteOutcome.Refused.class, outcome, outcome::toString).reason();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The check, before the dialog
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theCheckReportsTheWholeFolderAndAsksWindows() throws IOException {
        Map<String, String> before = TestSupport.treeSnapshot(game);

        CheckOutcome outcome = check();

        ProjectDeleter.DeletePreflight preflight = assertInstanceOf(CheckOutcome.Ready.class, outcome,
                outcome::toString).preflight();
        assertEquals(game, preflight.folder());
        assertEquals(game.toRealPath(), preflight.realFolder());
        // Everything goes, the editor's state and the compiled scripts too.
        FolderTree tree = FolderTree.scan(game, relative -> false, JobMonitor.NONE);
        assertEquals(tree.totalBytes(), preflight.bytes());
        assertEquals(tree.files().size(), preflight.files());
        assertEquals(tree.folders().size(), preflight.folders());
        assertEquals(List.of(), preflight.notes());
        assertEquals(List.of(game.toRealPath()), fakeBin.checked);
        assertEquals(before, TestSupport.treeSnapshot(game), "the check changes nothing");
    }

    /** A folder holding other projects is more than this project (review M2: a stray .vfproj in a projects root). */
    @Test
    void aProjectFileNestedInTheFolderIsRefused() throws IOException {
        Files.writeString(game.resolve("assets/Other.vfproj"), "{\"projectName\": \"Other\"}");

        String reason = refusal(check());

        assertTrue(reason.contains("holds other project files (assets/Other.vfproj)"), reason);
        assertEquals(List.of(), fakeBin.checked, "Windows is not even asked");
    }

    @Test
    void aProjectNestedAfterTheCheckStopsTheMove() throws IOException {
        assertInstanceOf(CheckOutcome.Ready.class, check());
        Files.writeString(game.resolve("assets/Late.vfproj"), "{\"projectName\": \"Late\"}");

        assertTrue(refusal(deleter().delete(request(), "Game", JobMonitor.NONE)).contains("holds other project files"));
        assertEquals(List.of(), fakeBin.recycled);
    }

    /** Review M1: a dry run that went ahead must never be reported as "nothing was changed". */
    @Test
    void aDryRunThatMovedTheFolderAnywayIsAFailureNotARefusal() {
        RecycleBin goingAhead = new RecycleBin() {
            @Override
            public boolean available() {
                return true;
            }

            @Override
            public RecycleCheck check(Path folder, BooleanSupplier cancelRequested) {
                try {
                    Files.move(folder, bin.resolve(folder.getFileName()));
                } catch (IOException e) {
                    throw new IllegalStateException(e);
                }
                return new RecycleCheck.Unknown("Windows finished the check without asking first");
            }

            @Override
            public RecycleResult recycle(Path folder) {
                throw new AssertionError("never reached");
            }
        };
        ProjectDeleter deleter = new ProjectDeleter(goingAhead, drives, new EditorLockProbe(0, Duration.ZERO,
                duration -> {
                }), sleeps::add, Duration.ZERO);

        CheckOutcome outcome = deleter.check(request(), JobMonitor.NONE);

        String message = assertInstanceOf(CheckOutcome.Failed.class, outcome, outcome::toString).message();
        assertTrue(message.contains("Recycle Bin"), message);
        assertFalse(message.contains("nothing was changed"), message);
    }

    /** Review L2: a linked project file would make its link's folder "the project". */
    @Test
    void aLinkedProjectFileIsRefused() throws IOException {
        Path links = Files.createDirectory(parent.resolve("Links"));
        Path link = TestLinks.symlink(links.resolve("Game.vfproj"), projectFile);
        try {
            String reason = refusal(deleter().check(new DeleteRequest(link, "assets", "Game",
                    DeleteGuards.DeleteContext.none()), JobMonitor.NONE));

            assertTrue(reason.contains("is a link or a folder"), reason);
        } finally {
            TestLinks.removeLink(link);
        }
        assertEquals(List.of(), fakeBin.checked);
    }

    /** Review L3: the checks used the working directory as first read. */
    @Test
    void aWorkingDirectoryChangedSinceTheReadStopsTheMove() throws IOException {
        Files.writeString(projectFile, ProjectTreeFixture.PROJECT_JSON.replace("\"assets\"", "\"elsewhere\""));

        String reason = refusal(deleter().delete(request(), "Game", JobMonitor.NONE));

        assertTrue(reason.contains("working directory changed"), reason);
        assertEquals(List.of(), fakeBin.recycled);
    }

    @Test
    void windowsThatWouldDeleteForGoodRefusesBeforeTheNameIsTyped() {
        fakeBin.check = new RecycleCheck.NotRecyclable("the drive has no Recycle Bin");

        String reason = refusal(check());

        assertTrue(reason.contains("the drive has no Recycle Bin"), reason);
        assertTrue(reason.endsWith(ProjectDeleter.NOTHING_CHANGED), reason);
        assertTrue(Files.exists(projectFile));
    }

    @Test
    void windowsThatCannotSayIsARefusalToo() {
        fakeBin.check = new RecycleCheck.Unknown("COM 0x80004005");

        assertTrue(refusal(check()).contains("COM 0x80004005"));
    }

    @Test
    void aCancelWhileWindowsLooksIsACancel() {
        fakeBin.check = new RecycleCheck.Cancelled();

        assertInstanceOf(CheckOutcome.Cancelled.class, check());
    }

    @Test
    void aFolderWithTwoProjectFilesIsRefused() throws IOException {
        Files.writeString(game.resolve("Other.vfproj"), "{\"projectName\": \"Other\"}");

        String reason = refusal(check());

        assertTrue(reason.contains("2 project files (Game.vfproj, Other.vfproj)"), reason);
        assertEquals(List.of(), fakeBin.checked, "Windows is not even asked");
    }

    @Test
    void aProjectNamedOtherwiseNowIsRefused() {
        String reason = refusal(deleter().check(new DeleteRequest(projectFile, "assets", "Renamed",
                DeleteGuards.DeleteContext.none()), JobMonitor.NONE));

        assertTrue(reason.contains("now names the project \"Game\""), reason);
    }

    @Test
    void aProjectWhoseContentLiesOutsideItsFolderIsRefused() {
        String reason = refusal(deleter().check(new DeleteRequest(projectFile, "../elsewhere", "Game",
                DeleteGuards.DeleteContext.none()), JobMonitor.NONE));

        assertTrue(reason.contains("lies outside the project folder"), reason);
    }

    @Test
    void aFolderThatIsAJunctionIsRefused() throws IOException {
        Path link = TestLinks.junction(parent.resolve("Linked"), game);
        try {
            String reason = refusal(deleter().check(new DeleteRequest(link.resolve("Game.vfproj"), "assets", "Game",
                    DeleteGuards.DeleteContext.none()), JobMonitor.NONE));

            assertTrue(reason.contains("is a link to another folder"), reason);
        } finally {
            TestLinks.removeLink(link);
        }
        assertTrue(Files.exists(projectFile));
    }

    @Test
    void aDriveWithoutARecycleBinIsRefused() {
        for (DriveTypes.Kind kind : List.of(DriveTypes.Kind.REMOTE, DriveTypes.Kind.REMOVABLE,
                DriveTypes.Kind.OPTICAL, DriveTypes.Kind.RAM_DISK, DriveTypes.Kind.UNKNOWN)) {
            drives = root -> kind;

            String reason = refusal(check());

            assertTrue(reason.contains("Recycle Bin") || reason.contains("kind of drive"), kind + ": " + reason);
        }
        assertEquals(List.of(), fakeBin.checked);
    }

    @Test
    void aProtectedFolderTheProjectFolderHoldsIsRefused() {
        DeleteGuards.DeleteContext context = new DeleteGuards.DeleteContext(new ProtectedFolders(
                List.of(game.resolve("assets")), List.of()), List.of(), List.of());

        String reason = refusal(deleter().check(request(context), JobMonitor.NONE));

        assertTrue(reason.contains("a folder the launcher never deletes"), reason);
    }

    @Test
    void aProtectedFolderAboveTheProjectFolderDoesNotStopIt() {
        DeleteGuards.DeleteContext context = new DeleteGuards.DeleteContext(new ProtectedFolders(
                List.of(parent), List.of()), List.of(), List.of());

        assertInstanceOf(CheckOutcome.Ready.class, deleter().check(request(context), JobMonitor.NONE));
    }

    @Test
    void anotherListedProjectInOrAroundTheFolderIsRefused() {
        DeleteGuards.DeleteContext holding = new DeleteGuards.DeleteContext(ProtectedFolders.none(), List.of(),
                List.of(game.resolve("assets/sub/Other.vfproj")));
        DeleteGuards.DeleteContext around = new DeleteGuards.DeleteContext(ProtectedFolders.none(), List.of(),
                List.of(parent.resolve("Outer.vfproj")));

        assertTrue(refusal(deleter().check(request(holding), JobMonitor.NONE)).contains("also holds the listed project"));
        assertTrue(refusal(deleter().check(request(around), JobMonitor.NONE))
                .contains("lies in the folder of the listed project"));
    }

    @Test
    void anEnginesFolderInTheProjectFolderIsRefused() {
        DeleteGuards.DeleteContext context = new DeleteGuards.DeleteContext(ProtectedFolders.none(),
                List.of(game.resolve("engine/bin")), List.of());

        assertTrue(refusal(deleter().check(request(context), JobMonitor.NONE)).contains("holds an engine's folder"));
    }

    @Test
    void aFolderThisLauncherHoldsIsRefused() {
        EditorLockProbe locks = new EditorLockProbe(0, Duration.ZERO, duration -> {
        });
        EditorLockProbe.Hold hold = assertInstanceOf(EditorLockProbe.HoldResult.Holding.class, locks.hold(game))
                .hold();
        try {
            assertInstanceOf(CheckOutcome.Refused.class, check());
        } finally {
            hold.close();
        }
        assertEquals(List.of(), fakeBin.checked);
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    @Timeout(value = 120, unit = TimeUnit.SECONDS)
    void aProjectAnEditorHasOpenIsRefusedWithItsProcess() throws Exception {
        Process editor = startJava(LockHolderMain.class, EditorLockProbe.lockFile(game).toString());
        try {
            BufferedReader output = new BufferedReader(new InputStreamReader(editor.getInputStream(),
                    StandardCharsets.UTF_8));
            String line = CompletableFuture.supplyAsync(() -> {
                try {
                    return output.readLine();
                } catch (IOException e) {
                    return "<output closed: " + e + ">";
                }
            }).get(60, TimeUnit.SECONDS);
            assertTrue(line.startsWith(LockHolderMain.LOCKED), line);
            String pid = line.substring(LockHolderMain.LOCKED.length()).trim();

            String reason = refusal(check());
            assertTrue(reason.contains("open in the editor (process " + pid + ")"), reason);
            assertEquals(List.of(), fakeBin.checked);
            String atDelete = refusal(deleter().delete(request(), "Game", JobMonitor.NONE));
            assertTrue(atDelete.contains("open in the editor"), atDelete);
        } finally {
            editor.getOutputStream().close();
            assertTrue(editor.waitFor(30, TimeUnit.SECONDS), "the editor stand-in did not exit");
        }
        assertEquals(List.of(), fakeBin.recycled);
        assertTrue(Files.exists(projectFile));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The move
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theTypedNameMovesTheFolder() throws IOException {
        Path real = game.toRealPath();

        DeleteOutcome outcome = deleter().delete(request(), "Game", JobMonitor.NONE);

        assertEquals(new DeleteOutcome.Recycled(game), outcome);
        assertEquals(List.of(real), fakeBin.recycled);
        assertFalse(Files.exists(game));
        assertTrue(Files.exists(bin.resolve("Game/Game.vfproj")));
    }

    @Test
    void aNameThatIsNotExactlyTheProjectsMovesNothing() {
        for (String typed : new String[] {"game", "Game ", " Game", "", null}) {
            String reason = refusal(deleter().delete(request(), typed, JobMonitor.NONE));

            assertTrue(reason.contains("does not match"), typed + ": " + reason);
        }
        assertEquals(List.of(), fakeBin.recycled);
        assertTrue(Files.exists(projectFile));
    }

    @Test
    void theMoveChecksEverythingAgain() throws IOException {
        Files.writeString(game.resolve("Second.vfproj"), "{\"projectName\": \"Second\"}");

        assertTrue(refusal(deleter().delete(request(), "Game", JobMonitor.NONE)).contains("2 project files"));
        assertEquals(List.of(), fakeBin.recycled);
    }

    @Test
    void aFileInUseIsTriedAgainThenMoved() {
        fakeBin.results.add(new RecycleResult.Refused("a file in it is open in another program", true));
        fakeBin.results.add(new RecycleResult.Refused("a file in it is open in another program", true));

        DeleteOutcome outcome = deleter().delete(request(), "Game", JobMonitor.NONE);

        assertEquals(new DeleteOutcome.Recycled(game), outcome);
        assertEquals(3, fakeBin.recycled.size());
        assertEquals(List.of(Duration.ofMillis(300), Duration.ofMillis(300)), sleeps);
    }

    @Test
    void aFileThatStaysInUseLeavesTheFolderWhereItIs() {
        for (int i = 0; i < ProjectDeleter.RECYCLE_ATTEMPTS; i++) {
            fakeBin.results.add(new RecycleResult.Refused("a file in it is open in another program", true));
        }

        String reason = refusal(deleter().delete(request(), "Game", JobMonitor.NONE));

        assertTrue(reason.contains("open in another program"), reason);
        assertEquals(ProjectDeleter.RECYCLE_ATTEMPTS, fakeBin.recycled.size());
        assertTrue(Files.exists(projectFile));
    }

    @Test
    void aRefusalThatCannotPassIsNotRetried() {
        fakeBin.results.add(new RecycleResult.Refused("it needs administrator rights", false));

        assertTrue(refusal(deleter().delete(request(), "Game", JobMonitor.NONE)).contains("administrator"));
        assertEquals(1, fakeBin.recycled.size());
        assertEquals(List.of(), sleeps);
    }

    @Test
    void windowsRefusingAtTheMoveIsANotRecyclableRefusal() {
        fakeBin.results.add(new RecycleResult.NotRecyclable("the folder is too large for the Recycle Bin"));

        String reason = refusal(deleter().delete(request(), "Game", JobMonitor.NONE));

        assertTrue(reason.contains("too large"), reason);
        assertTrue(reason.endsWith(ProjectDeleter.NOTHING_CHANGED), reason);
        assertTrue(Files.exists(projectFile));
    }

    @Test
    void aSuccessThatLeftTheFolderIsAFailure() {
        fakeBin.results.add(new RecycleResult.Recycled());

        DeleteOutcome outcome = deleter().delete(request(), "Game", JobMonitor.NONE);

        assertTrue(assertInstanceOf(DeleteOutcome.Failed.class, outcome).message().contains("still there"));
    }

    @Test
    void aFolderGoneDespiteAnErrorSaysWhereToLook() {
        fakeBin.results.add(new RecycleResult.Failed("0x80004005"));
        fakeBin.moveAnyway = true;

        DeleteOutcome outcome = deleter().delete(request(), "Game", JobMonitor.NONE);

        assertTrue(assertInstanceOf(DeleteOutcome.Failed.class, outcome).message().contains("Recycle Bin"));
        assertFalse(Files.exists(game));
    }

    @Test
    void aPathTooLongForTheRecycleBinIsRefused() throws IOException {
        Path deep = parent;
        while (deep.toString().length() <= ProjectNameRules.MAX_PATH_CHARS) {
            deep = deep.resolve("d".repeat(40));
        }
        Path long1 = Files.createDirectories(deep);
        Path file = Files.writeString(long1.resolve("Deep.vfproj"), ProjectTreeFixture.PROJECT_JSON
                .replace("\"Game\"", "\"Deep\""));

        String reason = refusal(deleter().check(new DeleteRequest(file, "assets", "Deep",
                DeleteGuards.DeleteContext.none()), JobMonitor.NONE));

        assertTrue(reason.contains("longer than Windows'"), reason);
    }

    /** A child JVM with the test classes and the launcher's own classes, running {@code main}. */
    private static Process startJava(Class<?> main, String... args) throws Exception {
        String classPath = Path.of(main.getProtectionDomain().getCodeSource().getLocation().toURI())
                + java.io.File.pathSeparator
                + Path.of(EditorLockProbe.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        Path javaExe = Path.of(System.getProperty("java.home"), "bin", TestSupport.isWindows() ? "java.exe" : "java");
        List<String> command = new ArrayList<>(List.of(javaExe.toString(),
                "-Duser.home=" + System.getProperty("user.home"), "-cp", classPath, main.getName()));
        command.addAll(List.of(args));
        return new ProcessBuilder(command).redirectErrorStream(true).start();
    }
}
