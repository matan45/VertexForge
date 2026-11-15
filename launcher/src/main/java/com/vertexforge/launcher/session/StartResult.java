package com.vertexforge.launcher.session;

import java.util.Objects;

/** Whether an editor could be started (VK-1683). */
public sealed interface StartResult {

    /** The editor runs; its session is not watching it yet. */
    record Started(SessionStart start) implements StartResult {
        public Started {
            Objects.requireNonNull(start, "start");
        }
    }

    /** Nothing was started (no editor, no working directory, no log, or the start itself failed). */
    record Failed(String message) implements StartResult {
        public Failed {
            Objects.requireNonNull(message, "message");
        }
    }
}
