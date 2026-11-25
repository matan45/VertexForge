package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettings.AfterLaunch;
import javafx.beans.property.ReadOnlyStringProperty;
import javafx.beans.property.StringProperty;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.geometry.Rectangle2D;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.RadioButton;
import javafx.scene.control.ScrollPane;
import javafx.scene.control.TextField;
import javafx.scene.control.ToggleGroup;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.DirectoryChooser;
import javafx.stage.Screen;
import javafx.stage.Window;

import java.io.File;
import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * The launcher's settings (VK-1683, VK-1694): where new projects go, how many projects the list keeps, the default
 * engine, what happens once an editor has opened its project, and the editor's console and logs. Everything it does
 * is in {@link SettingsForm}; this class only lays it out.
 */
public final class SettingsDialog {

    /** What the window does for the dialog. */
    public interface Services {
        /** Opens the per-launch logs folder in Explorer (creating it first), off the FX thread. */
        void openLogsFolder();
    }

    private static final ButtonType SAVE = new ButtonType("Save", ButtonBar.ButtonData.OK_DONE);
    private static final double TEXT_WIDTH = 520;
    /** Room kept free around the dialog when it is fitted onto a small screen. */
    private static final double SCREEN_MARGIN = 40;

    private SettingsDialog() {
        // Shown through show()
    }

    /**
     * Asks for the settings; empty when the user cancelled.
     *
     * @param logsDir where the per-launch logs are kept, shown for reference
     */
    public static Optional<SettingsForm.Result> show(Window owner, SettingsForm form, Path logsDir,
                                                     Services services) {
        Objects.requireNonNull(form, "form");
        Objects.requireNonNull(services, "services");
        Dialog<SettingsForm.Result> dialog = DialogFactory.dialog(owner, "Settings");
        dialog.setHeaderText("Launcher settings");
        dialog.setResizable(true);

        DialogPane pane = DialogFactory.pane();
        pane.getStyleClass().add("settings-dialog");
        pane.getButtonTypes().addAll(SAVE, ButtonType.CANCEL);

        VBox content = new VBox(6,
                section("Projects"),
                projectsSection(form),
                section("Engines"),
                enginesSection(form),
                section("After a launch"),
                afterLaunchSection(form),
                section("Editor console and logs"),
                logsSection(form, logsDir, services));
        if (!form.canSave()) {
            Label readOnly = new Label("The settings file was saved by a newer version of the launcher, so this one"
                    + " cannot change it.");
            readOnly.setWrapText(true);
            readOnly.setMaxWidth(TEXT_WIDTH);
            readOnly.getStyleClass().add("validation-error");
            content.getChildren().add(readOnly);
        }
        content.setPadding(new Insets(4, 16, 16, 16));
        content.setFillWidth(true);

        // A scroll pane, so the dialog still fits a small screen (150 % or 200 % scaling on a laptop).
        ScrollPane scroll = new ScrollPane(content);
        scroll.setFitToWidth(true);
        scroll.setHbarPolicy(ScrollPane.ScrollBarPolicy.NEVER);
        scroll.getStyleClass().add("settings-scroll");
        pane.setContent(scroll);
        dialog.setDialogPane(pane);

        Node saveButton = pane.lookupButton(SAVE);
        saveButton.disableProperty().bind(form.saveEnabledProperty().not());

        dialog.setResultConverter(button -> button == SAVE ? form.result().orElse(null) : null);
        dialog.setOnShown(event -> fitOntoScreen(pane.getScene().getWindow()));
        return dialog.showAndWait();
    }

    private static Label section(String title) {
        Label label = new Label(title);
        label.getStyleClass().add("settings-section");
        // Its underline spans the dialog, not just the title.
        label.setMaxWidth(Double.MAX_VALUE);
        return label;
    }

    private static VBox projectsSection(SettingsForm form) {
        Label locationLabel = new Label("Default location for new projects");
        locationLabel.getStyleClass().add("form-label");
        TextField locationField = new TextField();
        locationField.setPromptText("Empty: Create Project starts with an empty location");
        locationField.textProperty().bindBidirectional(form.locationTextProperty());
        HBox.setHgrow(locationField, Priority.ALWAYS);
        Button browse = new Button("Browse…");
        browse.getStyleClass().add("browse-button");
        browse.setOnAction(event -> {
            DirectoryChooser chooser = new DirectoryChooser();
            chooser.setTitle("Default Location for New Projects");
            // No initial folder: finding one that still exists could block on an unreachable share.
            File folder = chooser.showDialog(browse.getScene().getWindow());
            if (folder != null) {
                form.locationTextProperty().set(folder.getAbsolutePath());
            }
        });
        HBox locationRow = new HBox(8, locationField, browse);
        locationRow.setAlignment(Pos.CENTER_LEFT);

        Label limitLabel = new Label("Projects the list keeps (" + LauncherSettings.MIN_RECENT_PROJECTS_LIMIT + " to "
                + LauncherSettings.MAX_RECENT_PROJECTS_LIMIT + ")");
        limitLabel.getStyleClass().add("form-label");
        TextField limitField = numberField(form.limitTextProperty());

        return new VBox(6, locationLabel, locationRow, problem(form.locationProblemProperty()),
                limitLabel, limitField, problem(form.limitProblemProperty()),
                note(form.trimNoticeProperty(), "dialog-warning"));
    }

