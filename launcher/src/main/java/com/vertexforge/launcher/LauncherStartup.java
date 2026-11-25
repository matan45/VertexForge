package com.vertexforge.launcher;

import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.instance.InstanceMessage;
import com.vertexforge.launcher.instance.SingleInstance;
import com.vertexforge.launcher.util.LauncherLog;

import java.util.Objects;

/**
 * What {@link Launcher#main} decided before JavaFX started (VK-1685), handed to {@link LauncherApp}, which JavaFX
 * creates itself. It lives outside {@code LauncherApp} so that a second launcher, which only forwards its arguments,
 * never loads a JavaFX class.
 *
 * @param environment the environment the launcher runs in
 * @param log         the launcher's error log
 * @param primary     this launcher's single-instance claim, or {@code null} when it runs on its own (it could not
 *                    reach the running launcher in time); its inbox already holds {@code message}
 * @param message     this launcher's own command-line requests
 */
public record LauncherStartup(LauncherEnvironment environment, LauncherLog log, SingleInstance.Primary primary,
                              InstanceMessage message) {

    private static volatile LauncherStartup pending;

    public LauncherStartup {
        Objects.requireNonNull(environment, "environment");
        Objects.requireNonNull(log, "log");
        Objects.requireNonNull(message, "message");
    }

    /** Leaves this startup for {@link #take()}; called once, before JavaFX starts. */
    static void hand(LauncherStartup startup) {
        pending = Objects.requireNonNull(startup, "startup");
    }

    /** The startup {@link #hand} left, once; {@code null} when the launcher was not started through {@link Launcher}. */
    static LauncherStartup take() {
        LauncherStartup startup = pending;
        pending = null;
        return startup;
    }
}
