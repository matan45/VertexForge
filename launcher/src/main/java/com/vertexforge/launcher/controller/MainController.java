package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.model.RecentProjects;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.cell.PropertyValueFactory;
import javafx.scene.input.KeyCode;
import javafx.scene.input.MouseButton;
import javafx.scene.layout.VBox;

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

        // Custom row factory for invalid project styling
        projectsTable.setRowFactory(tv -> new TableRow<>() {
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
        });
    }

    /**
     * Configures TableView behavior: selection mode, double-click, keyboard.
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
     * Will be implemented in VK-155.
     */
    @FXML
    private void onCreateProject() {
        // TODO: Implement in VK-155 (Create New Project Flow)
    }

    /**
     * Handles open selected project button click.
     */
    @FXML
    private void onOpenProject() {
        openSelectedProject();
    }
}
