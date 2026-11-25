package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.ui.MainView;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.TextArea;
import javafx.scene.input.Clipboard;
import javafx.scene.input.ClipboardContent;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.Priority;
import javafx.stage.Modality;
import javafx.stage.Stage;
import javafx.stage.Window;

import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Objects;
import java.util.Optional;

/**
 * Builds every dialog, message box and dialog-like window of the launcher (VK-1694). Each one is owned by the window it
 * is shown over (the main window, or the dialog that opened it) and gets the launcher look: a dialog is a window of
 * its own, so it does not get the main window's stylesheet unless it is given it. The file and folder choosers are the
 * operating system's own and are not built here.
 * <p>
 * {@code DialogFactoryGuardTest} keeps every other class from building a dialog, a window or a stylesheet itself.
 */
public final class DialogFactory {

    /** The style class every launcher dialog pane carries; {@code styles.css} styles dialogs through it. */
    public static final String LAUNCHER_DIALOG = "launcher-dialog";

    private DialogFactory() {
        // Utility class - prevent instantiation
    }

    /**
     * Gives {@code pane} the launcher style class and stylesheet, and the stylesheet to the scene it is shown in: a
     * dialog's popups (a combo box's list, a tooltip) are windows of their own that take their owner scene's
     * stylesheets, not the pane's. Calling it again changes nothing.
     */
    public static void style(DialogPane pane) {
        Objects.requireNonNull(pane, "pane");
        if (!pane.getStyleClass().contains(LAUNCHER_DIALOG)) {
            pane.getStyleClass().add(LAUNCHER_DIALOG);
        }
        stylesheet().ifPresent(stylesheet -> {
            if (!pane.getStylesheets().contains(stylesheet)) {
                pane.getStylesheets().add(stylesheet);
            }
        });
        if (pane.getProperties().putIfAbsent(SCENE_STYLED, Boolean.TRUE) == null) {
            pane.sceneProperty().addListener((observable, was, scene) -> styleScene(scene));
        }
        styleScene(pane.getScene());
    }

    /** The key that marks a pane whose scene is followed, so {@link #style} adds one listener only. */
    private static final String SCENE_STYLED = DialogFactory.class.getName() + ".sceneStyled";

    private static void styleScene(Scene scene) {
        if (scene == null) {
            return;
        }
        stylesheet().ifPresent(stylesheet -> {
            if (!scene.getStylesheets().contains(stylesheet)) {
                scene.getStylesheets().add(stylesheet);
            }
        });
    }

    private static Optional<String> stylesheet() {
        try {
            return Optional.of(MainView.stylesheet().toExternalForm());
        } catch (IOException e) {
            // The self-test fails a build without the stylesheet; the dialog still works unstyled.
            return Optional.empty();
        }
    }

    /** A new dialog pane in the launcher look, for {@link Dialog#setDialogPane} or a {@link #stage}. */
    public static DialogPane pane() {
        DialogPane pane = new DialogPane();
        style(pane);
        return pane;
    }

    /**
     * A dialog owned by {@code owner}, titled {@code title}, whose pane is in the launcher look. A pane set later with
     * {@link Dialog#setDialogPane} must come from {@link #pane()}.
     *
     * @param owner the window the dialog is shown over; {@code null} only when there is none yet
     */
    public static <R> Dialog<R> dialog(Window owner, String title) {
        Dialog<R> dialog = new Dialog<>();
        dialog.setTitle(title);
        if (owner != null) {
            dialog.initOwner(owner);
        }
        style(dialog.getDialogPane());
        return dialog;
    }

    /** A plain message box owned by {@code owner}, in the launcher look; nothing is shown yet. */
    public static Alert alert(Alert.AlertType type, Window owner, String title, String message) {
        Alert alert = new Alert(type);
        alert.setTitle(title);
        alert.setHeaderText(null);
        alert.setContentText(message);
        if (owner != null) {
            alert.initOwner(owner);
        }
        style(alert.getDialogPane());
        return alert;
    }

    /** Asks an OK/Cancel question over {@code owner} and waits; {@code true} for OK. */
    public static boolean confirm(Window owner, String title, String message) {
        return alert(Alert.AlertType.CONFIRMATION, owner, title, message).showAndWait()
                .filter(ButtonType.OK::equals)
                .isPresent();
    }

    /** Shows an error message over {@code owner} and waits until it is closed. */
    public static void error(Window owner, String title, String message) {
        alert(Alert.AlertType.ERROR, owner, title, message).showAndWait();
    }

    /**
     * Shows {@code throwable} in an error dialog with its stack trace and a "Copy to Clipboard" button, and waits.
     *
     * @param owner the window to show it over, or {@code null} for none (before the main window exists, or while it
     *              is minimized: a dialog owned by a minimized window can stay out of sight)
     */
    public static void showException(Window owner, String title, String header, Throwable throwable) {
        Alert alert = new Alert(Alert.AlertType.ERROR);
        if (owner != null) {
            alert.initOwner(owner);
        }
        style(alert.getDialogPane());
        alert.setTitle(title);
        alert.setHeaderText(header);
        alert.setContentText(throwable.getMessage());

        StringWriter text = new StringWriter();
        throwable.printStackTrace(new PrintWriter(text));
        String stackTrace = text.toString();

        TextArea textArea = new TextArea(stackTrace);
        textArea.setEditable(false);
        textArea.setWrapText(false);
        textArea.setMaxWidth(Double.MAX_VALUE);
        textArea.setMaxHeight(Double.MAX_VALUE);
        GridPane.setVgrow(textArea, Priority.ALWAYS);
        GridPane.setHgrow(textArea, Priority.ALWAYS);

        GridPane details = new GridPane();
        details.setMaxWidth(Double.MAX_VALUE);
        details.add(textArea, 0, 0);
        alert.getDialogPane().setExpandableContent(details);

        ButtonType copyButton = new ButtonType("Copy to Clipboard");
        alert.getButtonTypes().add(copyButton);

        alert.showAndWait().ifPresent(response -> {
            if (response == copyButton) {
                ClipboardContent content = new ClipboardContent();
                content.putString(stackTrace);
                Clipboard.getSystemClipboard().setContent(content);
            }
        });
    }

    /**
     * A window of its own that shows {@code pane} (which must come from {@link #pane()}), owned by {@code owner}, for
     * dialogs whose closing the caller controls itself. Nothing is shown yet.
     *
     * @param owner the window it is shown over; {@code null} only when there is none
     */
    public static Stage stage(Window owner, Modality modality, String title, DialogPane pane) {
        Objects.requireNonNull(modality, "modality");
        Objects.requireNonNull(pane, "pane");
        Stage stage = new Stage();
        stage.setTitle(title);
        if (owner != null) {
            stage.initOwner(owner);
        }
        stage.initModality(modality);
        style(pane);
        stage.setScene(new Scene(pane));
        return stage;
    }
}
