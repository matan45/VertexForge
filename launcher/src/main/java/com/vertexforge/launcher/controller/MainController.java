package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.CreateProjectDialog;
import com.vertexforge.launcher.dialog.CreateProjectForm;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.service.CreateOutcome;
import com.vertexforge.launcher.service.CreateRequest;
import com.vertexforge.launcher.service.CreationJob;
import com.vertexforge.launcher.service.LocationChecker.LocationCheck;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.ui.BannerBar;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.value.ChangeListener;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.css.PseudoClass;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.Label;
import javafx.scene.control.MenuItem;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.TableCell;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.Tooltip;
import javafx.scene.input.KeyCode;
import javafx.scene.input.MouseButton;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import javafx.stage.Window;

import java.net.URL;
import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.ResourceBundle;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Consumer;


/**
 * The main window. All project logic lives in {@link ProjectHub}; this class only turns it into UI.
 * It is created by {@code MainView}'s controller factory, never by FXMLLoader's default constructor lookup.
 * <p>
 * Threading (VK-1678): every list read and change runs on the single {@code background} executor, in order, and its
 * result comes back through {@link Platform#runLater}, so results are applied in the order they were asked for.
 * Status and location checks run on the hub's own threads, and project files are written on the single
 * {@code creation} executor (VK-1679). Everything in this class itself runs on the FX thread.
 */
public class MainController implements Initializable {

    static final String BANNER_READ_ONLY = "read-only";
    static final String BANNER_QUARANTINE = "quarantine";
    static final String BANNER_SAVE = "save";
    static final String BANNER_CREATE = "create";

    private static final PseudoClass MISSING_ROW = PseudoClass.getPseudoClass("missing");
    private static final PseudoClass UNREADABLE_ROW = PseudoClass.getPseudoClass("unreadable");
    private static final PseudoClass CHECKING_ROW = PseudoClass.getPseudoClass("checking");

    @FXML
    private TableView<ProjectViewModel> projectsTable;

    @FXML
    private TableColumn<ProjectViewModel, String> nameColumn;

    @FXML
    private TableColumn<ProjectViewModel, ProjectStatus> statusColumn;

    @FXML
    private TableColumn<ProjectViewModel, String> pathColumn;

    @FXML
    private TableColumn<ProjectViewModel, Instant> lastOpenedColumn;

    @FXML
    private VBox emptyStatePane;

    @FXML
    private VBox projectsTablePane;

    @FXML
    private VBox bannerBox;

    @FXML
    private Button openProjectButton;

    @FXML
    private Button createProjectButton;

    private final ObservableList<ProjectViewModel> projectsList = FXCollections.observableArrayList();
    private final ProjectHub hub;
    private final Executor background;
    private final Executor creation;
    private final CompletableFuture<Void> firstLoad = new CompletableFuture<>();

    /** An editor launch is in progress. */
    private final BooleanProperty launching = new SimpleBooleanProperty(false);
    /** A project creation is in progress, possibly after its dialog closed; a second one must wait. */
    private final BooleanProperty creating = new SimpleBooleanProperty(false);
    /** The creation in progress, or {@code null}. */
    private CreationJob activeJob;
    /** The list's file must not be written (newer, corrupt and unmovable, or unreadable). */
    private final BooleanProperty readOnly = new SimpleBooleanProperty(false);
    /** The selected row can be opened; follows the selected row's status. */
    private final BooleanProperty selectedOpenable = new SimpleBooleanProperty(false);
    private final ChangeListener<Boolean> openableListener = (observable, was, now) -> selectedOpenable.set(now);

    private BannerBar banners;

    public MainController(ProjectHub hub, Executor background, Executor creation) {
        this.hub = Objects.requireNonNull(hub, "hub");
        this.background = Objects.requireNonNull(background, "background");
        this.creation = Objects.requireNonNull(creation, "creation");
    }

    @Override
    public void initialize(URL location, ResourceBundle resources) {
        banners = new BannerBar(bannerBox);
        setupTableColumns();
        setupTableBehavior();
        setupSelectionListener();
        setupButtons();

        // Until the first read answers, show the (empty) table with a loading note rather than "no projects".
        projectsTable.setPlaceholder(new Label("Loading…"));
        showEmptyState(false);
        reload();
    }

    /** Completes once the first list read has been shown (the self-test waits for it). */
    public CompletableFuture<Void> firstLoad() {
        return firstLoad;
    }

