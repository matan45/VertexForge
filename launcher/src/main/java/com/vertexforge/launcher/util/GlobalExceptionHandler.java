package com.vertexforge.launcher.util;

import javafx.application.Platform;
import javafx.stage.Stage;
import javafx.stage.Window;

import java.util.Objects;
import java.util.function.Supplier;


public final class GlobalExceptionHandler {

    /** Shows an error to the user (the launcher's themed error dialog, VK-1694). FX thread. */
    @FunctionalInterface
    public interface ErrorPresenter {
        /**
         * @param owner the window to show it over, or {@code null} for none
         */
        void show(Window owner, String title, String header, Throwable throwable);
    }

    private static volatile LauncherLog log;
    private static volatile ErrorPresenter presenter;

    private GlobalExceptionHandler() {
        // Utility class - prevent instantiation
    }


    /**
     * Logs every uncaught exception to {@code launcherLog} and shows it with {@code errorPresenter}, over the main
     * window ({@code mainWindow} returns {@code null} until the window exists).
     */
    public static void install(LauncherLog launcherLog, Supplier<Window> mainWindow, ErrorPresenter errorPresenter) {
        log = Objects.requireNonNull(launcherLog, "launcherLog");
        presenter = Objects.requireNonNull(errorPresenter, "errorPresenter");
        Objects.requireNonNull(mainWindow, "mainWindow");
        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            logException(throwable);

            // Show error dialog on JavaFX Application Thread (the owner is read there too)
            if (Platform.isFxApplicationThread()) {
                showErrorDialog(mainWindow.get(), "Unexpected Error", "An unexpected error occurred.", throwable);
            } else {
                Platform.runLater(() ->
                        showErrorDialog(mainWindow.get(), "Unexpected Error", "An unexpected error occurred.", throwable)
                );
            }
        });
    }


    public static void logException(Throwable throwable) {
        LauncherLog current = log;
        if (current != null) {
            current.error(throwable);
        } else {
            // Not installed (e.g. a failure before init): the console is all there is.
            throwable.printStackTrace();
        }
    }

    /**
     * Shows {@code throwable} in an error dialog. {@code owner} is the main window, or {@code null} before it
     * exists; a hidden or minimized owner is skipped, since a dialog owned by a minimized window can stay out of
     * sight (the launcher minimizes itself after opening the editor). Before {@link #install} nothing is shown: the
     * error has been logged, and there is no dialog to show it with yet.
     */
    public static void showErrorDialog(Window owner, String title, String header, Throwable throwable) {
        ErrorPresenter current = presenter;
        if (current != null) {
            current.show(isUsableOwner(owner) ? owner : null, title, header, throwable);
        }
    }

    private static boolean isUsableOwner(Window owner) {
        if (owner == null || !owner.isShowing()) {
            return false;
        }
        return !(owner instanceof Stage stage && stage.isIconified());
    }
}
