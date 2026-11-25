package com.vertexforge.launcher.project;

import java.util.Objects;
import java.util.OptionalLong;

/**
 * Whether an editor has a project folder open, as {@link EditorLockProbe} found it (VK-1686). Only {@link Held}
 * makes a row "Already open"; the other states let it open, as the editor itself would.
 */
public sealed interface EditorLockState {

    /** No editor holds the folder's lock (or there is no lock file at all). */
    EditorLockState FREE = new Free();

    /** The lock was not looked at: an editor this launcher started has a project of the folder open. */
    EditorLockState SKIPPED = new Skipped();

    record Free() implements EditorLockState {
    }

    /**
     * Another editor holds the folder's lock.
     *
     * @param pid the process id the lock file names; for the tooltip only, as it can be stale or another machine's
     */
    record Held(OptionalLong pid) implements EditorLockState {
        public Held {
            Objects.requireNonNull(pid, "pid");
        }
    }

    /**
     * The lock could not be checked (no access, no lock support, a share that failed): the editor would open the
     * project anyway, unprotected or after its own check, so this never blocks Open.
     *
     * @param reason why, for the tooltip
     */
    record Unknown(String reason) implements EditorLockState {
        public Unknown {
            Objects.requireNonNull(reason, "reason");
        }
    }

    record Skipped() implements EditorLockState {
    }
}