    /** The projects table, for the self-test's checks of the packaged image. */
    public TableView<ProjectViewModel> projectsTable() {
        return projectsTable;
    }

    /** Asks a creation in progress to stop (the launcher is closing). FX thread. */
    public void cancelCreation() {
        if (activeJob != null) {
            activeJob.cancel();
        }
    }

    /** Whether a creation is in progress. FX thread. */
    public boolean creationRunning() {
        return activeJob != null;
    }

    private void setupTableColumns() {
        nameColumn.setCellValueFactory(cell -> cell.getValue().nameProperty());
        pathColumn.setCellValueFactory(cell -> cell.getValue().pathTextProperty());

        statusColumn.setCellValueFactory(cell -> cell.getValue().statusProperty());
        statusColumn.setComparator(Comparator.comparing(ProjectStatus::kind));
        statusColumn.setCellFactory(column -> new TableCell<>() {
            @Override
            protected void updateItem(ProjectStatus status, boolean empty) {
                super.updateItem(status, empty);
                if (empty || status == null) {
                    setText(null);
                    setTooltip(null);
                } else {
                    setText(status.label());
                    setTooltip(status.reason().isEmpty() ? null : new Tooltip(status.reason()));
                }
            }
        });

        lastOpenedColumn.setCellValueFactory(cell -> cell.getValue().lastOpenedProperty());
        // Sorts by the instant itself, so the order is chronological whatever the display format.
        lastOpenedColumn.setComparator(Comparator.nullsFirst(Comparator.<Instant>naturalOrder()));
        lastOpenedColumn.setCellFactory(column -> new TableCell<>() {
            @Override
            protected void updateItem(Instant instant, boolean empty) {
                super.updateItem(instant, empty);
                setText(empty ? null : ProjectViewModel.formatLastOpened(instant, hub.zone()));
            }
        });
    }

    private void setupTableBehavior() {
        projectsTable.setItems(projectsList);
        projectsTable.getSelectionModel().setSelectionMode(SelectionMode.SINGLE);
        projectsTable.disableProperty().bind(launching);

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
        removeItem.disableProperty().bind(readOnly);

        contextMenu.getItems().add(removeItem);

        projectsTable.setRowFactory(table -> {
            ProjectRow row = new ProjectRow();
            // Show context menu only on non-empty rows
            row.setOnContextMenuRequested(event -> {
                if (!row.isEmpty()) {
                    contextMenu.show(row, event.getScreenX(), event.getScreenY());
                }
            });
            return row;
        });
    }

    /** A row styled by its project's status; it follows the status as background checks answer. */
    private static final class ProjectRow extends TableRow<ProjectViewModel> {
        private final ChangeListener<ProjectStatus> statusListener = (observable, was, now) -> applyStatus(now);
        private ProjectViewModel watched;

        @Override
        protected void updateItem(ProjectViewModel item, boolean empty) {
            super.updateItem(item, empty);
            // A recycled row stops following its previous item.
            if (watched != null) {
                watched.statusProperty().removeListener(statusListener);
                watched = null;
            }
            if (empty || item == null) {
                applyStatus(null);
            } else {
                watched = item;
                item.statusProperty().addListener(statusListener);
                applyStatus(item.getStatus());
            }
        }

        private void applyStatus(ProjectStatus status) {
            ProjectStatus.Kind kind = status == null ? null : status.kind();
            pseudoClassStateChanged(MISSING_ROW, kind == ProjectStatus.Kind.MISSING);
            pseudoClassStateChanged(UNREADABLE_ROW, kind == ProjectStatus.Kind.UNREADABLE);
            pseudoClassStateChanged(CHECKING_ROW, kind == ProjectStatus.Kind.CHECKING);
        }
    }

    private void setupSelectionListener() {
        projectsTable.getSelectionModel().selectedItemProperty().addListener((observable, previous, selected) -> {
            if (previous != null) {
                previous.openableProperty().removeListener(openableListener);
            }
            if (selected != null) {
                selected.openableProperty().addListener(openableListener);
                selectedOpenable.set(selected.isOpenable());
            } else {
                selectedOpenable.set(false);
            }
        });
    }

