package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.ui.MainView;
import javafx.animation.PauseTransition;
import javafx.application.Platform;
import javafx.event.ActionEvent;
import javafx.geometry.Insets;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressBar;
import javafx.scene.control.TextField;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.DirectoryChooser;
import javafx.stage.Window;
import javafx.stage.WindowEvent;

import java.io.File;
import java.io.IOException;
import java.nio.file.Path;
import java.util.Optional;

/**
 * The Create Project dialog (VK-1679). It only shows {@link CreateProjectForm}, which holds all of its behaviour:
 * nothing here touches the file system on the FX thread, so an unreachable folder can never freeze it.
 * <p>
 * Create does not close the dialog: the project is created while it stays open, a failure is shown in it, and Cancel
 * (or the close button) stops a creation in progress before the dialog closes.
 */
public class CreateProjectDialog {

    /** {@link CreateProjectForm.Timer}s on the FX thread's animation clock. */
    static final CreateProjectForm.Timers FX_TIMERS = (delay, action) -> {
        PauseTransition pause = new PauseTransition(javafx.util.Duration.millis(delay.toMillis()));
        pause.setOnFinished(event -> action.run());
        return new CreateProjectForm.Timer() {
            @Override
            public void restart() {
                pause.playFromStart();
            }

            @Override
            public void stop() {
                pause.stop();
            }
        };
    };

    private final Dialog<CreateProjectForm.Result> dialog;
    private final CreateProjectForm form;
    private final TextField projectNameField;
    private final TextField projectLocationField;
    private final CheckBox openAfterCreateBox;
    private final Label validationLabel;
    private final Label warningLabel;
    private final Label statusLabel;
    private final ProgressBar progressBar;
    private final Window owner;

    public CreateProjectDialog(Window owner, CreateProjectForm.Services services, boolean editorAvailable) {
        this.owner = owner;
        this.form = new CreateProjectForm(services, FX_TIMERS, Platform::runLater, editorAvailable);
        this.dialog = new Dialog<>();
        this.projectNameField = new TextField();
        this.projectLocationField = new TextField();
        this.openAfterCreateBox = new CheckBox("Open in the editor after creating");
        this.validationLabel = new Label();
        this.warningLabel = new Label();
        this.statusLabel = new Label();
        this.progressBar = new ProgressBar();

        configureDialog();
        DialogPane dialogPane = createDialogPane();
        dialog.setDialogPane(dialogPane);

        Button createButton = (Button) dialogPane.lookupButton(ButtonType.OK);
        createButton.setText("Create");
        Button cancelButton = (Button) dialogPane.lookupButton(ButtonType.CANCEL);
        bindToForm(createButton, cancelButton);

        // Focus on project name field when dialog opens
        Platform.runLater(projectNameField::requestFocus);
    }

    /**
     * Shows the dialog and waits until it closes.
     *
     * @return how it ended; empty only if it closed without going through the form
     */
    public static Optional<CreateProjectForm.Result> show(Window owner, CreateProjectForm.Services services,
                                                          boolean editorAvailable) {
        return new CreateProjectDialog(owner, services, editorAvailable).showAndWait();
    }

    public Optional<CreateProjectForm.Result> showAndWait() {
        return dialog.showAndWait();
    }

    private void configureDialog() {
        dialog.setTitle("Create New Project");
        dialog.setHeaderText("Create a new VertexForge project");
        dialog.initOwner(owner);
        dialog.setResizable(false);
    }

    private DialogPane createDialogPane() {
        DialogPane dialogPane = new DialogPane();
        dialogPane.getStyleClass().addAll(DialogStyle.LAUNCHER_DIALOG, "create-project-dialog");
        // A dialog is a window of its own: it does not get the main window's stylesheet unless it is given it.
        try {
            dialogPane.getStylesheets().add(MainView.stylesheet().toExternalForm());
        } catch (IOException e) {
            // The self-test fails a build without the stylesheet; the dialog still works unstyled.
        }
        dialogPane.setContent(createFormGrid());
        dialogPane.getButtonTypes().addAll(ButtonType.OK, ButtonType.CANCEL);
        return dialogPane;
    }

