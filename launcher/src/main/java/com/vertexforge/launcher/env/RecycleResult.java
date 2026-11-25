package com.vertexforge.launcher.env;

import java.util.Objects;

/**
 * How {@link RecycleBin#recycle} ended (VK-1695). Only {@link Recycled} means the folder is in the Recycle Bin;
 * {@link NotRecyclable} and {@link Refused} mean nothing was deleted; {@link Failed} is everything else.
 */
public sealed interface RecycleResult {

    /** The folder is in the Recycle Bin, as one item the user can restore. */
    record Recycled() implements RecycleResult {
    }

    /**
     * Windows would have deleted the folder permanently, so the operation was stopped before it deleted anything.
     *
     * @param reason why, in words for the user
     */
    record NotRecyclable(String reason) implements RecycleResult {
        public NotRecyclable {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /**
     * Windows refused to move the folder, and nothing was deleted: a file in it is in use, access is denied, or the
     * move needs administrator rights (the launcher never asks for them).
     *
     * @param reason    why, in words for the user
     * @param retryable whether trying again later can work (closing the program that holds a file open); false when
     *                  only administrator rights would help
     */
    record Refused(String reason, boolean retryable) implements RecycleResult {
        public Refused {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /**
     * Anything else: Windows reported an error, did not finish in time, or reported a delete that did not go into the
     * Recycle Bin. The folder may be partly moved; the message says what happened and where to look.
     *
     * @param message what went wrong, in words for the user (with the Windows error code when there is one)
     */
    record Failed(String message) implements RecycleResult {
        public Failed {
            Objects.requireNonNull(message, "message");
        }
    }
}
