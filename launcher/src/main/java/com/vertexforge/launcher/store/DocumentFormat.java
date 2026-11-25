package com.vertexforge.launcher.store;

import com.google.gson.JsonObject;

import java.util.Objects;
import java.util.OptionalInt;

/**
 * Describes one kind of document an {@link AtomicJsonStore} keeps.
 *
 * @param currentVersion    the version this build writes; a file with a higher {@code "version"} is read-only
 * @param versionWhenAbsent the version assumed when a file has no {@code "version"}; empty means such a file is corrupt
 * @param structureCheck    rejects a document of a known version whose shape is wrong
 */
public record DocumentFormat(
        int currentVersion,
        OptionalInt versionWhenAbsent,
        StructureCheck structureCheck
) {
    /** The top-level member every store document carries. */
    public static final String VERSION_KEY = "version";

    public DocumentFormat {
        if (currentVersion < 1) {
            throw new IllegalArgumentException("currentVersion must be at least 1");
        }
        Objects.requireNonNull(versionWhenAbsent, "versionWhenAbsent");
        Objects.requireNonNull(structureCheck, "structureCheck");
        if (versionWhenAbsent.isPresent() && versionWhenAbsent.getAsInt() < 1) {
            throw new IllegalArgumentException("versionWhenAbsent must be at least 1");
        }
    }

    /** Checks a document whose version is not newer than {@link #currentVersion()}. */
    @FunctionalInterface
    public interface StructureCheck {
        void check(JsonObject root) throws CorruptDocumentException;
    }
}
