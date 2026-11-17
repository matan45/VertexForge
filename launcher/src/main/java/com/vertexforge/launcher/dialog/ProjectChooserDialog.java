package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.project.PickedProject;
import javafx.geometry.Insets;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.ListCell;
import javafx.scene.control.ListView;
import javafx.scene.input.MouseButton;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.nio.file.Path;
import java.util.Optional;

/**
 * Asks which project to add when a folder holds several project files (VK-1685). Each row shows the project's name and
 * file, or why the file cannot be added; only a project that can be added may be chosen.
 */
public final class ProjectChooserDialog {

    private static final ButtonType ADD = new ButtonType("Add", ButtonBar.ButtonData.OK_DONE);

    private ProjectChooserDialog() {
        // Utility class - prevent instantiation
    }

    /** Shows the folder's project files; empty when the user cancelled. */
    public static Optional<PickedProject.Found> show(Window owner, PickedProject.Choose choose) {
        Dialog<PickedProject.Found> dialog = new Dialog<>();
        dialog.setTitle("Add Existing Project");
        dialog.setHeaderText("Which project do you want to add?");
        dialog.initOwner(owner);
        dialog.setResizable(true);

        DialogPane pane = new DialogPane();
        DialogStyle.apply(pane);
        pane.getButtonTypes().addAll(ADD, ButtonType.CANCEL);

        Label folderLabel = new Label(choose.folder() + " holds " + choose.candidates().size() + " project files.");
        folderLabel.setWrapText(true);
        folderLabel.setMaxWidth(640);

        ListView<PickedProject> list = new ListView<>();
        list.setPrefSize(640, 200);
        list.getItems().setAll(choose.candidates());
        list.setCellFactory(view -> new ListCell<>() {
            @Override
            protected void updateItem(PickedProject candidate, boolean empty) {
                super.updateItem(candidate, empty);
                setText(empty || candidate == null ? null : rowText(candidate));
            }
        });

        VBox content = new VBox(10, folderLabel, list);
        VBox.setVgrow(list, Priority.ALWAYS);
        content.setPadding(new Insets(16));
        pane.setContent(content);
        dialog.setDialogPane(pane);

        Button addButton = (Button) pane.lookupButton(ADD);
        addButton.disableProperty().bind(list.getSelectionModel().selectedItemProperty().map(
                selected -> !(selected instanceof PickedProject.Found)).orElse(true));

        // A double-click on a project that can be added chooses it.
        list.setOnMouseClicked(event -> {
            if (event.getButton() == MouseButton.PRIMARY && event.getClickCount() == 2
                    && list.getSelectionModel().getSelectedItem() instanceof PickedProject.Found) {
                addButton.fire();
            }
        });

        for (PickedProject candidate : choose.candidates()) {
            if (candidate instanceof PickedProject.Found) {
                list.getSelectionModel().select(candidate);
                break;
            }
        }

        dialog.setResultConverter(button -> button == ADD
                && list.getSelectionModel().getSelectedItem() instanceof PickedProject.Found found ? found : null);
        return dialog.showAndWait();
    }

    /** {@code projectName — file.vfproj}, or {@code file.vfproj — cannot be added: reason}. */
    static String rowText(PickedProject candidate) {
        return switch (candidate) {
            case PickedProject.Found found -> found.project().projectName() + "  —  " + fileName(found.file());
            case PickedProject.Refused refused -> fileName(refused.picked()) + "  —  cannot be added: "
                    + refused.reason();
            case PickedProject.Choose nested -> fileName(nested.folder());
        };
    }

    private static String fileName(Path path) {
        Path name = path.getFileName();
        return name == null ? path.toString() : name.toString();
    }
}
