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
import java.net.URL;


public class LauncherApp extends Application {

    private static final String APP_TITLE = "VertexForge Launcher";
    private static final int MIN_WIDTH = 800;
    private static final int MIN_HEIGHT = 600;
    private static final int DEFAULT_WIDTH = 1024;
    private static final int DEFAULT_HEIGHT = 768;

    public static void main(String[] args) {
        launch(args);
    }

    @Override
    public void init() {
        GlobalExceptionHandler.install();
    }

    @Override
    public void start(Stage primaryStage) {
        try {
            FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main.fxml"));
            Parent root = loader.load();

            Scene scene = new Scene(root, DEFAULT_WIDTH, DEFAULT_HEIGHT);

            URL stylesheetUrl = getClass().getResource("/css/styles.css");
            if (stylesheetUrl == null) {
                throw new IOException("Stylesheet not found: /css/styles.css");
            }
            scene.getStylesheets().add(stylesheetUrl.toExternalForm());

            primaryStage.setTitle(APP_TITLE);
            primaryStage.setMinWidth(MIN_WIDTH);
            primaryStage.setMinHeight(MIN_HEIGHT);
            primaryStage.setScene(scene);

            loadApplicationIcon(primaryStage);

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
        } catch (Exception e) {
            // Icon loading failure is not critical, just log it
            // Catches IOException, IllegalArgumentException (invalid image), etc.
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
