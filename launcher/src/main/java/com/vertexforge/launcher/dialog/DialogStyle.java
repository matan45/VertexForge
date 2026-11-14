package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.ui.MainView;
import javafx.scene.control.Alert;
import javafx.scene.control.DialogPane;
import javafx.stage.Window;

import java.io.IOException;

/** Gives the launcher's dialogs the dark launcher look (VK-1679, VK-1681). */
final class DialogStyle {

    /** The style class every launcher dialog pane carries; {@code styles.css} styles dialogs through it. */
    static final String LAUNCHER_DIALOG = "launcher-dialog";

    private DialogStyle() {
        // Utility class - prevent instantiation
    }

    /**
     * Adds the launcher style class and stylesheet to {@code pane}. A dialog is a window of its own: it does not get
     * the main window's stylesheet unless it is given it.
     */
    static void apply(DialogPane pane) {
        pane.getStyleClass().add(LAUNCHER_DIALOG);
        try {
            pane.getStylesheets().add(MainView.stylesheet().toExternalForm());
        } catch (IOException e) {
            // The self-test fails a build without the stylesheet; the dialog still works unstyled.
        }
    }

    /** A plain message box owned by {@code owner}, in the launcher look. */
    static Alert alert(Alert.AlertType type, Window owner, String title, String message) {
        Alert alert = new Alert(type);
        alert.setTitle(title);
        alert.setHeaderText(null);
        alert.setContentText(message);
        alert.initOwner(owner);
        apply(alert.getDialogPane());
        return alert;
    }
}
