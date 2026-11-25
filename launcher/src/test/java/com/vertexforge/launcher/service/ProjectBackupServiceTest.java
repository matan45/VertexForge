package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileReader;
import com.vertexforge.launcher.service.ProjectBackupService.BackupOutcome;
import com.vertexforge.launcher.service.ProjectBackupService.BackupRequest;
import com.vertexforge.launcher.util.TestLinks;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.AccessDeniedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectBackupServiceTest {

    /** The zip's name for a project saved by 1.0.0, backed up at {@link TestSupport#T0}. */
    private static final String ZIP = "Game-backup-1.0.0-20260102-030405Z.zip";

    @TempDir
    Path tempDir;

    /** Where the project lives; the zip goes here too. Nothing else is in it. */
    private Path parent;
    private Path game;
    private Path projectFile;

    @BeforeEach
    void setUp() throws IOException {
        parent = Files.createDirectory(TestSupport.real(tempDir).resolve("projects"));
        game = ProjectTreeFixture.game(parent);
        projectFile = game.resolve("Game.vfproj");
    }

    private ProjectBackupService service() {
        return service(DirectoryMover.SYSTEM, folder -> Long.MAX_VALUE);
    }

    private ProjectBackupService service(DirectoryMover mover, SpaceProbe space) {
        return new ProjectBackupService(TestSupport.fixedClock(), space, mover, Duration.ZERO);
    }

    private BackupRequest request() {
        return new BackupRequest(projectFile, "assets", "1.0.0");
    }

    private BackupOutcome.Done done(BackupOutcome outcome) {
        return assertInstanceOf(BackupOutcome.Done.class, outcome, outcome::toString);
    }

    private static List<String> entryNames(Path zip) throws IOException {
        try (ZipFile file = new ZipFile(zip.toFile())) {
            return file.stream().map(ZipEntry::getName).toList();
        }
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
    // What a backup is
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aBackupIsAVerifiedZipBesideTheProjectNamedByTheSavedVersionAndTheTime() throws IOException {
        Map<String, String> before = TestSupport.treeSnapshot(game);

        BackupOutcome.Done done = done(service().backUp(request(), JobMonitor.NONE));

        assertEquals(parent.resolve(ZIP), done.zip());
        assertEquals(Set.of("Game", ZIP), TestSupport.fileNames(parent), "no .partial is left");
        assertEquals(Files.size(done.zip()), done.zipBytes());
        // .gitignore, Game.vfproj, Main.mt, Main.mt.vfmeta, big.bin
        assertEquals(5, done.files());
        assertEquals(Files.size(game.resolve("Game.vfproj")) + Files.size(game.resolve(".gitignore"))
                + Files.size(game.resolve("assets/scripts/game/Main.mt"))
                + Files.size(game.resolve("assets/scripts/game/Main.mt.vfmeta"))
                + ProjectTreeFixture.BIG_FILE_BYTES, done.contentBytes());
        assertEquals(List.of(), done.notes());
        assertEquals(before, TestSupport.treeSnapshot(game), "the project is only read");
    }

    /** A backup without the project file would not restore to a project: a linked .vfproj is never followed. */
    @Test
    void aProjectFileThatIsALinkIsRefusedAndNothingIsWritten() throws IOException {
        Path real = Files.move(projectFile, parent.resolve("Real.vfproj"));
        TestLinks.symlink(projectFile, real);

        BackupOutcome outcome = service().backUp(request(), JobMonitor.NONE);

        BackupOutcome.Refused refused = assertInstanceOf(BackupOutcome.Refused.class, outcome, outcome::toString);
        assertTrue(refused.reason().contains("is not a file in the project folder"), refused.reason());
        assertEquals(Set.of("Game", "Real.vfproj"), TestSupport.fileNames(parent), "no zip and no .partial");
    }

    @Test
    void theZipHoldsTheFolderWithoutTheEditorsStateAndTheCompiledScripts() throws IOException {
        BackupOutcome.Done done = done(service().backUp(request(), JobMonitor.NONE));

        List<String> names = entryNames(done.zip());
        assertEquals(List.of("Game/", "Game/assets/", "Game/assets/scenes/", "Game/assets/scripts/",
                "Game/assets/scripts/game/", "Game/assets/textures/", "Game/.gitignore", "Game/Game.vfproj",
                "Game/assets/scripts/game/Main.mt", "Game/assets/scripts/game/Main.mt.vfmeta",
                "Game/assets/textures/big.bin"), names);
    }

    /** VK-1690 AC1: the backup restores to a project the launcher reads, identical but for what it leaves out. */
    @Test
    void aRestoredBackupIsTheProjectMinusWhatTheEditorRebuilds() throws Exception {
        BackupOutcome.Done done = done(service().backUp(request(), JobMonitor.NONE));
        Path restore = Files.createDirectory(tempDir.resolve("restore"));

        try (InputStream in = Files.newInputStream(done.zip())) {
            ZipExtractor.extract(in, restore);
        }

        Path restored = restore.resolve("Game");
        assertEquals(Set.of("Game"), TestSupport.fileNames(restore));
        ProjectFile project = ProjectFileReader.read(restored.resolve("Game.vfproj"));
        assertEquals("Game", project.projectName());
        assertEquals(null, project.structureProblem(), "the editor would accept it");
        assertEquals(ProjectTreeFixture.contentHashes(game, ProjectContent.of(projectFile, "assets").excluded()),
                ProjectTreeFixture.contentHashes(restored, relative -> false));
        assertTrue(Files.isDirectory(restored.resolve("assets/scenes")), "the empty folder came back too");
    }

    @Test
    void reportsScanningThenWritingThenVerifying() {
        List<JobMonitor.Phase> phases = new ArrayList<>();
        List<long[]> reports = new ArrayList<>();

        done(service().backUp(request(), new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) {
                if (phases.isEmpty() || phases.getLast() != phase) {
                    phases.add(phase);
                }
                if (phase != Phase.SCANNING) {
                    reports.add(new long[]{done, total});
                }
            }
        }));

        assertEquals(List.of(JobMonitor.Phase.SCANNING, JobMonitor.Phase.WRITING, JobMonitor.Phase.VERIFYING), phases);
        for (long[] report : reports) {
            assertTrue(report[0] <= report[1]);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Its name
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anUnknownOrMalformedSavedVersionIsNamedUnknown() {
        BackupOutcome.Done unknown = done(service().backUp(new BackupRequest(projectFile, "assets", null),
                JobMonitor.NONE));
        BackupOutcome.Done product = done(service().backUp(new BackupRequest(projectFile, "assets",
                "VertexForge 1.0.0"), JobMonitor.NONE));

        assertEquals(parent.resolve("Game-backup-unknown-20260102-030405Z.zip"), unknown.zip());
        assertEquals(parent.resolve("Game-backup-unknown-20260102-030405Z-2.zip"), product.zip(),
                "not a bare version, and the first name is taken now");
    }

    @Test
    void aTakenNameGetsTheNextFreeSuffixAndIsLeftAsItWas() throws IOException {
        Files.writeString(parent.resolve(ZIP), "someone else's");
        Files.createDirectory(parent.resolve("Game-backup-1.0.0-20260102-030405Z-2.zip"));

        BackupOutcome.Done done = done(service().backUp(request(), JobMonitor.NONE));

        assertEquals(parent.resolve("Game-backup-1.0.0-20260102-030405Z-3.zip"), done.zip());
        assertEquals("someone else's", Files.readString(parent.resolve(ZIP)));
        assertTrue(Files.isDirectory(parent.resolve("Game-backup-1.0.0-20260102-030405Z-2.zip")));
        assertEquals(4, TestSupport.fileNames(parent).size(), "no .partial is left");
    }

    @Test
    void aStalePartialOfAnEarlierBackupIsLeftAlone() throws IOException {
        Path stale = Files.writeString(parent.resolve(ZIP + ".partial"), "stale");

        BackupOutcome.Done done = done(service().backUp(request(), JobMonitor.NONE));

        assertEquals(parent.resolve(ZIP), done.zip(), "only the .partial's name moved on");
        assertEquals("stale", Files.readString(stale));
        assertEquals(Set.of("Game", ZIP, ZIP + ".partial"), TestSupport.fileNames(parent));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Refusals: nothing is written
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aWorkingDirectoryOutsideTheFolderIsRefused() {
        BackupOutcome outcome = service().backUp(new BackupRequest(projectFile, "../Shared", "1.0.0"),
                JobMonitor.NONE);

        assertEquals(new BackupOutcome.Refused(ProjectContent.of(projectFile, "../Shared").unavailableReason()
                .orElseThrow()), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aProjectAtADriveRootIsRefused() {
        Path atRoot = tempDir.getRoot().resolve("vk1690-no-such-project.vfproj");

        BackupOutcome outcome = service().backUp(new BackupRequest(atRoot, ".", "1.0.0"), JobMonitor.NONE);

        assertEquals(new BackupOutcome.Refused("The project folder is a drive root, so there is no folder beside it "
                + "to put a backup or a copy in."), outcome);
    }

    @Test
    void notEnoughFreeSpaceIsRefused() {
        List<Path> asked = new ArrayList<>();

        BackupOutcome outcome = service(DirectoryMover.SYSTEM, folder -> {
            asked.add(folder);
            return 1000;
        }).backUp(request(), JobMonitor.NONE);

        BackupOutcome.Refused refused = assertInstanceOf(BackupOutcome.Refused.class, outcome);
        assertTrue(refused.reason().startsWith("Not enough free space for the backup in " + parent
                + ": it needs up to "), refused.reason());
        assertTrue(refused.reason().endsWith(", but only 1000 bytes is free."), refused.reason());
        assertEquals(List.of(parent), asked, "the drive the zip goes to");
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aFreeSpaceCheckThatCannotRunLetsTheBackupGoAheadWithANote() {
        BackupOutcome.Done done = done(service(DirectoryMover.SYSTEM, folder -> {
            throw new IOException("no answer");
        }).backUp(request(), JobMonitor.NONE));

        assertEquals(List.of("Could not check the free space in " + parent + " (no answer); the backup went ahead."),
                done.notes());
    }

    /** Never inside the project: a link that makes the folder beside the project part of it is caught. */
    @Test
    void aZipThatWouldLandInsideTheProjectThroughALinkIsRefused() throws IOException {
        Path real = Files.createDirectory(parent.resolve("Real"));
        Files.writeString(real.resolve("Game.vfproj"), ProjectTreeFixture.PROJECT_JSON);
        Path sub = Files.createDirectory(real.resolve("sub"));
        // parent/J -> Real/sub, and Real/sub/Game -> Real: so parent/J/Game is Real, and the folder beside it, J, is
        // Real/sub, which lies inside Real.
        Path inner = TestLinks.junction(sub.resolve("Game"), real);
        Path outer = null;
        try {
            outer = TestLinks.junction(parent.resolve("J"), sub);

            BackupOutcome outcome = service().backUp(new BackupRequest(outer.resolve("Game/Game.vfproj"), ".",
                    "1.0.0"), JobMonitor.NONE);

            BackupOutcome.Refused refused = assertInstanceOf(BackupOutcome.Refused.class, outcome);
            assertTrue(refused.reason().contains("is inside the project folder itself"), refused.reason());
            assertEquals(Set.of("Game.vfproj", "sub"), TestSupport.fileNames(real));
            assertEquals(Set.of("Game"), TestSupport.fileNames(sub));
        } finally {
            if (outer != null) {
                TestLinks.removeLink(outer);
            }
            TestLinks.removeLink(inner);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // A cancel or a failure leaves nothing behind
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aCancelWhileWritingLeavesNothing() {
        Map<String, String> before = TestSupport.treeSnapshot(game);

        BackupOutcome outcome = service().backUp(request(), cancellingAt(JobMonitor.Phase.WRITING));

        assertEquals(new BackupOutcome.Cancelled(), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
        assertEquals(before, TestSupport.treeSnapshot(game));
    }

    @Test
    void aCancelWhileVerifyingDeletesThePartial() {
        BackupOutcome outcome = service().backUp(request(), cancellingAt(JobMonitor.Phase.VERIFYING));

        assertEquals(new BackupOutcome.Cancelled(), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aCancelWhileScanningWritesNothing() {
        BackupOutcome outcome = service().backUp(request(), cancellingAt(JobMonitor.Phase.SCANNING));

        assertEquals(new BackupOutcome.Cancelled(), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aFailureWhileVerifyingDeletesThePartial() {
        BackupOutcome outcome = service().backUp(request(), new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) throws IOException {
                if (phase == Phase.VERIFYING) {
                    throw new IOException("injected failure");
                }
            }
        });

        assertEquals(new BackupOutcome.Failed("Could not write the backup: injected failure"), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aFailureWhileWritingLeavesNothing() {
        BackupOutcome outcome = service().backUp(request(), new JobMonitor() {
            @Override
            public void progress(Phase phase, long done, long total) throws IOException {
                if (phase == Phase.WRITING && done > FolderZipWriter.CHUNK_BYTES) {
                    throw new IOException("injected failure");
                }
            }
        });

        assertEquals(new BackupOutcome.Failed("Could not write the backup: injected failure"), outcome);
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aFailedRenameDeletesThePartialAndIsNotRetried() {
        AtomicInteger calls = new AtomicInteger();

        BackupOutcome outcome = service((source, target) -> {
            calls.incrementAndGet();
            assertEquals(parent.resolve(ZIP + ".partial"), source);
            assertEquals(parent.resolve(ZIP), target);
            throw new IOException("the disk is on fire");
        }, folder -> Long.MAX_VALUE).backUp(request(), JobMonitor.NONE);

        BackupOutcome.Failed failed = assertInstanceOf(BackupOutcome.Failed.class, outcome);
        assertTrue(failed.message().startsWith("Could not move the backup into place: the disk is on fire"),
                failed.message());
        assertEquals(1, calls.get(), "a failure that is not transient is not retried");
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aRenameBlockedForAMomentIsRetried() {
        AtomicInteger calls = new AtomicInteger();

        BackupOutcome.Done done = done(service((source, target) -> {
            if (calls.incrementAndGet() < 3) {
                throw new AccessDeniedException(source.toString(), null, "used by another process");
            }
            DirectoryMover.SYSTEM.move(source, target);
        }, folder -> Long.MAX_VALUE).backUp(request(), JobMonitor.NONE));

        assertEquals(3, calls.get());
        assertEquals(parent.resolve(ZIP), done.zip());
        assertEquals(Set.of("Game", ZIP), TestSupport.fileNames(parent));
    }

    @Test
    void aNameTakenJustBeforeTheRenameIsNeverReplaced() throws IOException {
        BackupOutcome.Done done = done(service((source, target) -> {
            if (target.getFileName().toString().equals(ZIP)) {
                Files.writeString(target, "theirs");
            }
            DirectoryMover.SYSTEM.move(source, target);
        }, folder -> Long.MAX_VALUE).backUp(request(), JobMonitor.NONE));

        assertEquals(parent.resolve("Game-backup-1.0.0-20260102-030405Z-2.zip"), done.zip());
        assertEquals("theirs", Files.readString(parent.resolve(ZIP)));
    }

    @Test
    void theProjectIsNeverChangedWhateverHappens() {
        Map<String, String> before = TestSupport.treeSnapshot(game);

        service().backUp(request(), cancellingAt(JobMonitor.Phase.VERIFYING));
        service((source, target) -> {
            throw new IOException("no");
        }, folder -> Long.MAX_VALUE).backUp(request(), JobMonitor.NONE);
        done(service().backUp(request(), JobMonitor.NONE));

        assertEquals(before, TestSupport.treeSnapshot(game));
        assertFalse(Files.exists(game.resolve(ZIP)));
    }
}
