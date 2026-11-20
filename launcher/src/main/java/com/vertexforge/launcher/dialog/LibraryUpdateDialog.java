package com.vertexforge.launcher.dialog;

import javafx.geometry.Insets;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.ScrollPane;
import javafx.scene.control.TitledPane;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.util.Objects;
import java.util.Optional;

/**
 * Shows what a script-library update would do and asks to go ahead (VK-1691). Shows a {@link LibraryUpdatePrompt};
 * touches no file.
 */
public final class LibraryUpdateDialog {

    private static final double WIDTH = 640;
    /** Lists longer than this many files start collapsed. */
    private static final int EXPANDED_UP_TO = 12;

    private LibraryUpdateDialog() {
        // Shown through show()
    }

    /**
     * Asks and waits. Empty when the user cancels; otherwise whether files in conflict keep the user's copy.
     */
    public static Optional<Boolean> show(Window owner, LibraryUpdatePrompt prompt) {
        Objects.requireNonNull(prompt, "prompt");
        Dialog<Boolean> dialog = new Dialog<>();
        dialog.setTitle(prompt.title());
        dialog.setHeaderText(prompt.header());
        dialog.initOwner(owner);
        dialog.setResizable(true);

        DialogPane pane = new DialogPane();
        DialogStyle.apply(pane);
        pane.getStyleClass().add("library-update-dialog");
        ButtonType update = new ButtonType("Update", ButtonBar.ButtonData.OK_DONE);
        pane.getButtonTypes().setAll(update, ButtonType.CANCEL);

        CheckBox keepMine = new CheckBox(LibraryUpdatePrompt.KEEP_MINE + " (" + prompt.conflicts() + ")");
        pane.setContent(content(prompt, keepMine));
        dialog.setDialogPane(pane);

        Node updateNode = pane.lookupButton(update);
        if (updateNode instanceof Button button) {
            button.setDefaultButton(true);
        }
        // The window's close button answers Cancel: the pane has a cancel button.
        dialog.setResultConverter(type -> type == update ? keepMine.isSelected() : null);
        return dialog.showAndWait();
    }

    private static Node content(LibraryUpdatePrompt prompt, CheckBox keepMine) {
        VBox content = new VBox(10);
        content.setPadding(new Insets(16));
        content.getChildren().add(wrapped(prompt.intro(), null));

        for (LibraryUpdatePrompt.Group group : prompt.groups()) {
            ListView<String> files = new ListView<>();
            files.getItems().setAll(group.files());
            files.setPrefHeight(Math.min(group.files().size(), 8) * 24 + 4);
            VBox body = new VBox(6, wrapped(group.explanation(), "dialog-note"), files);
            TitledPane pane = new TitledPane(group.label(), body);
            pane.setExpanded(group.files().size() <= EXPANDED_UP_TO);
            content.getChildren().add(pane);
        }

        if (prompt.conflicts() > 0) {
            content.getChildren().add(keepMine);
        }
        if (!prompt.warnings().isEmpty()) {
            Label title = new Label("These imports will break; fix them after the update:");
            title.getStyleClass().add("dialog-warning");
            content.getChildren().add(title);
            for (String warning : prompt.warnings()) {
                content.getChildren().add(wrapped(warning, "dialog-warning"));
            }
        }
        for (String note : prompt.notes()) {
            content.getChildren().add(wrapped(note, "dialog-note"));
        }

        ScrollPane scroll = new ScrollPane(content);
        scroll.setFitToWidth(true);
        scroll.setPrefViewportWidth(WIDTH + 32);
        scroll.setPrefViewportHeight(480);
        return scroll;
    }

    private static Label wrapped(String text, String styleClass) {
        Label label = new Label(text);
        label.setWrapText(true);
        label.setMaxWidth(WIDTH);
        if (styleClass != null) {
            label.getStyleClass().add(styleClass);
        }
        return label;
    }
}
