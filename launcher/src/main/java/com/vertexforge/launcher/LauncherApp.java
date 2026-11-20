package com.vertexforge.launcher;

import com.vertexforge.launcher.controller.MainController;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.instance.InstanceMessage;
import com.vertexforge.launcher.instance.SingleInstance;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.ui.MainView;
import com.vertexforge.launcher.util.GlobalExceptionHandler;
import com.vertexforge.launcher.util.LauncherLog;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.scene.Scene;
import javafx.scene.image.Image;
import javafx.stage.Stage;

import java.io.IOException;
import java.io.InputStream;
import java.time.Duration;


public class LauncherApp extends Application {

    private static final String APP_TITLE = "VertexForge Launcher";
    private static final int MIN_WIDTH = 800;
    private static final int MIN_HEIGHT = 600;

    /** How long closing the window waits for a cancelled creation's cleanup and a save in progress (VK-1678). */
    private static final Duration SHUTDOWN_WAIT = Duration.ofSeconds(10);

    private LauncherEnvironment environment;
    private LauncherLog log;
    /**
     * This launcher's single-instance claim (VK-1685), or {@code null} when it runs on its own: started without
     * {@link Launcher}, or unable to reach the running launcher in time.
     */
    private SingleInstance.Primary instance;
    /** This launcher's own command-line requests; already in {@link #instance}'s inbox when there is one. */
    private InstanceMessage ownRequests = InstanceMessage.EMPTY;
    private ProjectHub hub;
    private MainView.Loaded view;
    /** The main window once it is showing; the owner of every error dialog. */
    private volatile Stage mainStage;

    public static void main(String[] args) {
        launch(args);
    }

    @Override
    public void init() {
        LauncherStartup startup = LauncherStartup.take();
        if (startup != null) {
            environment = startup.environment();
            log = startup.log();
            instance = startup.primary();
            ownRequests = startup.message();
        } else {
            // Started without Launcher#main: no command line and no single instance.
            environment = LauncherEnvironment.system();
            log = LauncherLog.forConfigDir(environment.configDir(), environment.clock());
            log.warn("The launcher was started without " + Launcher.class.getName()
                    + ", so it ignores its command line and does not check for a running launcher");
        }
        GlobalExceptionHandler.install(log, () -> mainStage);
    }

    @Override
    public void start(Stage primaryStage) {
        try {
            hub = ProjectHub.create(environment, log);
            view = MainView.load(hub);
            Scene scene = MainView.createScene(view.root());

            primaryStage.setTitle(APP_TITLE);
            primaryStage.setMinWidth(MIN_WIDTH);
            primaryStage.setMinHeight(MIN_HEIGHT);
            primaryStage.setScene(scene);

            loadApplicationIcon(primaryStage);
            // From the moment the window is closing, a second launcher is told to start on its own (VK-1685).
            primaryStage.setOnCloseRequest(event -> markClosing());

            primaryStage.show();
            mainStage = primaryStage;
            takeRequests(view.controller());

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
     * Hands the window this launcher's own command-line requests and, from now on, every request a second launcher
     * forwards (VK-1685). Forwarded requests arrive on the single-instance threads and are run on the FX thread.
     */
    private void takeRequests(MainController controller) {
        controller.setBeforeExit(this::markClosing);
        if (instance != null) {
            // Platform.runLater throws once the toolkit has exited: the inbox then refuses the request, and the
            // second launcher opens a window of its own instead of losing it.
            instance.inbox().attach(message -> Platform.runLater(() -> controller.handleRequests(message)));
        } else {
            controller.handleRequests(ownRequests);
        }
    }

    /** A second launcher that connects from now on is refused, so it opens a window of its own instead. */
    private void markClosing() {
        if (instance != null) {
            instance.inbox().markClosing();
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
        // First: a second launcher started now becomes the running one instead of waiting for this one to finish.
        if (instance != null) {
            instance.release();
        }
        // A creation in progress is cancelled and cleans up after itself; a save on the store thread is let finish
        // rather than cut off. The store thread is not a daemon, so the process only ends once that work is done.
        if (view != null) {
            boolean creating = view.controller().creationRunning();
            try {
                if (!view.shutdown(SHUTDOWN_WAIT)) {
                    log.warn("A project creation, backup or copy, or a recent-projects save was still running after "
                            + SHUTDOWN_WAIT.toSeconds() + " s; the launcher exits as soon as the save finishes"
                            + (creating ? ". The cancelled job may leave a temporary \"."
                                    + "<name>" + ProjectCreator.STAGING_INFIX + "<id>\" folder (a creation or a copy)"
                                    + " or a \"<name>.zip.partial\" file (a backup) beside the project" : ""));
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        if (hub != null) {
            hub.close();
        }
    }
}
