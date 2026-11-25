package com.vertexforge.launcher.dialog;

import javafx.geometry.Insets;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

/**
 * Asks what to do before a project opens with an engine that may upgrade it, or that the launcher cannot vouch for
 * (VK-1690): back up and open, open a copy, open anyway, or cancel. Shows a {@link VersionPrompt}; touches no file.
 */
public final class VersionDialog {

    private VersionDialog() {
        // Shown through show()
    }

    /** Asks the question and waits for the answer; {@link VersionPrompt.Choice#CANCEL} when the window is closed. */
    public static VersionPrompt.Choice show(Window owner, VersionPrompt prompt) {
        Objects.requireNonNull(prompt, "prompt");
        Dialog<VersionPrompt.Choice> dialog = DialogFactory.dialog(owner, prompt.title());
        dialog.setHeaderText(prompt.header());
        dialog.setResizable(true);

        DialogPane pane = DialogFactory.pane();
        pane.getStyleClass().add("version-dialog");
        // Insertion order is kept: buttons that share a ButtonData keep the order they were added in.
        Map<ButtonType, VersionPrompt.Choice> types = new LinkedHashMap<>();
        for (VersionPrompt.Choice choice : prompt.choices()) {
            types.put(choice == VersionPrompt.Choice.CANCEL ? ButtonType.CANCEL
                    : new ButtonType(choice.label(), ButtonBar.ButtonData.OTHER), choice);
        }
        pane.getButtonTypes().setAll(types.keySet());
        pane.setContent(content(prompt));
        dialog.setDialogPane(pane);

        types.forEach((type, choice) -> {
            Node node = pane.lookupButton(type);
            node.setDisable(!prompt.enabled(choice));
            if (node instanceof Button button) {
                button.setDefaultButton(choice == prompt.defaultChoice());
            }
        });
        // The window's close button answers Cancel: the pane has a cancel button.
        dialog.setResultConverter(type -> types.getOrDefault(type, VersionPrompt.Choice.CANCEL));
        return dialog.showAndWait().orElse(VersionPrompt.Choice.CANCEL);
    }

    private static VBox content(VersionPrompt prompt) {
        Label text = new Label(prompt.text());
        text.setWrapText(true);
        text.setMaxWidth(640);
        VBox content = new VBox(10, text);
        if (prompt.unavailable() != null) {
            Label unavailable = new Label(VersionPrompt.Choice.BACK_UP_AND_OPEN.label() + " and "
                    + VersionPrompt.Choice.OPEN_COPY.label() + " are not available: " + prompt.unavailable());
            unavailable.setWrapText(true);
            unavailable.setMaxWidth(640);
            unavailable.getStyleClass().add("dialog-note");
            content.getChildren().add(unavailable);
        }
        content.setPadding(new Insets(16));
        return content;
    }
}
