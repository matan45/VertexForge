package com.vertexforge.launcher;

import com.vertexforge.launcher.util.GlobalExceptionHandler;
import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.image.Image;
import javafx.stage.Stage;

import java.io.IOException;
import java.io.InputStream;
import java.util.Objects;

/**
 * Main JavaFX Application class for VertexForge Launcher.
 * <p>
 * Handles initialization of the launcher window, loading FXML layout,
 * applying styles, and setting up global exception handling.
 * </p>
 */
public class LauncherApp extends Application {

    private static final String APP_TITLE = "VertexForge Launcher";
    private static final int MIN_WIDTH = 800;
    private static final int MIN_HEIGHT = 600;
    private static final int DEFAULT_WIDTH = 1024;
    private static final int DEFAULT_HEIGHT = 768;

    /**
     * Main entry point called by {@link Launcher}.
     *
     * @param args command line arguments
     */
    public static void main(String[] args) {
        launch(args);
    }

    /**
     * Called before {@link #start(Stage)} to perform initialization.
     * Sets up global exception handling.
     */
    @Override
    public void init() {
        GlobalExceptionHandler.install();
    }

    /**
     * Main entry point for the JavaFX application.
     * Sets up the primary stage with the main window.
     *
     * @param primaryStage the primary stage for this application
     */
    @Override
    public void start(Stage primaryStage) {
        try {
            // Load FXML layout
            FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main.fxml"));
            Parent root = loader.load();

            // Create scene with default size
            Scene scene = new Scene(root, DEFAULT_WIDTH, DEFAULT_HEIGHT);

            // Apply dark theme stylesheet
            String stylesheet = Objects.requireNonNull(
                    getClass().getResource("/css/styles.css"),
                    "Stylesheet not found"
            ).toExternalForm();
            scene.getStylesheets().add(stylesheet);

            // Configure primary stage
            primaryStage.setTitle(APP_TITLE);
            primaryStage.setMinWidth(MIN_WIDTH);
            primaryStage.setMinHeight(MIN_HEIGHT);
            primaryStage.setScene(scene);

            // Set application icon
            loadApplicationIcon(primaryStage);

            // Show the window
            primaryStage.show();

        } catch (IOException e) {
            GlobalExceptionHandler.showErrorDialog(
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
        } catch (IOException e) {
            // Icon loading failure is not critical, just log it
            System.err.println("Warning: Failed to load application icon: " + e.getMessage());
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
