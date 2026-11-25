package com.vertexforge.launcher.scriptlib;

import java.util.Objects;

/**
 * What the update does with one library file (VK-1691), from its project copy, its engine copy and its baseline.
 *
 * @param key         the file's {@linkplain com.vertexforge.launcher.template.LibraryManifest#pathKey key}
 * @param verdict     what happens to it
 * @param projectPath its exact path in the project's library, or {@code null} when the project has no such file
 * @param enginePath  its exact path in the engine's library, or {@code null} when the engine has no such file
 */
public record FileDecision(String key, Verdict verdict, String projectPath, String enginePath) {

    /** What happens to a file. */
    public enum Verdict {
        /** The project's copy already equals the engine's: it stays as it is. */
        NOOP(false),
        /** A file new in the engine: the engine's copy is added. */
        ADD(false),
        /** A file the user deleted: the engine's copy is put back. */
        RESTORE(false),
        /** A file the user never changed and the engine did: the engine's copy replaces it. */
        UPDATE(false),
        /** A file the engine removed and the user never changed: it is deleted, with its sidecar. */
        DELETE(false),
        /**
         * A file both the user and the engine changed (or the user added one where the engine now adds one): the
         * engine's copy replaces it and the user's goes to the backup, unless the user keeps theirs.
         */
        CONFLICT(true),
        /** A file only the user changed; the engine has nothing new for it, so the user's copy stays. */
        KEEP_EDIT(true),
        /** A file the engine removed but the user changed: the user's copy stays. */
        KEEP_REMOVED_EDITED(true),
        /** A file of the user's own, which the engine has never had: it stays. */
        USER_FILE(true);

        private final boolean userChanged;

        Verdict(boolean userChanged) {
            this.userChanged = userChanged;
        }

        /** Whether the project's copy holds something of the user's. */
        public boolean userChanged() {
            return userChanged;
        }
    }

    public FileDecision {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(verdict, "verdict");
        if (projectPath == null && enginePath == null) {
            throw new IllegalArgumentException("a decision is about a file that exists somewhere");
        }
    }

    /** The path the decision is shown under: the engine's spelling when it has the file, else the project's. */
    public String displayPath() {
        return enginePath != null ? enginePath : projectPath;
    }
}