    private void setupButtons() {
        // One binding each: a bound property must never be set directly.
        openProjectButton.disableProperty().bind(launching.or(selectedOpenable.not()));
        createProjectButton.disableProperty().bind(creating.or(readOnly));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Loading and showing the list
    // ---------------------------------------------------------------------------------------------------------------

    /** Reads the list again and re-checks every project (startup and Refresh). Never removes anything. */
    private void reload() {
        runInBackground(hub::listProjects, view -> applyView(view, true), error -> {
            // Nothing is known about the file, so nothing may write it until a Refresh reads it.
            readOnly.set(true);
            projectsTable.setPlaceholder(new Label("The recent-projects list could not be read"));
            banners.show(BANNER_READ_ONLY, BannerBar.Kind.ERROR,
                    "The recent-projects list could not be read: " + describe(error) + ". Press Refresh to try again.",
                    false);
            if (!firstLoad.isDone()) {
                firstLoad.complete(null);
            }
        });
    }

    /**
     * Shows a list read or written in the background. Rows whose entry keeps its id and path are reused, so their
     * status survives; new rows start as Checking and are checked. With {@code recheckAll}, every row is checked again.
     */
    private void applyView(RecentProjectsView view, boolean recheckAll) {
        readOnly.set(view.readOnly());
        showStoreBanners(view);

        UUID selectedId = Optional.ofNullable(projectsTable.getSelectionModel().getSelectedItem())
                .map(ProjectViewModel::getId)
                .orElse(null);

        Map<UUID, ProjectViewModel> existing = new HashMap<>();
        for (ProjectViewModel row : projectsList) {
            existing.put(row.getId(), row);
        }

        List<ProjectViewModel> rows = new ArrayList<>(view.entries().size());
        List<ProjectViewModel> toCheck = new ArrayList<>();
        for (RecentEntry entry : view.entries()) {
            ProjectViewModel row = existing.get(entry.id());
            if (row != null && row.matches(entry)) {
                row.update(entry);
                if (recheckAll) {
                    toCheck.add(row);
                }
            } else {
                row = new ProjectViewModel(entry);
                toCheck.add(row);
            }
            rows.add(row);
        }

        projectsList.setAll(rows);
        // Keeps a sort the user chose by clicking a column header; without one, the list's own order stays.
        projectsTable.sort();
        showEmptyState(rows.isEmpty());
        if (selectedId != null) {
            selectById(selectedId);
        }

        for (ProjectViewModel row : toCheck) {
            check(row);
        }
        if (!firstLoad.isDone()) {
            firstLoad.complete(null);
        }
    }

    /** Checks one row in the background; an answer only lands if that row is still listed and was not re-checked. */
    private void check(ProjectViewModel row) {
        long token = row.startProbe();
        hub.checkStatus(row.getEntry()).whenComplete((status, error) -> Platform.runLater(() -> {
            if (row.isCurrentProbe(token) && projectsList.contains(row)) {
                row.setStatus(error == null && status != null
                        ? status
                        : ProjectStatus.unreadable("Could not check the project file: " + describe(error)));
            }
        }));
    }

    private void showStoreBanners(RecentProjectsView view) {
        StoreStatus status = view.status();
        switch (status) {
            case NEWER_READ_ONLY -> banners.show(BANNER_READ_ONLY, BannerBar.Kind.INFO,
                    "This recent-projects list was saved by a newer version of the launcher, so it is read-only here: "
                            + "projects can be opened, but not created or removed.", false);
            case CORRUPT, CORRUPT_UNMOVABLE -> banners.show(BANNER_READ_ONLY, BannerBar.Kind.ERROR,
                    "The recent-projects list is damaged and could not be set aside (" + view.detail()
                            + "). It is read-only until the file is fixed or removed. Press Refresh to try again.",
                    false);
            case UNREADABLE -> banners.show(BANNER_READ_ONLY, BannerBar.Kind.ERROR,
                    "The recent-projects list could not be read (" + view.detail()
                            + "). It is read-only for now. Press Refresh to try again.", false);
            default -> banners.hide(BANNER_READ_ONLY);
        }
        view.quarantinedTo().ifPresent(this::showQuarantineBanner);
    }

    /** Stays until dismissed, even after later reads find the new list fine. */
    private void showQuarantineBanner(Path movedTo) {
        banners.show(BANNER_QUARANTINE, BannerBar.Kind.WARNING,
                "The recent-projects list was damaged. It was kept as " + movedTo.getFileName()
                        + " and a new list was started.", true);
    }

    private void showEmptyState(boolean show) {
        emptyStatePane.setVisible(show);
        emptyStatePane.setManaged(show);
        projectsTablePane.setVisible(!show);
        projectsTablePane.setManaged(!show);
    }

    private void selectById(UUID id) {
        for (ProjectViewModel row : projectsList) {
            if (row.getId().equals(id)) {
                projectsTable.getSelectionModel().select(row);
                return;
            }
        }
    }

    private void selectByPath(Path projectPath) {
        for (ProjectViewModel row : projectsList) {
            if (projectPath.equals(row.getProjectPath())) {
                projectsTable.getSelectionModel().select(row);
                projectsTable.scrollTo(row);
                return;
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Actions
    // ---------------------------------------------------------------------------------------------------------------

    private void removeSelectedProject() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || readOnly.get()) {
            return;
        }
        UUID id = selected.getId();
        runInBackground(() -> hub.removeProject(id), this::applySavedView, this::showSaveError);
    }

    private void openSelectedProject() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || !selected.isOpenable() || launching.get()) {
            return;
        }
        launchProject(selected.getProjectPath(), selected.getId());
    }

