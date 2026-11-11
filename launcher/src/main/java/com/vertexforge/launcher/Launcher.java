package com.vertexforge.launcher;

import com.vertexforge.launcher.selftest.SelfTest;

/** The jar's main class. A plain class (not an {@code Application}) so JavaFX runs from the shaded classpath jar. */
public class Launcher {

    public static void main(String[] args) {
        if (SelfTest.isRequested(args)) {
            // Decided before any Application code runs: no window, no dialogs, no user files.
            System.exit(SelfTest.run(args));
            return;
        }
        LauncherApp.main(args);
    }
}
