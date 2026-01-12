package com.vertexforge.launcher.dialog;

import javafx.application.Platform;
import javafx.geometry.Insets;
import javafx.scene.control.*;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.stage.DirectoryChooser;
import javafx.stage.Window;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Optional;
import java.util.regex.Pattern;

/**
 * Dialog for creating a new VertexForge project.
 * <p>
 * Prompts the user for a project name and location,
 * with real-time validation of inputs.
 * </p>
 */
public class CreateProjectDialog {

    /**
     * Invalid characters for project names (Windows filesystem restrictions).
     */
    private static final Pattern INVALID_NAME_CHARS = Pattern.compile("[\\\\/:*?\"<>|]");

    private final Dialog<ProjectCreationParams> dialog;
    private final TextField projectNameField;
    private final TextField projectLocationField;
    private final Label validationLabel;
    private final Button createButton;
    private final Window owner;

    /**
     * Parameters for project creation.
     *
     * @param name     the project name
     * @param location the parent directory where the project will be created
     */
    public record ProjectCreationParams(String name, Path location) {
    }

    /**
     * Creates a new CreateProjectDialog.
     *
     * @param owner the owner window for the dialog
     */
    public CreateProjectDialog(Window owner) {
        this.owner = owner;
        this.dialog = new Dialog<>();
        this.projectNameField = new TextField();
        this.projectLocationField = new TextField();
        this.validationLabel = new Label();

        configureDialog();
        DialogPane dialogPane = createDialogPane();
        dialog.setDialogPane(dialogPane);

        // Get the Create button
        this.createButton = (Button) dialogPane.lookupButton(ButtonType.OK);
        createButton.setText("Create");
        createButton.setDisable(true);

        setupValidation();
        setupResultConverter();

        // Focus on project name field when dialog opens
        Platform.runLater(() -> projectNameField.requestFocus());
    }

    /**
     * Shows the dialog and returns the result.
     *
     * @param owner the owner window
     * @return the project creation parameters if the user clicked Create
     */
    public static Optional<ProjectCreationParams> show(Window owner) {
        CreateProjectDialog dialog = new CreateProjectDialog(owner);
        return dialog.showAndWait();
    }

    /**
     * Shows the dialog and waits for the result.
     *
     * @return the result
     */
    public Optional<ProjectCreationParams> showAndWait() {
        return dialog.showAndWait();
    }

    /**
     * Configures the dialog properties.
     */
    private void configureDialog() {
        dialog.setTitle("Create New Project");
        dialog.setHeaderText("Create a new VertexForge project");
        dialog.initOwner(owner);
        dialog.setResizable(false);
    }

    /**
     * Creates the dialog pane with form content.
     *
     * @return the configured dialog pane
     */
    private DialogPane createDialogPane() {
        DialogPane dialogPane = new DialogPane();
        dialogPane.getStyleClass().add("create-project-dialog");
        dialogPane.setContent(createFormGrid());
        dialogPane.getButtonTypes().addAll(ButtonType.OK, ButtonType.CANCEL);
        return dialogPane;
    }

    /**
     * Creates the form grid with input fields.
     *
     * @return the form grid
     */
    private GridPane createFormGrid() {
        GridPane grid = new GridPane();
        grid.setHgap(10);
        grid.setVgap(15);
        grid.setPadding(new Insets(20));
        grid.setPrefWidth(500);

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

        HBox locationBox = new HBox(10, projectLocationField, browseButton);
        HBox.setHgrow(projectLocationField, Priority.ALWAYS);
        grid.add(locationLabel, 0, 1);
        grid.add(locationBox, 1, 1);

        // Validation message
        validationLabel.getStyleClass().add("validation-error");
        validationLabel.setWrapText(true);
        validationLabel.setMaxWidth(400);
        grid.add(validationLabel, 0, 2, 2, 1);

        return grid;
    }

    /**
     * Sets up real-time validation on input fields.
     */
    private void setupValidation() {
        projectNameField.textProperty().addListener((obs, old, newVal) -> validateInputs());
        projectLocationField.textProperty().addListener((obs, old, newVal) -> validateInputs());
    }

    /**
     * Sets up the result converter for the dialog.
     */
    private void setupResultConverter() {
        dialog.setResultConverter(buttonType -> {
            if (buttonType == ButtonType.OK) {
                String name = projectNameField.getText().trim();
                Path location = Paths.get(projectLocationField.getText().trim());
                return new ProjectCreationParams(name, location);
            }
            return null;
        });
    }

    /**
     * Handles the Browse button click to open a directory chooser.
     */
    private void onBrowse() {
        DirectoryChooser chooser = new DirectoryChooser();
        chooser.setTitle("Select Project Location");

        // Set initial directory if current path is valid
        String currentPath = projectLocationField.getText().trim();
        if (!currentPath.isEmpty()) {
            File currentDir = new File(currentPath);
            if (currentDir.isDirectory()) {
                chooser.setInitialDirectory(currentDir);
            }
        }

        File selectedDir = chooser.showDialog(owner);
        if (selectedDir != null) {
            projectLocationField.setText(selectedDir.getAbsolutePath());
        }
    }

    /**
     * Validates the current inputs and updates the UI accordingly.
     */
    private void validateInputs() {
        ValidationResult result = validate();

        if (result.valid()) {
            validationLabel.setText("");
            createButton.setDisable(false);
        } else {
            validationLabel.setText(result.message());
            createButton.setDisable(true);
        }
    }

    /**
     * Performs validation on the current input values.
     *
     * @return the validation result
     */
    private ValidationResult validate() {
        String name = projectNameField.getText().trim();
        String location = projectLocationField.getText().trim();

        // Check project name
        if (name.isEmpty()) {
            return ValidationResult.failure("Project name is required");
        }

        if (INVALID_NAME_CHARS.matcher(name).find()) {
            return ValidationResult.failure("Project name contains invalid characters: \\ / : * ? \" < > |");
        }

        // Check location
        if (location.isEmpty()) {
            return ValidationResult.failure("Project location is required");
        }

        Path locationPath;
        try {
            locationPath = Paths.get(location);
        } catch (Exception e) {
            return ValidationResult.failure("Invalid location path");
        }

        if (!Files.exists(locationPath)) {
            return ValidationResult.failure("Selected location does not exist");
        }

        if (!Files.isDirectory(locationPath)) {
            return ValidationResult.failure("Selected location is not a directory");
        }

        if (!Files.isWritable(locationPath)) {
            return ValidationResult.failure("Cannot write to selected location");
        }

        // Check if project already exists
        Path projectPath = locationPath.resolve(name);
        if (Files.exists(projectPath)) {
            return ValidationResult.failure("A project with this name already exists at this location");
        }

        return ValidationResult.success();
    }

    /**
     * Result of input validation.
     */
    private record ValidationResult(boolean valid, String message) {
        static ValidationResult success() {
            return new ValidationResult(true, null);
        }

        static ValidationResult failure(String message) {
            return new ValidationResult(false, message);
        }
    }
}
