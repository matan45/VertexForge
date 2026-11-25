package com.vertexforge.launcher.service;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class FileJobTest {

    /** An outcome for the tests: the work's own text, or "failed: " plus the job's message. */
    private static String failed(String message) {
        return "failed: " + message;
    }

    /** Runs nothing until told to, so a test sees what happens before and after the work runs. */
    private static final class QueueExecutor implements Executor {
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

    @Test
    void theWorksOutcomeIsTheJobsOutcome() {
        QueueExecutor executor = new QueueExecutor();

        FileJob<String> job = FileJob.start(executor, monitor -> "done", FileJob.ProgressListener.NONE,
                FileJobTest::failed);

        assertFalse(job.outcome().isDone(), "the work runs on the executor, not in start");
        executor.runAll();
        assertEquals("done", job.outcome().join());
    }

    @Test
    void anExecutorThatRefusesTheWorkMeansTheLauncherIsClosing() {
        Executor closed = command -> {
            throw new RejectedExecutionException("shut down");
        };

        FileJob<String> job = FileJob.start(closed, monitor -> "never", FileJob.ProgressListener.NONE,
                FileJobTest::failed);

        assertTrue(job.outcome().isDone());
        assertEquals("failed: The launcher is closing.", job.outcome().join());
    }

    @Test
    void anExceptionInsideTheWorkBecomesAFailedOutcome() {
        QueueExecutor executor = new QueueExecutor();

        FileJob<String> job = FileJob.start(executor, monitor -> {
            throw new IllegalStateException("boom");
        }, FileJob.ProgressListener.NONE, FileJobTest::failed);
        executor.runAll();

        assertEquals("failed: The job stopped unexpectedly: java.lang.IllegalStateException: boom",
                job.outcome().join());
    }

    @Test
    void aWorkThatGivesNoOutcomeFails() {
        QueueExecutor executor = new QueueExecutor();

        FileJob<String> job = FileJob.start(executor, monitor -> null, FileJob.ProgressListener.NONE,
                FileJobTest::failed);
        executor.runAll();

        assertEquals("failed: The job stopped unexpectedly: it gave no outcome", job.outcome().join());
    }

    @Test
    void anErrorStillEndsTheJobAndThenGoesOn() {
        QueueExecutor executor = new QueueExecutor();
        StackOverflowError error = new StackOverflowError("deep");

        FileJob<String> job = FileJob.start(executor, monitor -> {
            throw error;
        }, FileJob.ProgressListener.NONE, FileJobTest::failed);

        assertEquals(error, assertThrows(StackOverflowError.class, executor::runAll));
        assertEquals("failed: The job stopped unexpectedly.", job.outcome().join());
    }

    @Test
    void aCancelReachesTheWorksMonitor() {
        QueueExecutor executor = new QueueExecutor();
        List<Boolean> seen = new ArrayList<>();

        FileJob<String> job = FileJob.start(executor, monitor -> {
            seen.add(monitor.cancelRequested());
            try {
                monitor.checkCancelled();
                return "not cancelled";
            } catch (JobCancelledException e) {
                return "cancelled: " + e.getMessage();
            }
        }, FileJob.ProgressListener.NONE, FileJobTest::failed);
        assertFalse(job.cancelRequested());
        job.cancel();
        executor.runAll();

        assertTrue(job.cancelRequested());
        assertEquals(List.of(true), seen);
        assertEquals("cancelled: The job was cancelled", job.outcome().join());
    }

    @Test
    void progressReachesTheListenerOnTheWorksThread() {
        QueueExecutor executor = new QueueExecutor();
        List<String> heard = new ArrayList<>();

        FileJob<String> job = FileJob.start(executor, monitor -> {
            try {
                monitor.progress(JobMonitor.Phase.SCANNING, 3, JobMonitor.UNKNOWN);
                monitor.progress(JobMonitor.Phase.WRITING, 10, 20);
            } catch (java.io.IOException e) {
                throw new IllegalStateException(e);
            }
            return "done";
        }, (phase, done, total) -> heard.add(phase + " " + done + "/" + total + " on "
                + Thread.currentThread().getName()), FileJobTest::failed);
        executor.runAll();

        String thread = Thread.currentThread().getName();
        assertEquals(List.of("SCANNING 3/-1 on " + thread, "WRITING 10/20 on " + thread), heard);
        assertEquals("done", job.outcome().join());
    }

    @Test
    void sizesReadAsWindowsShowsThem() {
        assertEquals("0 bytes", JobMonitor.formatBytes(0));
        assertEquals("1 byte", JobMonitor.formatBytes(1));
        assertEquals("1023 bytes", JobMonitor.formatBytes(1023));
        assertEquals("1.0 KB", JobMonitor.formatBytes(1024));
        assertEquals("1.5 KB", JobMonitor.formatBytes(1536));
        assertEquals("1.0 MB", JobMonitor.formatBytes(1024 * 1024 - 1), "never 1024.0 KB");
        assertEquals("1.2 GB", JobMonitor.formatBytes(1288490189L));
        assertEquals("8.0 EB", JobMonitor.formatBytes(Long.MAX_VALUE));
    }
}
