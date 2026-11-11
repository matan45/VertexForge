package com.vertexforge.launcher;

import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.ui.MainView;
import com.vertexforge.launcher.util.GlobalExceptionHandler;
import com.vertexforge.launcher.util.LauncherLog;
import javafx.application.Application;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.image.Image;
import javafx.stage.Stage;

import java.io.IOException;
import java.io.InputStream;


public class LauncherApp extends Application {

    private static final String APP_TITLE = "VertexForge Launcher";
    private static final int MIN_WIDTH = 800;
    private static final int MIN_HEIGHT = 600;

    private LauncherEnvironment environment;
    private LauncherLog log;
    /** The main window once it is showing; the owner of every error dialog. */
    private volatile Stage mainStage;

    public static void main(String[] args) {
        launch(args);
    }

    @Override
    public void init() {
        environment = LauncherEnvironment.system();
        log = LauncherLog.forConfigDir(environment.configDir(), environment.clock());
        GlobalExceptionHandler.install(log, () -> mainStage);
    }

    @Override
    public void start(Stage primaryStage) {
        try {
            ProjectHub hub = ProjectHub.create(environment, log);
            Parent root = MainView.load(hub);
            Scene scene = MainView.createScene(root);

            primaryStage.setTitle(APP_TITLE);
            primaryStage.setMinWidth(MIN_WIDTH);
            primaryStage.setMinHeight(MIN_HEIGHT);
            primaryStage.setScene(scene);

            loadApplicationIcon(primaryStage);

            primaryStage.show();
            mainStage = primaryStage;

        } catch (IOException e) {
            // No main window exists yet, so this is the one error dialog without an owner.
            GlobalExceptionHandler.logException(e);
            GlobalExceptionHandler.showErrorDialog(
                    null,
                    "Failed to Load Application",
                    "The launcher failed to initialize properly.",
                    e
            );
        }
    }

    /**
     * Loads and sets the application icon on the primary stage.
     *
     * @param stage the stage to set the icon on
     */
    private void loadApplicationIcon(Stage stage) {
        try (InputStream iconStream = getClass().getResourceAsStream("/images/icon.png")) {
            if (iconStream != null) {
                stage.getIcons().add(new Image(iconStream));
            }
        } catch (Exception e) {
            // Icon loading failure is not critical, just log it
            // Catches IOException, IllegalArgumentException (invalid image), etc.
            log.warn("Warning: Failed to load application icon: " + e.getMessage());
        }
    }

    /**
     * Called when the application should stop.
     * Performs cleanup operations.
     */
    @Override
    public void stop() {
        // Cleanup resources if needed
    }
}