    private GridPane createFormGrid() {
        GridPane grid = new GridPane();
        grid.setHgap(10);
        grid.setVgap(12);
        grid.setPadding(new Insets(20));
        grid.setPrefWidth(520);

        // Project Name
        Label nameLabel = new Label("Project Name:");
        nameLabel.getStyleClass().add("form-label");
        projectNameField.setPromptText("Enter project name");
        projectNameField.setPrefWidth(350);
        grid.add(nameLabel, 0, 0);
        grid.add(projectNameField, 1, 0);

        // Project Location
        Label locationLabel = new Label("Location:");
        locationLabel.getStyleClass().add("form-label");
        projectLocationField.setPromptText("Select project location");
        projectLocationField.setPrefWidth(280);

        Button browseButton = new Button("Browse...");
        browseButton.getStyleClass().add("browse-button");
        browseButton.setOnAction(e -> onBrowse());
        browseButton.disableProperty().bind(form.inputsEnabledProperty().not());

        HBox locationBox = new HBox(10, projectLocationField, browseButton);
        HBox.setHgrow(projectLocationField, Priority.ALWAYS);
        grid.add(locationLabel, 0, 1);
        grid.add(locationBox, 1, 1);

        grid.add(openAfterCreateBox, 1, 2);

        // Messages: an error blocks Create, warnings never do, the status says what is going on.
        validationLabel.getStyleClass().add("validation-error");
        warningLabel.getStyleClass().add("validation-warning");
        statusLabel.getStyleClass().add("creation-status");
        progressBar.getStyleClass().add("creation-progress");
        progressBar.setMaxWidth(Double.MAX_VALUE);
        VBox messages = new VBox(8, validationLabel, warningLabel, progressBar, statusLabel);
        for (Label label : new Label[]{validationLabel, warningLabel, statusLabel}) {
            label.setWrapText(true);
            label.setMaxWidth(480);
            label.visibleProperty().bind(label.textProperty().isNotEmpty());
            label.managedProperty().bind(label.visibleProperty());
        }
        progressBar.visibleProperty().bind(form.busyProperty());
        progressBar.managedProperty().bind(progressBar.visibleProperty());
        grid.add(messages, 0, 3, 2, 1);

        return grid;
    }

    private void bindToForm(Button createButton, Button cancelButton) {
        projectNameField.textProperty().bindBidirectional(form.nameProperty());
        projectLocationField.textProperty().bindBidirectional(form.locationProperty());
        openAfterCreateBox.selectedProperty().bindBidirectional(form.openAfterCreateProperty());

        projectNameField.disableProperty().bind(form.inputsEnabledProperty().not());
        projectLocationField.disableProperty().bind(form.inputsEnabledProperty().not());
        openAfterCreateBox.disableProperty().bind(form.openAfterCreateEnabledProperty().not());
        createButton.disableProperty().bind(form.createEnabledProperty().not());
        cancelButton.disableProperty().bind(form.cancelEnabledProperty().not());

        validationLabel.textProperty().bind(form.errorProperty());
        warningLabel.textProperty().bind(form.warningsProperty());
        statusLabel.textProperty().bind(form.statusProperty());
        progressBar.progressProperty().bind(form.progressProperty());

        // Create starts the creation instead of closing the dialog; Cancel goes through the form, which waits for a
        // creation in progress to clean up. Consuming the event in a filter stops the dialog's own close handler.
        createButton.addEventFilter(ActionEvent.ACTION, event -> {
            event.consume();
            form.create();
        });
        cancelButton.addEventFilter(ActionEvent.ACTION, event -> {
            event.consume();
            form.cancel();
        });
        // The window's close button and Alt+F4 ask too. They arrive as a window close request, which the dialog's
        // own handler never consumes once it may close: the window would then hide in the middle of a creation. A
        // filter runs before that handler, so the form decides; the dialog only closes once the form has.
        dialog.setOnShowing(event -> dialog.getDialogPane().getScene().getWindow()
                .addEventFilter(WindowEvent.WINDOW_CLOSE_REQUEST, closeRequest -> {
                    if (form.getState() != CreateProjectForm.State.CLOSED) {
                        closeRequest.consume();
                        form.cancel();
                    }
                }));
        // Any other close (Dialog.close) goes through the form the same way.
        dialog.setOnCloseRequest(event -> {
            if (form.getState() != CreateProjectForm.State.CLOSED) {
                event.consume();
                form.cancel();
            }
        });
        // Until the form closes the converter returns null, and a null result never closes a dialog.
        dialog.setResultConverter(buttonType -> form.result());
        form.setOnClosed(result -> dialog.close());

        // Messages come and go; let the window follow its content's height.
        validationLabel.textProperty().addListener((observable, was, now) -> resizeToContent());
        warningLabel.textProperty().addListener((observable, was, now) -> resizeToContent());
        statusLabel.textProperty().addListener((observable, was, now) -> resizeToContent());
        progressBar.visibleProperty().addListener((observable, was, now) -> resizeToContent());
    }

    private void resizeToContent() {
        Platform.runLater(() -> {
            if (dialog.getDialogPane().getScene() != null && dialog.getDialogPane().getScene().getWindow() != null) {
                dialog.getDialogPane().getScene().getWindow().sizeToScene();
            }
        });
    }

    private void onBrowse() {
        DirectoryChooser chooser = new DirectoryChooser();
        chooser.setTitle("Select Project Location");

        // Only a folder the background check has just found: looking at the typed one here could block the window.
        form.readyLocation().map(Path::toFile).ifPresent(chooser::setInitialDirectory);

        File selectedDir = chooser.showDialog(dialog.getDialogPane().getScene().getWindow());
        if (selectedDir != null) {
            projectLocationField.setText(selectedDir.getAbsolutePath());
        }
    }
}
