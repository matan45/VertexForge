package com.vertexforge.launcher.dialog;

import javafx.geometry.Insets;
import javafx.scene.Node;
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
import java.util.function.Function;

/**
 * Asks for the name of the folder "Open a copy" (VK-1690) or "Duplicate…" (VK-1695) makes beside the project's own.
 * The name is checked as it is typed with the same rules a new project's name has; whether a folder of that name
 * exists is checked by the copy itself, which asks again with the reason when it refuses.
 */
public final class CopyNameDialog {

    private CopyNameDialog() {
        // Shown through show()
    }

    /**
     * Asks for the name; empty when the user cancelled.
     *
     * @param check the problem with a name as typed, or empty when it may be used (pure: runs on every key)
     */
    public static Optional<String> show(Window owner, CopyNamePrompt prompt, Function<String, Optional<String>> check) {
        Objects.requireNonNull(prompt, "prompt");
        Objects.requireNonNull(check, "check");
        Dialog<String> dialog = DialogFactory.dialog(owner, prompt.purpose().title());
        dialog.setHeaderText(prompt.header());
        dialog.setResizable(true);

        ButtonType copy = new ButtonType(prompt.purpose().button(), ButtonBar.ButtonData.OK_DONE);
        DialogPane pane = DialogFactory.pane();
        pane.getStyleClass().add("copy-name-dialog");
        pane.getButtonTypes().addAll(copy, ButtonType.CANCEL);

        Label intro = new Label(prompt.intro());
        intro.setWrapText(true);
        intro.setMaxWidth(560);
        TextField name = new TextField(prompt.suggestedName());
        name.setPrefColumnCount(40);
        Label problem = new Label();
        problem.setWrapText(true);
        problem.setMaxWidth(560);
        problem.getStyleClass().add("dialog-note");
        problem.visibleProperty().bind(problem.textProperty().isNotEmpty());
        problem.managedProperty().bind(problem.visibleProperty());
        VBox content = new VBox(10, intro, name, problem);
        content.setPadding(new Insets(16));
        pane.setContent(content);
        dialog.setDialogPane(pane);

        Node copyButton = pane.lookupButton(copy);
        Runnable update = () -> {
            String text = name.getText();
            Optional<String> typed = check.apply(text);
            copyButton.setDisable(typed.isPresent());
            if (typed.isPresent()) {
                problem.setText(typed.get());
            } else if (prompt.problem() != null && text.equals(prompt.suggestedName())) {
                // The name the copy refused (it exists, the path is too long): say why until it is changed.
                problem.setText(prompt.problem());
            } else {
                problem.setText("");
            }
        };
        name.textProperty().addListener((observable, was, now) -> update.run());
        update.run();

        dialog.setOnShown(event -> {
            name.requestFocus();
            name.selectAll();
        });
        dialog.setResultConverter(type -> type == copy ? name.getText() : null);
        return dialog.showAndWait();
    }
}
