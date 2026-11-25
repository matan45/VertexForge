package com.vertexforge.launcher;

import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.instance.CommandLine;
import com.vertexforge.launcher.instance.SingleInstance;
import com.vertexforge.launcher.selftest.SelfTest;
import com.vertexforge.launcher.util.LauncherLog;

import java.time.Duration;

/**
 * The module's main class (VK-1692: {@code com.vertexforge.launcher/com.vertexforge.launcher.Launcher}). A plain class
 * (not an {@code Application}) so that everything before the window (the self-test, the command line, the
 * single-instance claim) runs before any JavaFX class is loaded.
 */
public class Launcher {

    /**
     * How long a second launcher keeps trying to hand its arguments to the running one before it opens a window of
     * its own (VK-1685). Counted from the start of {@code main}, so a forwarding launcher exits well within 2 s.
     */
    static final Duration FORWARD_DEADLINE = Duration.ofMillis(1200);

    public static void main(String[] args) {
        long started = System.nanoTime();
        if (SelfTest.isRequested(args)) {
            // Decided before any Application code runs: no window, no dialogs, no user files, no single instance.
            System.exit(SelfTest.run(args));
            return;
        }

        CommandLine commandLine = CommandLine.parse(args, LauncherEnvironment.systemWorkingDirectory());
        LauncherEnvironment environment = LauncherEnvironment.system();
        LauncherLog log = LauncherLog.forConfigDir(environment.configDir(), environment.clock());

        SingleInstance.Claim claim;
        try {
            // A dev run (no jpackage exe) never forwards into, or takes requests meant for, the installed launcher.
            claim = SingleInstance.claim(environment.configDir(),
                    SingleInstance.baseName(environment.launcherExecutable().isPresent()), commandLine.message(),
                    started + FORWARD_DEADLINE.toNanos(), SingleInstance.Options.system(log::warn));
        } catch (RuntimeException e) {
            log.warn("The single-instance check failed, so this launcher runs on its own: " + e);
            claim = new SingleInstance.Independent(e.toString());
        }

        if (claim instanceof SingleInstance.Forwarded) {
            // The running launcher has the request and comes to the front.
            System.exit(0);
            return;
        }
        SingleInstance.Primary primary = claim instanceof SingleInstance.Primary held ? held : null;
        LauncherStartup.hand(new LauncherStartup(environment, log, primary, commandLine.message()));
        LauncherApp.main(args);
    }
}
