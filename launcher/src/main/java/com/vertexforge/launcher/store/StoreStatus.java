package com.vertexforge.launcher.store;

/** What a read of an {@link AtomicJsonStore} found (VK-1678). */
public enum StoreStatus {
    /** There is no file yet; the document is empty. */
    ABSENT,
    /** A file of the current version. */
    OK,
    /** A file of an older version; the next write migrates it. */
    LEGACY,
    /** A file written by a newer build. It is shown, but this build never writes it. */
    NEWER_READ_ONLY,
    /** The file is corrupt. Only {@link AtomicJsonStore#inspect()} reports this; {@code read()} moves it aside. */
    CORRUPT,
    /** The file was corrupt and has been moved aside; an empty document replaces it. */
    CORRUPT_QUARANTINED,
    /** The file is corrupt and could not be moved aside, so nothing may write it. */
    CORRUPT_UNMOVABLE,
    /** The file exists but could not be read (an I/O error), so nothing may write it. */
    UNREADABLE;

    /** Whether a document in this state must never be written by this build. */
    public boolean readOnly() {
        return this == NEWER_READ_ONLY || this == CORRUPT || this == CORRUPT_UNMOVABLE || this == UNREADABLE;
    }
}
