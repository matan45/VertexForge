package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.CreateProjectDialog;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.service.EditorLauncher;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.MenuItem;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.cell.PropertyValueFactory;
import javafx.scene.input.KeyCode;
import javafx.scene.input.MouseButton;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.nio.file.Path;
import java.util.Optional;

import java.net.URL;
import java.util.ResourceBundle;


public class MainController implements Initializable {

    @FXML
    private TableView<ProjectViewModel> projectsTable;

    @FXML
    private TableColumn<ProjectViewModel, String> nameColumn;

    @FXML
    private TableColumn<ProjectViewModel, String> pathColumn;

    @FXML
    private TableColumn<ProjectViewModel, String> lastOpenedColumn;

    @FXML
    private VBox emptyStatePane;

    @FXML
    private VBox projectsTablePane;

    @FXML
    private Button openProjectButton;

    private final ObservableList<ProjectViewModel> projectsList = FXCollections.observableArrayList();
    private final RecentProjectsRepository repository = new RecentProjectsRepository();
    private final EditorLauncher editorLauncher = new EditorLauncher();

    @Override
    public void initialize(URL location, ResourceBundle resources) {
        setupTableColumns();
        setupTableBehavior();
        setupSelectionListener();
        loadProjects();
    }

    private void setupTableColumns() {
        nameColumn.setCellValueFactory(new PropertyValueFactory<>("name"));
        pathColumn.setCellValueFactory(new PropertyValueFactory<>("path"));
        lastOpenedColumn.setCellValueFactory(new PropertyValueFactory<>("lastOpened"));
    }

    private void setupTableBehavior() {
        projectsTable.setItems(projectsList);
        projectsTable.getSelectionModel().setSelectionMode(SelectionMode.SINGLE);

        // Double-click to open project
        projectsTable.setOnMouseClicked(event -> {
            if (event.getButton() == MouseButton.PRIMARY && event.getClickCount() == 2) {
                openSelectedProject();
            }
        });

        // Enter key to open project
        projectsTable.setOnKeyPressed(event -> {
            if (event.getCode() == KeyCode.ENTER) {
                openSelectedProject();
            }
        });

        // Context menu for right-click actions
        setupContextMenu();
    }

    private void setupContextMenu() {
        ContextMenu contextMenu = new ContextMenu();

        MenuItem removeItem = new MenuItem("Remove from List");
        removeItem.setOnAction(e -> removeSelectedProject());

        contextMenu.getItems().add(removeItem);

        // Only show context menu when clicking on a row with data
        projectsTable.setRowFactory(tv -> {
            TableRow<ProjectViewModel> row = new TableRow<>() {
                @Override
                protected void updateItem(ProjectViewModel item, boolean empty) {
                    super.updateItem(item, empty);
                    if (empty || item == null) {
                        getStyleClass().remove("invalid-project-row");
                    } else if (!item.isValid()) {
                        if (!getStyleClass().contains("invalid-project-row")) {
                            getStyleClass().add("invalid-project-row");
                        }
                    } else {
                        getStyleClass().remove("invalid-project-row");
                    }
                }
            };

            // Show context menu only on non-empty rows
            row.setOnContextMenuRequested(event -> {
                if (!row.isEmpty()) {
                    contextMenu.show(row, event.getScreenX(), event.getScreenY());
                }
            });

            return row;
        });
    }

