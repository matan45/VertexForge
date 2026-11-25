package com.vertexforge.launcher.service;

import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.TransientIo;

import java.io.IOException;
import java.nio.file.DirectoryNotEmptyException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Objects;

/**
 * Puts a {@link FolderTree} into a zip that is complete and verified the moment it has its name (VK-1690, shared with
 * VK-1691's script-library backup): it is written as {@code <name>.partial}, read back in full, and only then renamed
 * to {@code <stem>.zip}, in one step that never replaces anything; a name that is taken gets {@code -2}, {@code -3}, …
 * up to {@code -}{@value #MAX_NAME_ATTEMPTS}. A failure or a cancel deletes the {@code .partial} this call created, and
 * nothing else.
 */
final class VerifiedZipCommit {

    public static final String ZIP_EXTENSION = ".zip";
    /** Added to the zip's name while it is being written. */
    public static final String PARTIAL_EXTENSION = ".partial";
    /** How many names are tried ({@code name}, {@code name-2}, … {@code name-20}) before giving up. */
    static final int MAX_NAME_ATTEMPTS = 20;

    private static final int DELETE_ATTEMPTS = 5;
    private static final Duration DELETE_FIRST_DELAY = Duration.ofMillis(50);
    private static final Duration DELETE_MAX_DELAY = Duration.ofMillis(500);

    /** How the zip ended. */
    sealed interface Outcome {
        /**
         * The verified zip has its name.
         *
         * @param zip          the zip
         * @param zipBytes     its size
         * @param contentBytes the size of the files it holds
         */
        record Committed(Path zip, long zipBytes, long contentBytes) implements Outcome {
            public Committed {
                Objects.requireNonNull(zip, "zip");
            }
        }

        /** The monitor asked to stop; nothing is left behind. */
        record Cancelled() implements Outcome {
        }

        /** Something went wrong; nothing is left behind unless {@code message} names a file that could not go. */
        record Failed(String message) implements Outcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    private VerifiedZipCommit() {
        // Utility class - prevent instantiation
    }

    /**
     * Writes {@code tree} as {@code <destination>/<stem>.zip}, under the root entry {@code rootEntry}. A cancel is
     * possible until the final rename. Never throws, except an {@link Error}, after it has deleted the
     * {@code .partial}.
     *
     * @param mover          makes the final rename (a seam for tests)
     * @param retryFirstDelay the first wait before the rename is tried again; doubles up to {@code retryMaxDelay}
     */
    static Outcome write(FolderTree tree, String rootEntry, Path destination, String stem, JobMonitor monitor,
                         DirectoryMover mover, int commitAttempts, Duration retryFirstDelay, Duration retryMaxDelay) {
        Objects.requireNonNull(tree, "tree");
        Objects.requireNonNull(rootEntry, "rootEntry");
        Objects.requireNonNull(destination, "destination");
        Objects.requireNonNull(stem, "stem");
        Objects.requireNonNull(monitor, "monitor");
        Objects.requireNonNull(mover, "mover");

        // The .partial: from its creation on it is ours, and the only thing a failure deletes.
        FolderZipWriter.Written written = null;
        try {
            for (int attempt = 1; written == null; attempt++) {
                Path partial = destination.resolve(candidate(stem, attempt) + PARTIAL_EXTENSION);
                try {
                    written = FolderZipWriter.write(tree, rootEntry, partial, monitor);
                } catch (FileAlreadyExistsException e) {
                    // A .partial of an earlier backup that stopped; it is not ours to delete. (The writer creates its
                    // target first, so it wrote nothing here.)
                    if (attempt >= MAX_NAME_ATTEMPTS) {
                        throw e;
                    }
                }
            }
            FolderZipWriter.verify(written, monitor);
            monitor.checkCancelled();
        } catch (JobCancelledException e) {
            return abandon(written, new Outcome.Cancelled());
        } catch (IOException | RuntimeException e) {
            return abandon(written, new Outcome.Failed("Could not write the backup: " + describe(e)));
        } catch (Error e) {
            // Out of memory and the like: still take the .partial away (the writer removes its own on a failure
            // while writing), then let the caller's job report the Error.
            abandon(written, new Outcome.Failed("The backup stopped unexpectedly: " + e));
            throw e;
        }

        long zipBytes;
        try {
            zipBytes = Files.size(written.zip());
        } catch (IOException e) {
            return abandon(written, new Outcome.Failed("Could not read the backup back: " + describe(e)));
        }

        // The commit. A cancel is no longer possible.
        return commit(written, destination, stem, zipBytes, mover, commitAttempts, retryFirstDelay, retryMaxDelay);
    }

    private static Outcome commit(FolderZipWriter.Written written, Path destination, String stem, long zipBytes,
                                  DirectoryMover mover, int commitAttempts, Duration retryFirstDelay,
                                  Duration retryMaxDelay) {
        Path partial = written.zip();
        for (int attempt = 1; attempt <= MAX_NAME_ATTEMPTS; attempt++) {
            Path zip = destination.resolve(candidate(stem, attempt));
            try {
                TransientIo.retry(() -> mover.move(partial, zip), commitAttempts, retryFirstDelay, retryMaxDelay,
                        "moving the backup into place");
            } catch (FileAlreadyExistsException | DirectoryNotEmptyException e) {
                // Something has this name already; it is never replaced. Try the next one.
                continue;
            } catch (IOException | RuntimeException e) {
                return abandon(written, new Outcome.Failed("Could not move the backup into place: "
                        + describe(e) + ". Another program (often an antivirus scan) may be using it. Try again."));
            }

            // Files.move returns quietly when source and target are the same file; make sure the rename happened.
            if (!Files.isRegularFile(zip, LinkOption.NOFOLLOW_LINKS) || Files.exists(partial, LinkOption.NOFOLLOW_LINKS)) {
                return abandon(written, new Outcome.Failed("Could not move the backup into place: " + partial
                        + " is still there."));
            }
            return new Outcome.Committed(zip, zipBytes, written.contentBytes());
        }
        return abandon(written, new Outcome.Failed("Could not find a free name for the backup in "
                + destination + ": " + candidate(stem, 1) + " up to " + candidate(stem, MAX_NAME_ATTEMPTS)
                + " are all taken."));
    }

    /**
     * Deletes the {@code .partial} this call wrote (when it got that far) and returns {@code outcome}; when the file
     * cannot be deleted, a failure that says so instead.
     */
    private static Outcome abandon(FolderZipWriter.Written written, Outcome outcome) {
        if (written == null) {
            return outcome;
        }
        Path partial = written.zip();
        try {
            TransientIo.retry(() -> Files.deleteIfExists(partial), DELETE_ATTEMPTS, DELETE_FIRST_DELAY,
                    DELETE_MAX_DELAY, "deleting " + partial);
            return outcome;
        } catch (IOException e) {
            String left = "The temporary file " + partial + " could not be deleted (" + describe(e)
                    + "); you can delete it.";
            return switch (outcome) {
                case Outcome.Failed failed -> new Outcome.Failed(failed.message() + " " + left);
                case Outcome.Cancelled ignored -> new Outcome.Failed("The backup was cancelled. " + left);
                default -> new Outcome.Failed(left);
            };
        }
    }

    /** {@code stem.zip} for the first attempt, {@code stem-N.zip} after that. */
    static String candidate(String stem, int attempt) {
        return (attempt == 1 ? stem : stem + "-" + attempt) + ZIP_EXTENSION;
    }

    private static String describe(Throwable error) {
        return FileErrors.describe(error);
    }
}
