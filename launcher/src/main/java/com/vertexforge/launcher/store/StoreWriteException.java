package com.vertexforge.launcher.store;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Optional;

/**
 * A write to an {@link AtomicJsonStore} that did not happen. The previous file is always left as it was, except that
 * a corrupt file may have been moved aside first; {@link #quarantinedTo()} then says where.
 */
public final class StoreWriteException extends Exception {

    public enum Reason {
        /** Another launcher held the lock for longer than the timeout. */
        LOCK_TIMEOUT,
        /** The file was written by a newer build; this build never overwrites it. */
        NEWER_VERSION,
        /** The file is corrupt and could not be moved aside, so it is never overwritten. */
        CORRUPT_UNMOVABLE,
        /** The file exists but could not be read, so it is never overwritten. */
        UNREADABLE,
        /** Writing the new file or replacing the old one failed. */
        IO
    }

    private final Reason reason;
    private final Path quarantinedTo;

    public StoreWriteException(Reason reason, String message, Throwable cause) {
        this(reason, message, cause, null);
    }

    public StoreWriteException(Reason reason, String message, Throwable cause, Path quarantinedTo) {
        super(message, cause);
        this.reason = Objects.requireNonNull(reason, "reason");
        this.quarantinedTo = quarantinedTo;
    }

    public Reason reason() {
        return reason;
    }

    /** Where this write moved a corrupt file before it failed, if it did; the UI still has to say so. */
    public Optional<Path> quarantinedTo() {
        return Optional.ofNullable(quarantinedTo);
    }
}
