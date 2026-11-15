package com.vertexforge.launcher.session;

import java.util.Locale;
import java.util.Objects;
import java.util.Optional;

/**
 * One line of the editor's {@code --launch-status} file (VK-1682, {@code VFEngine/utilities/launch/LaunchStatusFile.hpp}):
 * <pre>{"state":"loading","code":0,"message":"Loading project C:\\Games\\Demo\\Demo.vfproj","pid":1234}</pre>
 *
 * @param state   how far the editor got
 * @param code    for {@link State#FAILED}: the exit code the editor gives up with (2 to 5), or the exception code of a
 *                crash (negative as a Java {@code int}); 0 otherwise
 * @param message text for a person (UTF-8 in the file); never parsed
 * @param pid     the editor's process id
 */
public record LaunchStatus(State state, int code, String message, long pid) {

    public enum State {
        /** Written as soon as the editor starts. */
        LOADING,
        /** The project and its startup scene have loaded. */
        READY,
        /** The editor gave up or crashed; nothing overwrites it. A crashed editor may still be alive. */
        FAILED;

        static Optional<State> fromJson(String text) {
            for (State state : values()) {
                if (state.name().toLowerCase(Locale.ROOT).equals(text)) {
                    return Optional.of(state);
                }
            }
            return Optional.empty();
        }
    }

    public LaunchStatus {
        Objects.requireNonNull(state, "state");
        Objects.requireNonNull(message, "message");
    }

    /** A {@code failed} line written by the crash handler: its code is a Windows exception code. */
    public boolean isCrash() {
        return state == State.FAILED && code < 0;
    }
}
