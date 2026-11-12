package com.vertexforge.launcher.ui;

import com.vertexforge.launcher.controller.MainController;
import com.vertexforge.launcher.service.ProjectHub;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;

import java.io.IOException;
import java.net.URL;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/**
 * Builds the main window's content. The launcher and {@code --self-test} both go through here, so the self-test
 * loads exactly what users see.
 */
public final class MainView {

    public static final String FXML_RESOURCE = "/fxml/main.fxml";
    public static final String STYLESHEET_RESOURCE = "/css/styles.css";
    static final int DEFAULT_WIDTH = 1024;
    static final int DEFAULT_HEIGHT = 768;
    /** An idle store thread ends after this, so it never keeps the JVM alive on its own. */
    static final long STORE_THREAD_IDLE_SECONDS = 5;

    private MainView() {
        // Utility class - prevent instantiation
    }

    /**
     * The loaded window content.
     *
     * @param root       the scene root
     * @param controller the window's controller
     * @param background the single "vf-store" thread every list read and change runs on (VK-1678): one at a time,
     *                   in order, and never on the FX thread
     */
    public record Loaded(Parent root, MainController controller, ExecutorService background) {
        public Loaded {
            Objects.requireNonNull(root, "root");
            Objects.requireNonNull(controller, "controller");
            Objects.requireNonNull(background, "background");
        }

        /** Lets a create or save in progress finish (up to {@code timeout}), then stops the store thread. */
        public boolean shutdown(Duration timeout) throws InterruptedException {
            background.shutdown();
            return background.awaitTermination(timeout.toMillis(), TimeUnit.MILLISECONDS);
        }
    }

    /** Loads {@code main.fxml} with a {@link MainController} bound to {@code hub}. */
    public static Loaded load(ProjectHub hub) throws IOException {
        URL fxml = MainView.class.getResource(FXML_RESOURCE);
        if (fxml == null) {
            throw new IOException("FXML not found: " + FXML_RESOURCE);
        }

        ExecutorService background = newStoreExecutor();
        try {
            FXMLLoader loader = new FXMLLoader(fxml);
            loader.setControllerFactory(type -> {
                if (type == MainController.class) {
                    return new MainController(hub, background);
                }
                throw new IllegalArgumentException("Unexpected controller type in " + FXML_RESOURCE + ": " + type.getName());
            });
            Parent root = loader.load();
            MainController controller = loader.getController();
            return new Loaded(root, controller, background);
        } catch (IOException | RuntimeException e) {
            background.shutdownNow();
            throw e;
        }
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

    /**
     * One non-daemon thread, so closing the window never kills a create or a save halfway (the launcher waits for it
     * in {@code stop()}); it ends after a few idle seconds, so it never keeps the JVM alive on its own.
     */
    static ExecutorService newStoreExecutor() {
        ThreadPoolExecutor executor = new ThreadPoolExecutor(1, 1, STORE_THREAD_IDLE_SECONDS, TimeUnit.SECONDS,
                new LinkedBlockingQueue<>(), runnable -> {
                    Thread thread = new Thread(runnable, "vf-store");
                    thread.setDaemon(false);
                    return thread;
                });
        executor.allowCoreThreadTimeOut(true);
        return executor;
    }
}
