package com.vertexforge.launcher.store;

import com.google.gson.JsonObject;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Optional;

/**
 * The state of an {@link AtomicJsonStore} document after a read or a write.
 *
 * @param status        what the read or write found
 * @param root          the document; an empty object when the file is absent, corrupt or unreadable. It is copied in
 *                      and out, so callers can never change the snapshot
 * @param fileVersion   the document's version, or 0 when there is no readable document
 * @param quarantinedTo where a corrupt file was moved, when this read or write moved it
 * @param detail        a human-readable reason for a non-OK status, or an empty string
 */
public record StoreSnapshot(
        StoreStatus status,
        JsonObject root,
        int fileVersion,
        Optional<Path> quarantinedTo,
        String detail
) {
    public StoreSnapshot {
        Objects.requireNonNull(status, "status");
        root = root == null ? new JsonObject() : root.deepCopy();
        quarantinedTo = quarantinedTo == null ? Optional.empty() : quarantinedTo;
        detail = detail == null ? "" : detail;
    }

    @Override
    public JsonObject root() {
        return root.deepCopy();
    }

    /** Whether this build must never write the document. */
    public boolean readOnly() {
        return status.readOnly();
    }
}
