package com.vertexforge.launcher.util;

import javafx.application.Platform;
import javafx.scene.control.Alert;
import javafx.scene.control.ButtonType;
import javafx.scene.control.TextArea;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.Priority;

import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;


public final class GlobalExceptionHandler {

    private static final String LOG_FILE_NAME = "launcher-error.log";
    private static final DateTimeFormatter DATE_FORMAT = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    private GlobalExceptionHandler() {
        // Utility class - prevent instantiation
    }


    public static void install() {
        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            logException(throwable);

            // Show error dialog on JavaFX Application Thread
            if (Platform.isFxApplicationThread()) {
                showErrorDialog("Unexpected Error", "An unexpected error occurred.", throwable);
            } else {
                Platform.runLater(() ->
                        showErrorDialog("Unexpected Error", "An unexpected error occurred.", throwable)
                );
            }
        });
    }


    public static void logException(Throwable throwable) {
        Path logPath = getLogFilePath();

        try (PrintWriter writer = new PrintWriter(new FileWriter(logPath.toFile(), true))) {
            writer.println("=".repeat(80));
            writer.println("Timestamp: " + LocalDateTime.now().format(DATE_FORMAT));
            writer.println("Thread: " + Thread.currentThread().getName());
            writer.println("-".repeat(80));
            throwable.printStackTrace(writer);
            writer.println();
        } catch (IOException e) {
            System.err.println("Failed to write to error log: " + e.getMessage());
            throwable.printStackTrace();
        }
    }

    public static void showErrorDialog(String title, String header, Throwable throwable) {
        Alert alert = new Alert(Alert.AlertType.ERROR);
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

    private static Path getLogFilePath() {
        String userHome = System.getProperty("user.home");
        Path logDir = Paths.get(userHome, ".vertexforge");
        try {
            Files.createDirectories(logDir);
        } catch (IOException e) {
            System.err.println("Failed to create log directory: " + e.getMessage());
        }
        return logDir.resolve(LOG_FILE_NAME);
    }
}
