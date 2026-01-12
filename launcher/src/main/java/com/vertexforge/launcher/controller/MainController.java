package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.CreateProjectDialog;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
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

/**
 * Controller for the main launcher window (main.fxml).
 * <p>
 * Handles UI interactions and coordinates between the view and services.
 * Implements VK-154 (Projects List View).
 * </p>
 */
public class MainController implements Initializable {

    // FXML Injected Fields - TableView
    @FXML
    private TableView<ProjectViewModel> projectsTable;

    @FXML
    private TableColumn<ProjectViewModel, String> nameColumn;

    @FXML
    private TableColumn<ProjectViewModel, String> pathColumn;

    @FXML
    private TableColumn<ProjectViewModel, String> lastOpenedColumn;

    // FXML Injected Fields - Layout
    @FXML
    private VBox emptyStatePane;

    @FXML
    private VBox projectsTablePane;

    @FXML
    private Button openProjectButton;

    // Data
    private final ObservableList<ProjectViewModel> projectsList = FXCollections.observableArrayList();
    private final RecentProjectsRepository repository = new RecentProjectsRepository();

    /**
     * Initializes the controller after FXML loading is complete.
     *
     * @param location  the location used to resolve relative paths
     * @param resources the resources used to localize the root object
     */
    @Override
    public void initialize(URL location, ResourceBundle resources) {
        setupTableColumns();
        setupTableBehavior();
        setupSelectionListener();
        loadProjects();
    }

    /**
     * Configures the TableView columns with cell value factories.
     */
    private void setupTableColumns() {
        nameColumn.setCellValueFactory(new PropertyValueFactory<>("name"));
        pathColumn.setCellValueFactory(new PropertyValueFactory<>("path"));
        lastOpenedColumn.setCellValueFactory(new PropertyValueFactory<>("lastOpened"));
    }

    /**
     * Configures TableView behavior: selection mode, double-click, keyboard, context menu.
     */
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

    /**
     * Sets up the right-click context menu for the projects table.
     */
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

    /**
     * Removes the selected project from the recent projects list.
     * Does not delete files from disk.
     */
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

    /**
     * Sets up selection listener to enable/disable Open Project button.
     */
    private void setupSelectionListener() {
        projectsTable.getSelectionModel().selectedItemProperty().addListener(
                (obs, oldSelection, newSelection) -> {
                    boolean hasValidSelection = newSelection != null && newSelection.isValid();
                    openProjectButton.setDisable(!hasValidSelection);
                }
        );
    }

    /**
     * Loads projects from repository and populates the TableView.
     */
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

    /**
     * Toggles between empty state and projects table visibility.
     *
     * @param show true to show empty state, false to show table
     */
    private void showEmptyState(boolean show) {
        emptyStatePane.setVisible(show);
        emptyStatePane.setManaged(show);
        projectsTablePane.setVisible(!show);
        projectsTablePane.setManaged(!show);
    }

    /**
     * Opens the currently selected project.
     */
    private void openSelectedProject() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected != null && selected.isValid()) {
            // TODO: Implement in VK-156 (Engine Editor CLI Integration)
            System.out.println("Opening project: " + selected.getProjectPath());
        }
    }

    /**
     * Refreshes the projects list by reloading from repository.
     */
    @FXML
    private void onRefreshProjects() {
        // Refresh validity of existing items
        for (ProjectViewModel vm : projectsList) {
            vm.refreshValidity();
        }
        // Reload from repository
        loadProjects();
    }

    /**
     * Handles create new project action.
     * Opens a dialog for project creation and creates the project structure.
     */
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

    /**
     * Selects a project in the table by its path.
     *
     * @param projectPath the path to the .vfproj file
     */
    private void selectProjectByPath(Path projectPath) {
        for (ProjectViewModel vm : projectsList) {
            if (vm.getProjectPath().equals(projectPath)) {
                projectsTable.getSelectionModel().select(vm);
                projectsTable.scrollTo(vm);
                break;
            }
        }
    }

    /**
     * Shows an error dialog with the specified title and message.
     *
     * @param title   the dialog title
     * @param message the error message
     */
    private void showErrorDialog(String title, String message) {
        Alert alert = new Alert(Alert.AlertType.ERROR);
        alert.setTitle(title);
        alert.setHeaderText(null);
        alert.setContentText(message);
        alert.initOwner(projectsTable.getScene().getWindow());
        alert.showAndWait();
    }

    /**
     * Handles open selected project button click.
     */
    @FXML
    private void onOpenProject() {
        openSelectedProject();
    }
}
