package com.vertexforge.launcher.ui;

import com.vertexforge.launcher.controller.MainController;
import com.vertexforge.launcher.service.ProjectHub;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;

import java.io.IOException;
import java.net.URL;

/**
 * Builds the main window's content. The launcher and {@code --self-test} both go through here, so the self-test
 * loads exactly what users see.
 */
public final class MainView {

    public static final String FXML_RESOURCE = "/fxml/main.fxml";
    public static final String STYLESHEET_RESOURCE = "/css/styles.css";
    static final int DEFAULT_WIDTH = 1024;
    static final int DEFAULT_HEIGHT = 768;

    private MainView() {
        // Utility class - prevent instantiation
    }

    /** Loads {@code main.fxml} with a {@link MainController} bound to {@code hub}. */
    public static Parent load(ProjectHub hub) throws IOException {
        URL fxml = MainView.class.getResource(FXML_RESOURCE);
        if (fxml == null) {
            throw new IOException("FXML not found: " + FXML_RESOURCE);
        }

        FXMLLoader loader = new FXMLLoader(fxml);
        loader.setControllerFactory(type -> {
            if (type == MainController.class) {
                return new MainController(hub);
            }
            throw new IllegalArgumentException("Unexpected controller type in " + FXML_RESOURCE + ": " + type.getName());
        });
        return loader.load();
    }

    public static URL stylesheet() throws IOException {
        URL stylesheetUrl = MainView.class.getResource(STYLESHEET_RESOURCE);
        if (stylesheetUrl == null) {
            throw new IOException("Stylesheet not found: " + STYLESHEET_RESOURCE);
        }
        return stylesheetUrl;
    }

    /** The main window's scene: default size plus the launcher stylesheet. */
    public static Scene createScene(Parent root) throws IOException {
        Scene scene = new Scene(root, DEFAULT_WIDTH, DEFAULT_HEIGHT);
        scene.getStylesheets().add(stylesheet().toExternalForm());
        return scene;
    }
}
