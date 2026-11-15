package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettings.AfterLaunch;
import javafx.geometry.Insets;
import javafx.scene.Node;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.RadioButton;
import javafx.scene.control.ToggleGroup;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.nio.file.Path;
import java.util.EnumMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;

/**
 * The launcher's settings (VK-1683): what to do once an editor has opened its project, and whether the editor gets a
 * console window. VK-1694 adds the rest.
 */
public final class SettingsDialog {

    /** What the user chose. */
    public record Result(AfterLaunch afterLaunch, boolean showEditorConsole) {
        public Result {
            Objects.requireNonNull(afterLaunch, "afterLaunch");
        }
    }

    private static final ButtonType SAVE = new ButtonType("Save", ButtonBar.ButtonData.OK_DONE);

    private SettingsDialog() {
        // Shown through show()
    }

    /**
     * Asks for the settings; empty when the user cancelled.
     *
     * @param canSave  whether the settings file may be written; Save is disabled when it may not
     * @param logsDir  where the per-launch logs are kept, shown for reference
     */
    public static Optional<Result> show(Window owner, LauncherSettings current, boolean canSave, Path logsDir) {
        Dialog<Result> dialog = new Dialog<>();
        dialog.setTitle("Settings");
        dialog.setHeaderText("Launcher settings");
        dialog.initOwner(owner);

        DialogPane pane = new DialogPane();
        DialogStyle.apply(pane);
        pane.getButtonTypes().addAll(SAVE, ButtonType.CANCEL);

        Label afterLabel = new Label("Once an editor has opened its project:");
        afterLabel.getStyleClass().add("form-label");
        ToggleGroup group = new ToggleGroup();
        Map<AfterLaunch, RadioButton> choices = new EnumMap<>(AfterLaunch.class);
        VBox choiceBox = new VBox(6);
        for (AfterLaunch value : AfterLaunch.values()) {
            RadioButton button = new RadioButton(value.label());
            button.setToggleGroup(group);
            button.setUserData(value);
            button.setSelected(value == current.afterLaunch());
            choices.put(value, button);
            choiceBox.getChildren().add(button);
        }
        Label afterNote = new Label("When the editor fails, the launcher always comes back and shows why.");
        afterNote.setWrapText(true);
        afterNote.setMaxWidth(520);
        afterNote.getStyleClass().add("dialog-note");

        CheckBox consoleBox = new CheckBox("Show editor console");
        consoleBox.setSelected(current.showEditorConsole());
        Label consoleNote = new Label("The editor opens its own console window, as when it is started from Explorer;"
                + " closing that window closes the editor. Editors built before this option existed run without one."
                + " Either way the editor's output is kept in a launch log in " + logsDir + " (the last 20).");
        consoleNote.setWrapText(true);
        consoleNote.setMaxWidth(520);
        consoleNote.getStyleClass().add("dialog-note");

        VBox content = new VBox(10, afterLabel, choiceBox, afterNote, consoleBox, consoleNote);
        if (!canSave) {
            Label readOnly = new Label("The settings file cannot be written by this launcher.");
            readOnly.getStyleClass().add("validation-error");
            content.getChildren().add(readOnly);
        }
        content.setPadding(new Insets(16));
        pane.setContent(content);
        dialog.setDialogPane(pane);

        Node saveButton = pane.lookupButton(SAVE);
        saveButton.setDisable(!canSave);

        dialog.setResultConverter(button -> {
            if (button != SAVE || group.getSelectedToggle() == null) {
                return null;
            }
            return new Result((AfterLaunch) group.getSelectedToggle().getUserData(), consoleBox.isSelected());
        });
        // Every value has a button; keep one selected even if the stored value was unknown.
        if (group.getSelectedToggle() == null) {
            choices.get(LauncherSettings.DEFAULT_AFTER_LAUNCH).setSelected(true);
        }
        return dialog.showAndWait();
    }
}
