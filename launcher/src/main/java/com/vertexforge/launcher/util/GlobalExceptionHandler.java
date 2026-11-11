package com.vertexforge.launcher.util;

import javafx.application.Platform;
import javafx.scene.control.Alert;
import javafx.scene.control.ButtonType;
import javafx.scene.control.TextArea;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.Priority;
import javafx.stage.Stage;
import javafx.stage.Window;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Objects;
import java.util.function.Supplier;


public final class GlobalExceptionHandler {

    private static volatile LauncherLog log;

    private GlobalExceptionHandler() {
        // Utility class - prevent instantiation
    }


    /**
     * Logs every uncaught exception to {@code launcherLog} and shows it in a dialog owned by the main window
     * ({@code mainWindow} returns {@code null} until the window exists).
     */
    public static void install(LauncherLog launcherLog, Supplier<Window> mainWindow) {
        log = Objects.requireNonNull(launcherLog, "launcherLog");
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
     * sight (the launcher minimizes itself after opening the editor).
     */
    public static void showErrorDialog(Window owner, String title, String header, Throwable throwable) {
        Alert alert = new Alert(Alert.AlertType.ERROR);
        if (isUsableOwner(owner)) {
            alert.initOwner(owner);
        }
        alert.setTitle(title);
        alert.setHeaderText(header);
        alert.setContentText(throwable.getMessage());

        // Create expandable content with stack trace
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        throwable.printStackTrace(pw);
        String stackTrace = sw.toString();

        TextArea textArea = new TextArea(stackTrace);
        textArea.setEditable(false);
        textArea.setWrapText(false);
        textArea.setMaxWidth(Double.MAX_VALUE);
        textArea.setMaxHeight(Double.MAX_VALUE);
        GridPane.setVgrow(textArea, Priority.ALWAYS);
        GridPane.setHgrow(textArea, Priority.ALWAYS);

        GridPane expContent = new GridPane();
        expContent.setMaxWidth(Double.MAX_VALUE);
        expContent.add(textArea, 0, 0);

        alert.getDialogPane().setExpandableContent(expContent);

        // Add copy to clipboard button
        ButtonType copyButton = new ButtonType("Copy to Clipboard");
        alert.getButtonTypes().add(copyButton);

        alert.showAndWait().ifPresent(response -> {
            if (response == copyButton) {
                javafx.scene.input.Clipboard clipboard = javafx.scene.input.Clipboard.getSystemClipboard();
                javafx.scene.input.ClipboardContent content = new javafx.scene.input.ClipboardContent();
                content.putString(stackTrace);
                clipboard.setContent(content);
            }
        });
    }

    private static boolean isUsableOwner(Window owner) {
        if (owner == null || !owner.isShowing()) {
            return false;
        }
        return !(owner instanceof Stage stage && stage.isIconified());
    }
}