    /**
     * Opens {@code projectFile} in the editor, then marks the entry {@code id} as opened (when it is listed and the
     * list may be written) and minimizes the launcher.
     *
     * @param id the project's entry, or {@code null} to leave the list alone (a project created a moment ago, whose
     *           entry was stamped when it was added)
     */
    private void launchProject(Path projectFile, UUID id) {
        if (launching.get()) {
            return;
        }

        // Find editor executable
        var editorPathOpt = hub.findEditor();
        if (editorPathOpt.isEmpty()) {
            showErrorDialog("Editor Not Found",
                    "Could not find the editor executable.\n" +
                    "Set VERTEXFORGE_EDITOR_PATH environment variable or ensure " +
                    "the editor is built in the bin/ directory.");
            return;
        }

        // Disable UI to prevent double-clicks during launch
        launching.set(true);

        // Launch editor asynchronously on a background thread
        hub.launchEditor(editorPathOpt.get(), projectFile)
                .thenAccept(result -> Platform.runLater(() -> {
                    launching.set(false);
                    if (!result.success()) {
                        showErrorDialog("Launch Failed", result.errorMessage());
                        return;
                    }

                    // Update last opened timestamp (a read-only list is never written)
                    if (id != null && !readOnly.get()) {
                        runInBackground(() -> hub.markOpened(id), this::applySavedView, this::showSaveError);
                    }

                    // Minimize launcher after successful launch
                    Stage stage = (Stage) projectsTable.getScene().getWindow();
                    stage.setIconified(true);
                }))
                .exceptionally(throwable -> {
                    Platform.runLater(() -> {
                        launching.set(false);
                        showErrorDialog("Launch Failed",
                                "Unexpected error: " + throwable.getMessage());
                    });
                    return null;
                });
    }

    @FXML
    private void onRefreshProjects() {
        // Re-read and re-check; nothing is ever removed here (VK-1686 owns removing with Undo).
        reload();
    }

    @FXML
    private void onCreateProject() {
        if (creating.get() || readOnly.get()) {
            return;
        }
        Window owner = projectsTable.getScene().getWindow();

        // The dialog checks, creates and shows progress itself (VK-1679); it ends with the creation to follow up on.
        List<CreationJob> started = new ArrayList<>();
        Optional<CreateProjectForm.Result> result =
                CreateProjectDialog.show(owner, createServices(started), hub.findEditor().isPresent());
        if (result.isPresent()) {
            followCreation(result.get());
        } else if (!started.isEmpty()) {
            // The dialog closed without going through its form: still follow its last creation, never open it.
            followCreation(new CreateProjectForm.Result(started.getLast(), false));
        }
    }

    /** @param started collects every creation the dialog starts, in order */
    private CreateProjectForm.Services createServices(List<CreationJob> started) {
        return new CreateProjectForm.Services() {
            @Override
            public CompletableFuture<LocationCheck> checkLocation(Path location, String name) {
                return hub.checkLocation(location, name);
            }

            @Override
            public CreationJob startCreation(CreateRequest request, CreationJob.ProgressListener listener) {
                CreationJob job = MainController.this.startCreation(request, listener);
                started.add(job);
                return job;
            }
        };
    }

