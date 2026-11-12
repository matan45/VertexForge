package com.vertexforge.launcher.store;

import com.vertexforge.launcher.store.StoreWriteException.Reason;

import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;

/**
 * The two locks a store write holds (VK-1678).
 * <ul>
 *   <li>An in-process {@link ReentrantLock} per lock file. Java file locks belong to the whole JVM: a second
 *       {@code tryLock} on the same file from this process throws {@link OverlappingFileLockException} instead of
 *       waiting, so threads must queue here first.</li>
 *   <li>A {@link FileLock} on a separate lock file, which other launcher processes respect. The data file itself is
 *       never locked: a lock on it would make the atomic replace fail on Windows. The OS releases the lock when the
 *       process dies, so a crash never leaves a stale lock behind. The lock file is never deleted.</li>
 * </ul>
 */
final class FileLocks {

    static final Duration POLL_INTERVAL = Duration.ofMillis(25);

    private static final ConcurrentHashMap<Path, ReentrantLock> LOCAL_LOCKS = new ConcurrentHashMap<>();

    private FileLocks() {
        // Utility class - prevent instantiation
    }

    /**
     * Takes both locks, waiting at most {@code timeout} in total.
     *
     * @throws IllegalStateException if this thread already holds the lock (a store update started from inside
     *                               another update of the same file)
     * @throws StoreWriteException   {@code LOCK_TIMEOUT} when the wait runs out or is interrupted, {@code IO} when the
     *                               lock file cannot be opened
     */
    static Held acquire(Path lockFile, Duration timeout) throws StoreWriteException {
        ReentrantLock local = LOCAL_LOCKS.computeIfAbsent(lockFile.toAbsolutePath().normalize(),
                key -> new ReentrantLock());
        if (local.isHeldByCurrentThread()) {
            throw new IllegalStateException("A store update of " + lockFile + " is already running on this thread");
        }

        long deadline = System.nanoTime() + timeout.toNanos();
        try {
            if (!local.tryLock(timeout.toNanos(), TimeUnit.NANOSECONDS)) {
                throw timedOut(lockFile, timeout, null);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw timedOut(lockFile, timeout, e);
        }

        FileChannel channel = null;
        boolean acquired = false;
        try {
            channel = FileChannel.open(lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE);
            while (true) {
                FileLock lock;
                try {
                    lock = channel.tryLock();
                } catch (OverlappingFileLockException e) {
                    // This JVM holds the lock through another spelling of the path (an 8.3 short name, say), which
                    // got its own ReentrantLock: treat it as busy and keep waiting.
                    lock = null;
                }
                if (lock != null) {
                    acquired = true;
                    return new Held(local, channel, lock);
                }
                if (System.nanoTime() - deadline >= 0) {
                    throw timedOut(lockFile, timeout, null);
                }
                Thread.sleep(POLL_INTERVAL.toMillis());
            }
        } catch (IOException e) {
            throw new StoreWriteException(Reason.IO,
                    "Could not open the lock file " + lockFile + ": " + describe(e), e);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw timedOut(lockFile, timeout, e);
        } finally {
            if (!acquired) {
                closeQuietly(channel);
                local.unlock();
            }
        }
    }

    private static StoreWriteException timedOut(Path lockFile, Duration timeout, Throwable cause) {
        String why = cause instanceof InterruptedException
                ? "the wait was interrupted"
                : "another launcher held it for more than " + timeout.toMillis() + " ms";
        return new StoreWriteException(Reason.LOCK_TIMEOUT, "Could not lock " + lockFile + ": " + why, cause);
    }

    static String describe(IOException e) {
        return e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
    }

    private static void closeQuietly(FileChannel channel) {
        if (channel == null) {
            return;
        }
        try {
            channel.close();
        } catch (IOException ignored) {
            // Closing the channel releases its lock; nothing more can be done.
        }
    }

    /** Both locks; {@link #close()} releases the file lock, closes its channel and then the in-process lock. */
    static final class Held implements AutoCloseable {
        private final ReentrantLock local;
        private final FileChannel channel;
        private final FileLock lock;

        private Held(ReentrantLock local, FileChannel channel, FileLock lock) {
            this.local = local;
            this.channel = channel;
            this.lock = lock;
        }

        @Override
        public void close() {
            try {
                lock.release();
            } catch (IOException ignored) {
                // Closing the channel below releases the lock anyway.
            } finally {
                closeQuietly(channel);
                local.unlock();
            }
        }
    }
}
