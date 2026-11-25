package com.vertexforge.launcher.env;

import java.util.Objects;

/**
 * What Windows says it would do with a folder if it were moved to the Recycle Bin now ({@link RecycleBin#check}),
 * learned without deleting anything (VK-1695). The confirmation dialog shows it before the user decides; the real
 * {@link RecycleBin#recycle} still refuses on its own, because the answer can change in between.
 */
public sealed interface RecycleCheck {

    /** Windows would move the folder to the Recycle Bin as one item. */
    record Recyclable() implements RecycleCheck {
    }

    /**
     * Windows would delete the folder permanently, so {@link RecycleBin#recycle} would refuse.
     *
     * @param reason why, in words for the user
     */
    record NotRecyclable(String reason) implements RecycleCheck {
        public NotRecyclable {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /**
     * Windows did not say: the folder is gone, the path is one the shell cannot take, or Windows did not answer in
     * time. A recycle may still work, and still refuses on its own.
     *
     * @param reason what went wrong, in words for the user (with the Windows error code when there is one)
     */
    record Unknown(String reason) implements RecycleCheck {
        public Unknown {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /** The caller cancelled the check before Windows answered. */
    record Cancelled() implements RecycleCheck {
    }
}