    /**
     * Starts a creation: its files on the creation thread, then its list add on the store thread. A second creation
     * must wait until this one has finished, even after its dialog closed (a cancel that is still cleaning up).
     */
    private CreationJob startCreation(CreateRequest request, CreationJob.ProgressListener listener) {
        CreationJob job = CreationJob.start(hub, request, creation, background, listener);
        activeJob = job;
        creating.set(true);
        job.outcome().whenComplete((outcome, error) -> Platform.runLater(() -> {
            if (activeJob == job) {
                activeJob = null;
                creating.set(false);
            }
        }));
        return job;
    }

    /** Follows up on how the dialog ended, once its creation (if any) has finished. */
    private void followCreation(CreateProjectForm.Result result) {
        CreationJob job = result.job();
        if (job == null) {
            return;
        }
        job.outcome().whenComplete((outcome, error) -> Platform.runLater(() -> {
            if (error != null || outcome == null) {
                // The outcome never completes exceptionally; this is only a safety net.
                banners.show(BANNER_CREATE, BannerBar.Kind.ERROR, "Project creation failed: " + describe(error), true);
                return;
            }
            onCreationFinished(job, outcome, result.openAfterCreate());
        }));
    }

    private void onCreationFinished(CreationJob job, CreateOutcome outcome, boolean openAfterCreate) {
        if (outcome.created()) {
            onProjectCreated(outcome);
            if (job.cancelRequested()) {
                // Cancel came too late: the project was already being moved into place. It is kept and listed (an
                // unlisted one already has its own banner).
                if (outcome.listed()) {
                    banners.show(BANNER_CREATE, BannerBar.Kind.INFO, "\"" + job.name()
                            + "\" was created before Cancel could stop it.", true);
                }
            } else if (openAfterCreate) {
                launchProject(outcome.projectPath(), null);
            }
            return;
        }

        // Only a creation the dialog stopped waiting for gets here (any other failure is shown in the dialog): report
        // what it left behind, or why it failed.
        String message = outcome.result().errorMessage();
        if (outcome.result().cancelled()) {
            if (message != null) {
                banners.show(BANNER_CREATE, BannerBar.Kind.WARNING, "Project creation was cancelled. " + message, true);
            }
        } else {
            banners.show(BANNER_CREATE, BannerBar.Kind.ERROR, "Project creation failed: "
                    + (message != null ? message : "unknown error"), true);
        }
    }

    private void onProjectCreated(CreateOutcome outcome) {
        if (!outcome.listed()) {
            banners.show(BANNER_CREATE, BannerBar.Kind.ERROR, "Project created at " + outcome.projectPath()
                    + ", but it could not be added to the list: " + outcome.listError().getMessage(), true);
            reload();
            return;
        }

        applySavedView(outcome.view());
        selectByPath(outcome.projectPath());
    }

    /** A change was saved: clear an old save error and show the list as written. */
    private void applySavedView(RecentProjectsView view) {
        banners.hide(BANNER_SAVE);
        applyView(view, false);
    }

    private void showSaveError(Throwable error) {
        banners.show(BANNER_SAVE, BannerBar.Kind.ERROR,
                "Could not save the recent-projects list: " + describe(error), true);
        // The file may have changed underneath (a newer launcher, a corrupt or locked file): show its real state.
        if (error instanceof StoreWriteException storeError) {
            storeError.quarantinedTo().ifPresent(this::showQuarantineBanner);
            reload();
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

    // ---------------------------------------------------------------------------------------------------------------
    // Background work
    // ---------------------------------------------------------------------------------------------------------------

    /** Runs {@code work} on the store thread and hands its result or failure to the FX thread. */
    private <T> void runInBackground(Callable<T> work, Consumer<T> onSuccess, Consumer<Throwable> onFailure) {
        try {
            CompletableFuture.supplyAsync(() -> {
                try {
                    return work.call();
                } catch (Exception e) {
                    throw new CompletionException(e);
                }
            }, background).whenComplete((value, error) -> Platform.runLater(() -> {
                if (error == null) {
                    onSuccess.accept(value);
                } else {
                    onFailure.accept(unwrap(error));
                }
            }));
        } catch (RejectedExecutionException e) {
            // The launcher is closing.
            onFailure.accept(e);
        }
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while ((current instanceof CompletionException || current instanceof ExecutionException)
                && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static String describe(Throwable error) {
        if (error == null) {
            return "unknown error";
        }
        Throwable cause = unwrap(error);
        return cause.getMessage() != null ? cause.getMessage() : cause.getClass().getSimpleName();
    }
}
