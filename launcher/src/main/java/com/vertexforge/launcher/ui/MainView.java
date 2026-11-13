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
     * @param creation   the single "vf-create" thread project files are written on (VK-1679), so that a slow creation
     *                   never holds up the list
     */
    public record Loaded(Parent root, MainController controller, ExecutorService background, ExecutorService creation) {
        public Loaded {
            Objects.requireNonNull(root, "root");
            Objects.requireNonNull(controller, "controller");
            Objects.requireNonNull(background, "background");
            Objects.requireNonNull(creation, "creation");
        }

        /**
         * Cancels a creation in progress and lets it clean up, then lets a save in progress finish (up to
         * {@code timeout} for both), then stops both threads. Must run on the FX thread. The creation thread is never
         * interrupted: an interrupt closes the file being written and the cleanup's own waits.
         *
         * @return whether both threads finished in time
         */
        public boolean shutdown(Duration timeout) throws InterruptedException {
            long deadline = System.nanoTime() + timeout.toNanos();
            controller.cancelCreation();
            creation.shutdown();
            boolean creationDone = creation.awaitTermination(timeout.toMillis(), TimeUnit.MILLISECONDS);
            // A creation that finished has handed its list add to the store thread by now, so that one still runs.
            background.shutdown();
            long left = Math.max(0, deadline - System.nanoTime());
            return background.awaitTermination(left, TimeUnit.NANOSECONDS) && creationDone;
        }

        /** Stops both threads at once (the self-test, which never creates through the window). */
        public void shutdownNow() {
            creation.shutdownNow();
            background.shutdownNow();
        }
    }

    /** Loads {@code main.fxml} with a {@link MainController} bound to {@code hub}. */
    public static Loaded load(ProjectHub hub) throws IOException {
        URL fxml = MainView.class.getResource(FXML_RESOURCE);
        if (fxml == null) {
            throw new IOException("FXML not found: " + FXML_RESOURCE);
        }

        ExecutorService background = newStoreExecutor();
        ExecutorService creation = newCreationExecutor();
        try {
            FXMLLoader loader = new FXMLLoader(fxml);
            loader.setControllerFactory(type -> {
                if (type == MainController.class) {
                    return new MainController(hub, background, creation);
                }
                throw new IllegalArgumentException("Unexpected controller type in " + FXML_RESOURCE + ": " + type.getName());
            });
            Parent root = loader.load();
            MainController controller = loader.getController();
            return new Loaded(root, controller, background, creation);
        } catch (IOException | RuntimeException e) {
            creation.shutdownNow();
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
     * One non-daemon thread, so closing the window never kills a save halfway (the launcher waits for it in
     * {@code stop()}); it ends after a few idle seconds, so it never keeps the JVM alive on its own.
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

    /**
     * One daemon thread for project creation (VK-1679). A daemon, unlike the store thread: a creation killed with the
     * JVM leaves only its staging folder, never a half-made project, while a write stuck on a dead network share
     * would otherwise keep the process alive for minutes. {@code stop()} still cancels it and waits for its cleanup.
     */
    static ExecutorService newCreationExecutor() {
        ThreadPoolExecutor executor = new ThreadPoolExecutor(1, 1, STORE_THREAD_IDLE_SECONDS, TimeUnit.SECONDS,
                new LinkedBlockingQueue<>(), runnable -> {
                    Thread thread = new Thread(runnable, "vf-create");
                    thread.setDaemon(true);
                    return thread;
                });
        executor.allowCoreThreadTimeOut(true);
        return executor;
    }
}
