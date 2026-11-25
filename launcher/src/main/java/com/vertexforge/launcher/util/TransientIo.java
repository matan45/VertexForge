package com.vertexforge.launcher.util;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileSystemException;
import java.time.Duration;
import java.util.Objects;

/**
 * Retries a file-system step that another program can briefly block (VK-1678, VK-1679): an antivirus scanner or the
 * search indexer opens a file or folder that was just written, and a rename or delete fails until it lets go.
 */
public final class TransientIo {

    /** How many times the recent-projects store tries its final move. */
    public static final int STORE_ATTEMPTS = 5;
    /** The store's fixed delay between two tries. */
    public static final Duration STORE_DELAY = Duration.ofMillis(200);

    private TransientIo() {
        // Utility class - prevent instantiation
    }

    /** One file-system step. */
    @FunctionalInterface
    public interface IoAction {
        void run() throws IOException;
    }

    /**
     * A failure that a retry can fix: access denied, or a plain {@link FileSystemException}, which is how a Windows
     * sharing violation (another program, typically an antivirus scanner, has the file open) arrives. Subclasses such
     * as {@code AtomicMoveNotSupportedException}, {@code FileAlreadyExistsException} or
     * {@code DirectoryNotEmptyException} are never retried. The reason text is localized, so it is not read.
     */
    public static boolean isTransient(IOException e) {
        return e instanceof AccessDeniedException || e.getClass() == FileSystemException.class;
    }

    /**
     * Runs {@code action} up to {@code attempts} times while it fails with a {@linkplain #isTransient transient}
     * error. The wait starts at {@code firstDelay} and doubles after every try, up to {@code maxDelay}. The last
     * failure, or the first one that is not transient, is thrown as it is.
     *
     * @param what what the action does, for the message of an interrupted wait ("saving x")
     * @throws InterruptedIOException when the thread is interrupted while it waits; the interrupt flag is set again
     */
    public static void retry(IoAction action, int attempts, Duration firstDelay, Duration maxDelay, String what)
            throws IOException {
        Objects.requireNonNull(action, "action");
        Objects.requireNonNull(firstDelay, "firstDelay");
        Objects.requireNonNull(maxDelay, "maxDelay");
        if (attempts < 1) {
            throw new IllegalArgumentException("attempts must be at least 1");
        }

        long delayMillis = firstDelay.toMillis();
        for (int attempt = 1; ; attempt++) {
            try {
                action.run();
                return;
            } catch (IOException e) {
                if (attempt >= attempts || !isTransient(e)) {
                    throw e;
                }
            }
            try {
                Thread.sleep(delayMillis);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                InterruptedIOException interrupted = new InterruptedIOException("interrupted while " + what);
                interrupted.initCause(e);
                throw interrupted;
            }
            delayMillis = Math.min(delayMillis * 2, maxDelay.toMillis());
        }
    }
}