    private void removeSelectedProject() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null) {
            return;
        }

        // Remove from repository
        RecentProjects recentProjects = repository.load();
        recentProjects.remove(selected.getProjectPath());
        repository.save(recentProjects);

        // Refresh UI
        loadProjects();
    }

    private void setupSelectionListener() {
        projectsTable.getSelectionModel().selectedItemProperty().addListener(
                (obs, oldSelection, newSelection) -> {
                    boolean hasValidSelection = newSelection != null && newSelection.isValid();
                    openProjectButton.setDisable(!hasValidSelection);
                }
        );
    }

    private void loadProjects() {
        RecentProjects recentProjects = repository.load();
        projectsList.clear();

        if (recentProjects.isEmpty()) {
            showEmptyState(true);
        } else {
            showEmptyState(false);
            for (ProjectInfo project : recentProjects.getAll()) {
                projectsList.add(new ProjectViewModel(project));
            }
        }
    }

    private void showEmptyState(boolean show) {
        emptyStatePane.setVisible(show);
        emptyStatePane.setManaged(show);
        projectsTablePane.setVisible(!show);
        projectsTablePane.setManaged(!show);
    }

    private void openSelectedProject() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || !selected.isValid()) {
            return;
        }

        // Find editor executable
        var editorPathOpt = editorLauncher.findEditorExecutable();
        if (editorPathOpt.isEmpty()) {
            showErrorDialog("Editor Not Found",
                    "Could not find the editor executable.\n" +
                    "Set VERTEXFORGE_EDITOR_PATH environment variable or ensure " +
                    "the editor is built in the bin/ directory.");
            return;
        }

        // Launch editor with project
        EditorLauncher.LaunchResult result = editorLauncher.launch(editorPathOpt.get(), selected.getProjectPath());

        if (!result.success()) {
            showErrorDialog("Launch Failed", result.errorMessage());
            return;
        }

        // Update last opened timestamp
        RecentProjects recentProjects = repository.load();
        recentProjects.touch(selected.getProjectPath());
        repository.save(recentProjects);

        // Close launcher after successful launch
        projectsTable.getScene().getWindow().hide();
    }

    @FXML
    private void onRefreshProjects() {
        // Load fresh data from repository
        RecentProjects recentProjects = repository.load();

        // Count invalid projects
        int invalidCount = recentProjects.countInvalid();

        if (invalidCount > 0) {
            // Show confirmation dialog
            Alert alert = new Alert(Alert.AlertType.CONFIRMATION);
            alert.initOwner(projectsTable.getScene().getWindow());
            alert.setTitle("Invalid Projects Found");
            alert.setHeaderText(invalidCount + " project(s) have invalid paths");
            alert.setContentText(
                "The project files no longer exist or cannot be accessed.\n\n" +
                "Would you like to remove them from the list?"
            );

            ButtonType removeButton = new ButtonType("Remove Invalid");
            ButtonType keepButton = new ButtonType("Keep All");
            alert.getButtonTypes().setAll(removeButton, keepButton, ButtonType.CANCEL);

            Optional<ButtonType> result = alert.showAndWait();

            if (result.isPresent() && result.get() == removeButton) {
                recentProjects.removeInvalid();
                repository.save(recentProjects);
            }
        }

        // Refresh validity of displayed items and reload
        for (ProjectViewModel vm : projectsList) {
            vm.refreshValidity();
        }
        loadProjects();
    }

    @FXML
    private void onCreateProject() {
        Window owner = projectsTable.getScene().getWindow();

        // Show dialog and get user input
        Optional<CreateProjectDialog.ProjectCreationParams> params =
                CreateProjectDialog.show(owner);

        if (params.isEmpty()) {
            return; // User cancelled
        }

        // Create project
        ProjectCreator creator = new ProjectCreator();
        ProjectCreationResult result = creator.createProject(
                params.get().name(),
                params.get().location()
        );

        if (!result.success()) {
            showErrorDialog("Project Creation Failed",
                    "Failed to create project: " + result.errorMessage());
            return;
        }

        // Add to recent projects
        ProjectInfo newProject = ProjectInfo.create(
                params.get().name(),
                result.projectPath()
        );

        RecentProjects recentProjects = repository.load();
        recentProjects.add(newProject);
        repository.save(recentProjects);

        // Refresh UI
        loadProjects();

        // Select the new project
        selectProjectByPath(result.projectPath());
    }

    private void selectProjectByPath(Path projectPath) {
        for (ProjectViewModel vm : projectsList) {
            if (vm.getProjectPath().equals(projectPath)) {
                projectsTable.getSelectionModel().select(vm);
                projectsTable.scrollTo(vm);
                break;
            }
        }
    }

    private void showErrorDialog(String title, String message) {
        Alert alert = new Alert(Alert.AlertType.ERROR);
        alert.setTitle(title);
        alert.setHeaderText(null);
        alert.setContentText(message);
        alert.initOwner(projectsTable.getScene().getWindow());
        alert.showAndWait();
    }

    @FXML
    private void onOpenProject() {
        openSelectedProject();
    }
}
