package com.vertexforge.launcher;

/**
 * Entry point for the fat JAR distribution.
 * <p>
 * This class MUST NOT extend {@link javafx.application.Application}.
 * It exists to bypass JavaFX's module system check when running from a shaded JAR.
 * The check fails if the main class extends Application in a non-modular environment.
 * </p>
 */
public class Launcher {

    /**
     * Main entry point for the application.
     * Delegates to {@link LauncherApp#main(String[])} to start JavaFX.
     *
     * @param args command line arguments
     */
    public static void main(String[] args) {
        LauncherApp.main(args);
    }
}