    private static VBox enginesSection(SettingsForm form) {
        Label engineLabel = new Label("Default engine");
        engineLabel.getStyleClass().add("form-label");
        ComboBox<SettingsForm.EngineItem> engineBox = new ComboBox<>();
        engineBox.getItems().setAll(form.engineItems());
        engineBox.valueProperty().bindBidirectional(form.defaultEngineProperty());
        engineBox.setMaxWidth(Double.MAX_VALUE);
        engineBox.setDisable(!form.engineChoiceEnabled());
        Label engineNote = new Label(form.engineNote());
        engineNote.setWrapText(true);
        engineNote.setMaxWidth(TEXT_WIDTH);
        engineNote.getStyleClass().add("dialog-note");
        return new VBox(6, engineLabel, engineBox, engineNote);
    }

    private static VBox afterLaunchSection(SettingsForm form) {
        Label afterLabel = new Label("Once an editor has opened its project:");
        afterLabel.getStyleClass().add("form-label");
        ToggleGroup group = new ToggleGroup();
        VBox choices = new VBox(6);
        for (AfterLaunch value : AfterLaunch.values()) {
            RadioButton button = new RadioButton(value.label());
            button.setToggleGroup(group);
            button.setUserData(value);
            button.setSelected(value == form.afterLaunchProperty().get());
            choices.getChildren().add(button);
        }
        // Every value has a button; keep one selected even if the stored value was unknown.
        if (group.getSelectedToggle() == null) {
            group.selectToggle(group.getToggles().getFirst());
        }
        form.afterLaunchProperty().set((AfterLaunch) group.getSelectedToggle().getUserData());
        group.selectedToggleProperty().addListener((observable, was, now) -> {
            if (now != null) {
                form.afterLaunchProperty().set((AfterLaunch) now.getUserData());
            }
        });
        Label afterNote = new Label("When the editor fails, the launcher always comes back and shows why.");
        afterNote.setWrapText(true);
        afterNote.setMaxWidth(TEXT_WIDTH);
        afterNote.getStyleClass().add("dialog-note");
        return new VBox(6, afterLabel, choices, afterNote);
    }

    private static VBox logsSection(SettingsForm form, Path logsDir, Services services) {
        CheckBox consoleBox = new CheckBox("Show editor console");
        consoleBox.selectedProperty().bindBidirectional(form.showEditorConsoleProperty());
        Label consoleNote = new Label("The editor opens its own console window, as when it is started from Explorer;"
                + " closing that window closes the editor. Editors built before this option existed run without one.");
        consoleNote.setWrapText(true);
        consoleNote.setMaxWidth(TEXT_WIDTH);
        consoleNote.getStyleClass().add("dialog-note");

        Label retentionLabel = new Label("Launch logs to keep (" + LauncherSettings.MIN_LOG_RETENTION + " to "
                + LauncherSettings.MAX_LOG_RETENTION + ")");
        retentionLabel.getStyleClass().add("form-label");
        TextField retentionField = numberField(form.retentionTextProperty());
        Label logsNote = new Label();
        logsNote.setWrapText(true);
        logsNote.setMaxWidth(TEXT_WIDTH);
        logsNote.getStyleClass().add("dialog-note");
        logsNote.textProperty().bind(form.retentionTextProperty().map(text -> {
            boolean valid = SettingsForm.wholeNumber(text, LauncherSettings.MIN_LOG_RETENTION,
                    LauncherSettings.MAX_LOG_RETENTION).isPresent();
            return "Whether or not it has a console, the editor's output is kept in a launch log in " + logsDir
                    + (valid ? ": the newest " + text.strip() + " are kept." : ".")
                    + " A running editor's log is never deleted.";
        }));
        Button openLogs = new Button("Open Logs Folder");
        openLogs.getStyleClass().add("browse-button");
        openLogs.setOnAction(event -> services.openLogsFolder());

        return new VBox(6, consoleBox, consoleNote, retentionLabel, retentionField,
                problem(form.retentionProblemProperty()), logsNote, openLogs);
    }

    private static TextField numberField(StringProperty text) {
        TextField field = new TextField();
        field.textProperty().bindBidirectional(text);
        field.setPrefColumnCount(6);
        field.setMaxWidth(120);
        return field;
    }

    /** A problem line that only takes room while it has something to say. */
    private static Label problem(ReadOnlyStringProperty text) {
        return note(text, "validation-error");
    }

    private static Label note(ReadOnlyStringProperty text, String styleClass) {
        Label label = new Label();
        label.textProperty().bind(text);
        label.setWrapText(true);
        label.setMaxWidth(TEXT_WIDTH);
        label.getStyleClass().add(styleClass);
        label.visibleProperty().bind(text.isNotEmpty());
        label.managedProperty().bind(label.visibleProperty());
        return label;
    }

    /** Shrinks the dialog onto the screen it opened on when it is taller than that screen; the content then scrolls. */
    private static void fitOntoScreen(Window window) {
        if (window == null) {
            return;
        }
        List<Screen> screens = Screen.getScreensForRectangle(window.getX(), window.getY(),
                Math.max(1, window.getWidth()), Math.max(1, window.getHeight()));
        Rectangle2D area = (screens.isEmpty() ? Screen.getPrimary() : screens.getFirst()).getVisualBounds();
        double maxHeight = area.getHeight() - SCREEN_MARGIN;
        if (window.getHeight() > maxHeight) {
            window.setHeight(maxHeight);
        }
        if (window.getY() < area.getMinY()) {
            window.setY(area.getMinY());
        } else if (window.getY() + window.getHeight() > area.getMaxY()) {
            window.setY(Math.max(area.getMinY(), area.getMaxY() - window.getHeight()));
        }
    }
}
