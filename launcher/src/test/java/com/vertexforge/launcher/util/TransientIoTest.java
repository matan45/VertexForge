package com.vertexforge.launcher.util;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.nio.file.AccessDeniedException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.DirectoryNotEmptyException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.FileSystemException;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class TransientIoTest {

    @AfterEach
    void clearInterrupt() {
        // A test that leaves the flag set would break the next one on this thread.
        Thread.interrupted();
    }

    @Test
    void onlyAccessDeniedAndPlainFileSystemErrorsAreTransient() {
        assertTrue(TransientIo.isTransient(new AccessDeniedException("f")));
        assertTrue(TransientIo.isTransient(new FileSystemException("f")));
        assertFalse(TransientIo.isTransient(new FileAlreadyExistsException("f")));
        assertFalse(TransientIo.isTransient(new DirectoryNotEmptyException("f")));
        assertFalse(TransientIo.isTransient(new AtomicMoveNotSupportedException("a", "b", "r")));
        assertFalse(TransientIo.isTransient(new IOException("disk full")));
    }

    @Test
    void aTransientFailureIsRetriedUntilItSucceeds() throws IOException {
        AtomicInteger calls = new AtomicInteger();

        TransientIo.retry(() -> {
            if (calls.incrementAndGet() < 3) {
                throw new AccessDeniedException("locked by a scanner");
            }
        }, 5, Duration.ZERO, Duration.ZERO, "testing");

        assertEquals(3, calls.get());
    }

    @Test
    void theLastFailureIsThrownOnceTheAttemptsRunOut() {
        AtomicInteger calls = new AtomicInteger();
        FileSystemException last = new FileSystemException("still locked");

        IOException thrown = assertThrows(IOException.class, () -> TransientIo.retry(() -> {
            calls.incrementAndGet();
            throw last;
        }, 4, Duration.ZERO, Duration.ZERO, "testing"));

        assertSame(last, thrown);
        assertEquals(4, calls.get());
    }

    @Test
    void aFailureThatIsNotTransientIsThrownAtOnce() {
        AtomicInteger calls = new AtomicInteger();

        assertThrows(FileAlreadyExistsException.class, () -> TransientIo.retry(() -> {
            calls.incrementAndGet();
            throw new FileAlreadyExistsException("target");
        }, 8, Duration.ZERO, Duration.ZERO, "testing"));

        assertEquals(1, calls.get());
    }

    @Test
    void anInterruptedWaitStopsAndKeepsTheInterruptFlag() {
        AtomicInteger calls = new AtomicInteger();
        Thread.currentThread().interrupt();

        InterruptedIOException thrown = assertThrows(InterruptedIOException.class, () -> TransientIo.retry(() -> {
            calls.incrementAndGet();
            throw new AccessDeniedException("locked");
        }, 5, Duration.ofSeconds(5), Duration.ofSeconds(5), "moving x"));

        assertEquals(1, calls.get());
        assertEquals("interrupted while moving x", thrown.getMessage());
        assertTrue(Thread.currentThread().isInterrupted());
    }

    @Test
    void atLeastOneAttemptIsRequired() {
        assertThrows(IllegalArgumentException.class,
                () -> TransientIo.retry(() -> { }, 0, Duration.ZERO, Duration.ZERO, "testing"));
    }
}
