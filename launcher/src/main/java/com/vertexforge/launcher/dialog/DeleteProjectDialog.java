package com.vertexforge.launcher.dialog;

import javafx.geometry.Insets;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.TextField;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.util.Objects;
import java.util.Optional;

/**
 * Asks the user to type the project's name before "Move to Recycle Bin…" moves its folder (VK-1695). The button only
 * works once the name matches exactly, and it is never the default button, so Enter alone cannot confirm.
 */
public final class DeleteProjectDialog {

    private DeleteProjectDialog() {
        // Shown through show()
    }

    /** Shows the question and waits; the typed name when the user confirmed, empty when they cancelled. */
    public static Optional<String> show(Window owner, DeletePrompt prompt) {
        Objects.requireNonNull(prompt, "prompt");
        Dialog<String> dialog = DialogFactory.dialog(owner, DeletePrompt.TITLE);
        dialog.setHeaderText(prompt.header());
        dialog.setResizable(true);

        ButtonType move = new ButtonType("Move to Recycle Bin", ButtonBar.ButtonData.OK_DONE);
        DialogPane pane = DialogFactory.pane();
        pane.getStyleClass().add("delete-project-dialog");
        pane.getButtonTypes().addAll(move, ButtonType.CANCEL);

        Label where = new Label("This folder and everything in it goes to the Recycle Bin:");
        TextField folder = new TextField(prompt.folder().toString());
        folder.setEditable(false);
        folder.setFocusTraversable(false);
        Label summary = new Label(prompt.summary());
        VBox content = new VBox(10, where, folder, summary);
        for (String note : prompt.notes()) {
            content.getChildren().add(wrapped(note, "dialog-note"));
        }
        content.getChildren().add(wrapped(prompt.restoreHint(), null));
        Label confirm = new Label(prompt.confirmLabel());
        TextField typed = new TextField();
        typed.setPrefColumnCount(40);
        content.getChildren().addAll(confirm, typed);
        content.setPadding(new Insets(16));
        content.setMaxWidth(600);
        pane.setContent(content);
        dialog.setDialogPane(pane);

        Button moveButton = (Button) pane.lookupButton(move);
        moveButton.setDefaultButton(false);
        moveButton.setDisable(true);
        typed.textProperty().addListener((observable, was, now) -> moveButton.setDisable(!prompt.matches(now)));

        dialog.setOnShown(event -> typed.requestFocus());
        dialog.setResultConverter(type -> type == move && prompt.matches(typed.getText()) ? typed.getText() : null);
        return dialog.showAndWait();
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
