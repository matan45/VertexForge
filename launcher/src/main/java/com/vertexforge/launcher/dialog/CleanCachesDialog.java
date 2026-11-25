package com.vertexforge.launcher.dialog;

import javafx.geometry.Insets;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.util.Objects;

/** Shows what "Clean Caches…" deletes and asks to go ahead (VK-1695). */
public final class CleanCachesDialog {

    private CleanCachesDialog() {
        // Shown through show()
    }

    /** Shows the question and waits; {@code true} when the user chose Clean. */
    public static boolean show(Window owner, CleanCachesPrompt prompt) {
        Objects.requireNonNull(prompt, "prompt");
        Dialog<Boolean> dialog = DialogFactory.dialog(owner, CleanCachesPrompt.TITLE);
        dialog.setHeaderText(prompt.header());
        dialog.setResizable(true);

        ButtonType clean = new ButtonType("Clean", ButtonBar.ButtonData.OK_DONE);
        DialogPane pane = DialogFactory.pane();
        pane.getStyleClass().add("clean-caches-dialog");
        pane.getButtonTypes().addAll(clean, ButtonType.CANCEL);

        VBox content = new VBox(8, new Label("These folders are deleted:"));
        for (String line : prompt.presentLines()) {
            content.getChildren().add(wrapped("    " + line, null));
        }
        if (!prompt.skippedLines().isEmpty()) {
            content.getChildren().add(new Label("Left alone:"));
            for (String line : prompt.skippedLines()) {
                content.getChildren().add(wrapped("    " + line, "dialog-note"));
            }
        }
        content.getChildren().add(wrapped(prompt.explanation(), null));
        content.setPadding(new Insets(16));
        content.setMaxWidth(600);
        pane.setContent(content);
        dialog.setDialogPane(pane);

        dialog.setResultConverter(type -> type == clean);
        return dialog.showAndWait().orElse(false);
    }

    private static Label wrapped(String text, String styleClass) {
        Label label = new Label(text);
        label.setWrapText(true);
        label.setMaxWidth(560);
        if (styleClass != null) {
            label.getStyleClass().add(styleClass);
        }
        return label;
    }
}
