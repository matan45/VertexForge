package com.vertexforge.launcher.instance;

/**
 * Lets the running launcher bring its window to the front (VK-1685). Windows only gives the foreground to a process
 * that is allowed to take it: the launcher just started by a double-click is, the one already running in the
 * background is not, so the new one passes its right on with {@code AllowSetForegroundWindow} before it forwards its
 * request.
 */
@FunctionalInterface
public interface ForegroundGrant {

    /** Lets process {@code pid} take the foreground. Never throws; does nothing when it cannot. */
    void allow(long pid);

    /** {@code user32!AllowSetForegroundWindow} through the Foreign Function API; a no-op off Windows. */
    static ForegroundGrant system() {
        return WindowsForegroundGrant.INSTANCE;
    }

    /** Grants nothing (tests, and anything that must make no native call). */
    static ForegroundGrant none() {
        return pid -> {
        };
    }
}
