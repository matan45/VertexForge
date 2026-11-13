package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.TickingClock;
import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import com.vertexforge.launcher.service.EditorLauncherTest.RecordingStarter;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CreationJobTest {

    @TempDir
    Path tempDir;

    private Path projectsDir;
    private ProjectHub hub;
    private final QueueExecutor creation = new QueueExecutor();
    private final QueueExecutor store = new QueueExecutor();

    /** Runs nothing until told to, so the test sees which thread each step was handed to. */
    static final class QueueExecutor implements Executor {
        final List<Runnable> queued = new ArrayList<>();

        @Override
        public void execute(Runnable command) {
            queued.add(command);
        }

        void runAll() {
            while (!queued.isEmpty()) {
                queued.removeFirst().run();
            }
        }
    }

    @BeforeEach
    void setUp() throws IOException {
        Path root = TestSupport.real(tempDir);
        projectsDir = Files.createDirectories(root.resolve("projects"));
        hub = ProjectHub.create(TestSupport.environment(root.resolve("config"), root,
                new TickingClock(TestSupport.T0), Map.of(), new RecordingStarter(FakeProcess.running(), null)),
                TestSupport.log(root.resolve("logs")).log);
    }

    @AfterEach
    void tearDown() {
        hub.close();
    }

    @Test
    void theFilesAreWrittenOnTheCreationThreadAndTheListAddOnTheStoreThread() {
        List<int[]> progress = new ArrayList<>();
        CreationJob job = CreationJob.start(hub, new CreateRequest("Game", projectsDir), creation, store,
                (done, total) -> progress.add(new int[]{done, total}));

        assertEquals(1, creation.queued.size());
        assertTrue(store.queued.isEmpty());
        assertFalse(job.outcome().isDone());

        creation.runAll();
        assertTrue(Files.isRegularFile(projectsDir.resolve("Game/Game.vfproj")), "files written on vf-create");
        assertTrue(hub.listProjects().isEmpty(), "the list is not touched on vf-create");
        assertEquals(1, store.queued.size());
        assertFalse(job.outcome().isDone());

        store.runAll();
        CreateOutcome outcome = job.outcome().join();
        assertTrue(outcome.created() && outcome.listed());
        assertEquals(projectsDir.resolve("Game/Game.vfproj"), outcome.projectPath());
        assertEquals(1, hub.listProjects().entries().size());
        assertFalse(progress.isEmpty());
        int[] last = progress.getLast();
        assertEquals(last[1], last[0], "the last report is the last file");
    }

    @Test
    void aCancelBeforeTheCreationRunsWritesNothing() {
        CreationJob job = CreationJob.start(hub, new CreateRequest("Game", projectsDir), creation, store,
                CreationJob.ProgressListener.NONE);

        job.cancel();
        creation.runAll();

        CreateOutcome outcome = job.outcome().join();
        assertFalse(outcome.created());
        assertTrue(outcome.result().cancelled());
        assertTrue(job.cancelRequested());
        assertTrue(store.queued.isEmpty(), "nothing to list");
        assertEquals(Set.of(), TestSupport.fileNames(projectsDir));
    }

    @Test
    void aFailedCreationIsNeverListed() throws IOException {
        Files.createDirectory(projectsDir.resolve("Game"));
        Files.writeString(projectsDir.resolve("Game/keep.txt"), "mine");

        CreationJob job = CreationJob.start(hub, new CreateRequest("Game", projectsDir), creation, store,
                CreationJob.ProgressListener.NONE);
        creation.runAll();

        CreateOutcome outcome = job.outcome().join();
        assertFalse(outcome.created());
        assertFalse(outcome.result().cancelled());
        assertTrue(store.queued.isEmpty());
    }

    @Test
    void aProjectFinishedWhileTheLauncherClosesIsStillListed() {
        Executor closed = command -> {
            throw new RejectedExecutionException("shut down");
        };

        CreationJob job = CreationJob.start(hub, new CreateRequest("Game", projectsDir), creation, closed,
                CreationJob.ProgressListener.NONE);
        creation.runAll();

        CreateOutcome outcome = job.outcome().join();
        assertTrue(outcome.created());
        assertTrue(outcome.listed(), "listed on the creation thread once the store thread is gone");
        assertTrue(Files.isRegularFile(outcome.projectPath()));
        assertEquals(1, hub.listProjects().entries().size());
    }

    @Test
    void aClosingCreationThreadFailsAtOnce() {
        Executor closed = command -> {
            throw new RejectedExecutionException("shut down");
        };

        CreationJob job = CreationJob.start(hub, new CreateRequest("Game", projectsDir), closed, store,
                CreationJob.ProgressListener.NONE);

        CreateOutcome outcome = job.outcome().join();
        assertFalse(outcome.created());
        assertEquals("The launcher is closing.", outcome.result().errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(projectsDir));
    }

    @Test
    void aListenerThatThrowsFailsTheCreationCleanly() {
        CreationJob job = CreationJob.start(hub, new CreateRequest("Game", projectsDir), creation, store,
                (done, total) -> {
                    throw new IllegalStateException("listener broke");
                });

        creation.runAll();

        CreateOutcome outcome = job.outcome().join();
        assertFalse(outcome.created());
        assertTrue(outcome.result().errorMessage().contains("listener broke"), outcome.result().errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(projectsDir));
    }
}
