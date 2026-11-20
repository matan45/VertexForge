package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.CreateProjectDialog;
import com.vertexforge.launcher.dialog.CreateProjectForm;
import com.vertexforge.launcher.dialog.EngineChooserDialog;
import com.vertexforge.launcher.dialog.EngineServices;
import com.vertexforge.launcher.dialog.EnginesDialog;
import com.vertexforge.launcher.dialog.McpDialog;
import com.vertexforge.launcher.dialog.ProjectChooserDialog;
import com.vertexforge.launcher.dialog.SessionFailureDialog;
import com.vertexforge.launcher.dialog.SettingsDialog;
import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineLabels;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryCodec;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.engine.PickedEngine;
import com.vertexforge.launcher.instance.InstanceMessage;
import com.vertexforge.launcher.instance.InstanceRequest;
import com.vertexforge.launcher.model.McpSetting;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.ProjectFiles;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.repository.RelinkedProject;
import com.vertexforge.launcher.repository.RemovedProject;
import com.vertexforge.launcher.service.CreateOutcome;
import com.vertexforge.launcher.service.CreateRequest;
import com.vertexforge.launcher.service.CreationJob;
import com.vertexforge.launcher.service.EditorExitCode;
import com.vertexforge.launcher.service.LocationChecker.LocationCheck;
import com.vertexforge.launcher.service.ProjectHealthService;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.template.ProjectBlueprint;
import com.vertexforge.launcher.template.TemplateCatalogs;
import com.vertexforge.launcher.session.EditorSession;
import com.vertexforge.launcher.session.EditorSessions;
import com.vertexforge.launcher.session.LaunchRequest;
import com.vertexforge.launcher.session.LaunchStatus;
import com.vertexforge.launcher.session.SessionOutcome;
import com.vertexforge.launcher.session.SessionReport;
import com.vertexforge.launcher.session.SessionStart;
import com.vertexforge.launcher.session.StartResult;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.ui.BannerBar;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import com.vertexforge.launcher.viewmodel.StatusDisplay;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.value.ChangeListener;
import javafx.beans.value.ObservableValue;
import javafx.collections.FXCollections;
import javafx.collections.ListChangeListener;
import javafx.collections.ObservableList;
import javafx.css.PseudoClass;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.Label;
import javafx.scene.control.MenuItem;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.SeparatorMenuItem;
import javafx.scene.control.SplitMenuButton;
import javafx.scene.control.TableCell;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.Tooltip;
import javafx.scene.input.Clipboard;
import javafx.scene.input.ClipboardContent;
import javafx.scene.input.DragEvent;
import javafx.scene.input.Dragboard;
import javafx.scene.input.KeyCode;
import javafx.scene.input.MouseButton;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.stage.DirectoryChooser;
import javafx.stage.FileChooser;
import javafx.stage.Modality;
import javafx.stage.Stage;
import javafx.stage.Window;

import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
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
 * {@code creation} executor (VK-1679). Every editor this window starts is watched by an {@link EditorSession} on the
 * hub's session thread, which reports back through {@link Platform#runLater} (VK-1683). Everything in this class
 * itself runs on the FX thread.
 * <p>
 * A row's status (VK-1686) comes from two answers that arrive in any order: its file's background check, kept on the
 * row, and the engines' check. {@link #applyHealth} works it out again whenever either changes, and whenever an editor
 * of this window starts or ends on a project of the row's folder.
 */
public class MainController implements Initializable {

    static final String BANNER_READ_ONLY = "read-only";
    static final String BANNER_QUARANTINE = "quarantine";
    static final String BANNER_SAVE = "save";
    static final String BANNER_CREATE = "create";
    static final String BANNER_ENGINES_READ_ONLY = "engines-read-only";
    static final String BANNER_ENGINES_QUARANTINE = "engines-quarantine";
    static final String BANNER_ENGINES = "engines";
    static final String BANNER_ENGINES_CHECK = "engines-check";
    static final String BANNER_OVERRIDE = "editor-override";
    static final String BANNER_SESSION_PREFIX = "session:";
    static final String BANNER_SETTINGS = "settings";
    static final String BANNER_ADD = "add-existing";
    static final String BANNER_COMMAND_LINE = "command-line";
    static final String BANNER_REMOVED = "removed";
    static final String BANNER_LOCATE = "locate";
    static final String BANNER_SHOW = "show-in-explorer";

    private static final PseudoClass DROP_TARGET = PseudoClass.getPseudoClass("drop-target");
    private static final PseudoClass MISSING_ROW = PseudoClass.getPseudoClass("missing");
    private static final PseudoClass UNREADABLE_ROW = PseudoClass.getPseudoClass("unreadable");
    private static final PseudoClass CHECKING_ROW = PseudoClass.getPseudoClass("checking");
    private static final PseudoClass INVALID_ROW = PseudoClass.getPseudoClass("invalid");
    /** Needs newer engine or Already open (VK-1686): the file is fine, but Open cannot use it right now. */
    private static final PseudoClass BLOCKED_ROW = PseudoClass.getPseudoClass("blocked");
    private static final PseudoClass RUNNING_ROW = PseudoClass.getPseudoClass("running");

    @FXML
    private TableView<ProjectViewModel> projectsTable;

    @FXML
    private TableColumn<ProjectViewModel, String> nameColumn;

    @FXML
    private TableColumn<ProjectViewModel, StatusDisplay> statusColumn;

    @FXML
    private TableColumn<ProjectViewModel, String> engineColumn;

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

    /** The whole window's content: the drop target (VK-1685). */
    @FXML
    private BorderPane rootPane;

    /** Outlined while files are dragged over the window. */
    @FXML
    private StackPane contentArea;

    @FXML
    private Button openProjectButton;

    @FXML
    private Button createProjectButton;

    @FXML
    private SplitMenuButton addExistingButton;

    @FXML
    private Button enginesButton;

    @FXML
    private Button settingsButton;

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
    /** Change Engine… is checking the engines or showing its chooser (VK-1681). */
    private final BooleanProperty choosingEngine = new SimpleBooleanProperty(false);
    /** The selected row can be opened; follows the selected row's status. */
    private final BooleanProperty selectedOpenable = new SimpleBooleanProperty(false);
    private final ChangeListener<Boolean> openableListener = (observable, was, now) -> selectedOpenable.set(now);

    /** The engines as last checked (VK-1681), or {@code null} before the first check answered. */
    private EngineOptions engineOptions;
    /** Only the latest engine check updates the window. */
    private long engineToken;
    private final EngineServices engineServices = new ControllerEngineServices();

    /** The editors this window started that still run, one per project (VK-1683). */
    private final EditorSessions sessions = new EditorSessions();

    /** Adds and opens projects that exist on disk, one request at a time (VK-1685). */
    private final AddExistingFlow addFlow = new AddExistingFlow(new FlowServices(), MainController::runLaterQuietly);

    /** A Locate… is checking a picked file or saving the relink (VK-1686); a second one must wait. */
    private final BooleanProperty locating = new SimpleBooleanProperty(false);
    /** The latest Remove from List, which the "removed" banner's Undo puts back; {@code null} once it is final. */
    private RemovedProject.Removal lastRemoval;

    /** Limits the re-checks when the window gets the focus back (VK-1686). */
    private final RecheckThrottle focusThrottle;
    /** A read of the list is in flight (startup, Refresh or a focus re-check). */
    private boolean reloadInFlight;
    /** The main window's focus, followed once the table is in a window; kept so the listener stays registered. */
    private ObservableValue<Boolean> windowFocused;

    /** Runs right before the launcher exits by itself (the Close after-launch setting); never {@code null}. */
    private Runnable beforeExit = () -> {
    };

    private BannerBar banners;

    public MainController(ProjectHub hub, Executor background, Executor creation) {
        this.hub = Objects.requireNonNull(hub, "hub");
        this.background = Objects.requireNonNull(background, "background");
        this.creation = Objects.requireNonNull(creation, "creation");
        this.focusThrottle = new RecheckThrottle(hub.clock(), RecheckThrottle.DEFAULT_GAP);
    }

    @Override
    public void initialize(URL location, ResourceBundle resources) {
        banners = new BannerBar(bannerBox);
        setupTableColumns();
        setupTableBehavior();
        setupSelectionListener();
        setupButtons();
        setupDropTarget();
        setupRequestQueue();
        setupFocusRecheck();

        // Until the first read answers, show the (empty) table with a loading note rather than "no projects".
        projectsTable.setPlaceholder(new Label("Loading…"));
        showEmptyState(false);
        reload();
    }

    /**
     * Requests from this launcher's command line, or forwarded by a second launcher (VK-1685): the window comes to
     * the front, and the requests are queued behind any that are waiting. An empty message only brings the window
     * up. FX thread.
     */
    public void handleRequests(InstanceMessage message) {
        Objects.requireNonNull(message, "message");
        if (windowOrNull() != null) {
            restoreWindow();
        }
        if (!message.problems().isEmpty()) {
            banners.show(BANNER_COMMAND_LINE, BannerBar.Kind.WARNING, "Ignored on the command line: "
                    + String.join("; ", message.problems()) + ".", true);
        }
        addFlow.submit(message.requests().stream()
                .map(request -> new AddExistingFlow.Item(request.path(),
                        request.action() == InstanceRequest.Action.OPEN))
                .toList());
    }

    /**
     * Runs {@code hook} right before the launcher exits by itself (the Close after-launch setting), so that a second
     * launcher is no longer told its request was taken (VK-1685). FX thread.
     */
    public void setBeforeExit(Runnable hook) {
        beforeExit = Objects.requireNonNull(hook, "hook");
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
        engineColumn.setCellValueFactory(cell -> cell.getValue().engineTextProperty());
        pathColumn.setCellValueFactory(cell -> cell.getValue().pathTextProperty());

        // The file's status, or the phase of an editor this window started on the project (VK-1683).
        statusColumn.setCellValueFactory(cell -> cell.getValue().statusDisplayProperty());
        statusColumn.setComparator(StatusDisplay.ORDER);
        statusColumn.setCellFactory(column -> new TableCell<>() {
            @Override
            protected void updateItem(StatusDisplay display, boolean empty) {
                super.updateItem(display, empty);
                if (empty || display == null) {
                    setText(null);
                    setTooltip(null);
                } else {
                    setText(display.label());
                    setTooltip(display.tooltip().isEmpty() ? null : new Tooltip(display.tooltip()));
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

        // VK-1686: where the project is, and fixing an entry whose project moved.
        MenuItem showItem = new MenuItem("Show in Explorer");
        showItem.setOnAction(e -> showSelectedInExplorer());
        MenuItem copyPathItem = new MenuItem("Copy Path");
        copyPathItem.setOnAction(e -> copySelectedPath());
        MenuItem locateItem = new MenuItem("Locate…");
        locateItem.setOnAction(e -> locateSelected());

        MenuItem changeEngineItem = new MenuItem("Change Engine…");
        changeEngineItem.setOnAction(e -> changeEngineOfSelected());
        changeEngineItem.disableProperty().bind(readOnly.or(launching).or(choosingEngine));

        // VK-1683: "Open with MCP" and its port, stored on the entry.
        MenuItem mcpItem = new MenuItem("MCP…");
        mcpItem.setOnAction(e -> editMcpOfSelected());
        mcpItem.disableProperty().bind(readOnly.or(launching).or(choosingEngine));

        MenuItem removeItem = new MenuItem("Remove from List");
        removeItem.setOnAction(e -> removeSelectedProject());
        removeItem.disableProperty().bind(readOnly);

        contextMenu.getItems().addAll(showItem, copyPathItem, new SeparatorMenuItem(), locateItem, changeEngineItem,
                mcpItem, new SeparatorMenuItem(), removeItem);
        // These depend on the row's status, which changes in the background; each handler checks again.
        contextMenu.setOnShowing(event -> {
            ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
            showItem.setDisable(!canShowInExplorer(selected));
            copyPathItem.setDisable(selected == null || isBlank(selected.getEntry().rawPath()));
            locateItem.setDisable(!canLocate(selected));
        });

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

    /**
     * A row styled by its project's status; it follows the status as background checks answer, and the editor this
     * window started on the project (VK-1683).
     */
    private static final class ProjectRow extends TableRow<ProjectViewModel> {
        private final ChangeListener<StatusDisplay> displayListener = (observable, was, now) -> applyStatus(now);
        private ProjectViewModel watched;

        @Override
        protected void updateItem(ProjectViewModel item, boolean empty) {
            super.updateItem(item, empty);
            // A recycled row stops following its previous item.
            if (watched != null) {
                watched.statusDisplayProperty().removeListener(displayListener);
                watched = null;
            }
            if (empty || item == null) {
                applyStatus(null);
            } else {
                watched = item;
                item.statusDisplayProperty().addListener(displayListener);
                applyStatus(item.getStatusDisplay());
            }
        }

        private void applyStatus(StatusDisplay display) {
            boolean running = display != null && display.running();
            ProjectStatus.Kind kind = display == null || running ? null : display.status().kind();
            pseudoClassStateChanged(MISSING_ROW, kind == ProjectStatus.Kind.MISSING);
            pseudoClassStateChanged(UNREADABLE_ROW, kind == ProjectStatus.Kind.UNREADABLE);
            pseudoClassStateChanged(CHECKING_ROW, kind == ProjectStatus.Kind.CHECKING);
            pseudoClassStateChanged(INVALID_ROW, kind == ProjectStatus.Kind.INVALID);
            pseudoClassStateChanged(BLOCKED_ROW, kind == ProjectStatus.Kind.ALREADY_OPEN
                    || kind == ProjectStatus.Kind.NEEDS_NEWER_ENGINE);
            pseudoClassStateChanged(RUNNING_ROW, running);
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
        openProjectButton.disableProperty().bind(launching.or(choosingEngine).or(selectedOpenable.not()));
        enginesButton.disableProperty().bind(launching.or(choosingEngine));
        settingsButton.disableProperty().bind(launching.or(choosingEngine));
        createProjectButton.disableProperty().bind(creating.or(readOnly));
        addExistingButton.disableProperty().bind(readOnly.or(launching).or(choosingEngine)
                .or(addFlow.busyProperty()));
    }

    /**
     * Files and folders dragged from Explorer onto any part of the window are added (VK-1685). Filters on the root see
     * the drag even over a disabled table. Nothing here touches the disk: Explorer waits until the drop returns.
     */
    private void setupDropTarget() {
        rootPane.addEventFilter(DragEvent.DRAG_OVER, event -> {
            if (event.getDragboard().hasFiles()) {
                // Never MOVE: the source would then delete what it dragged.
                event.acceptTransferModes(TransferMode.COPY);
            }
        });
        rootPane.addEventFilter(DragEvent.DRAG_DROPPED, event -> {
            Dragboard board = event.getDragboard();
            if (!board.hasFiles()) {
                return;
            }
            List<Path> dropped = toPaths(board.getFiles());
            event.setDropCompleted(true);
            event.consume();
            contentArea.pseudoClassStateChanged(DROP_TARGET, false);
            addProjects(dropped);
        });
        rootPane.addEventHandler(DragEvent.DRAG_ENTERED, event ->
                contentArea.pseudoClassStateChanged(DROP_TARGET, event.getDragboard().hasFiles()));
        rootPane.addEventHandler(DragEvent.DRAG_EXITED, event ->
                contentArea.pseudoClassStateChanged(DROP_TARGET, false));
    }

    /**
     * Waiting requests run once the window is free: after the first list read, a launch, an engine choice, or once a
     * dialog closes (a closing window leaves {@link Window#getWindows()}).
     */
    private void setupRequestQueue() {
        ChangeListener<Boolean> freed = (observable, was, now) -> {
            if (!now) {
                drainLater();
            }
        };
        launching.addListener(freed);
        choosingEngine.addListener(freed);
        Window.getWindows().addListener((ListChangeListener<Window>) change -> drainLater());
        firstLoad.thenRun(this::drainLater);
    }

    private void drainLater() {
        runLaterQuietly(addFlow::drain);
    }

    /**
     * The project files are read again when the window gets the focus back (VK-1686), at most once per
     * {@link RecheckThrottle#DEFAULT_GAP}: an editor may have been opened or closed, or a project moved, meanwhile.
     */
    private void setupFocusRecheck() {
        windowFocused = projectsTable.sceneProperty()
                .flatMap(Scene::windowProperty)
                .flatMap(Window::focusedProperty);
        windowFocused.addListener((observable, was, now) -> {
            if (Boolean.TRUE.equals(now)) {
                onWindowFocused();
            }
        });
    }

    private void onWindowFocused() {
        // Not before the first read, and not while a read of the list is in flight.
        if (!firstLoad.isDone() || reloadInFlight) {
            return;
        }
        // Not while a launch or an engine choice uses the engines, and at most once per throttle gap.
        if (!launching.get() && !choosingEngine.get() && focusThrottle.dueOnFocus()) {
            reload();
            return;
        }
        // Between those, the rows another editor had open are checked again at once: closing that editor and coming
        // back to the launcher is the common case, and their Open must not wait for the gap.
        for (ProjectViewModel row : projectsList) {
            if (row.getStatus().kind() == ProjectStatus.Kind.ALREADY_OPEN) {
                check(row);
            }
        }
    }

    private static List<Path> toPaths(List<File> files) {
        List<Path> paths = new ArrayList<>(files.size());
        for (File file : files) {
            try {
                paths.add(file.toPath().toAbsolutePath().normalize());
            } catch (InvalidPathException e) {
                // Explorer never hands over such a name; skip it rather than fail the whole drop.
            }
        }
        return paths;
    }

    /** Queues paths picked or dropped by the user for adding. */
    private void addProjects(List<Path> paths) {
        addFlow.submit(paths.stream().map(path -> new AddExistingFlow.Item(path, false)).toList());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Loading and showing the list
    // ---------------------------------------------------------------------------------------------------------------

    /**
     * Reads the list again and re-checks every project and engine (startup, Refresh and the focus re-check). Never
     * removes anything.
     */
    private void reload() {
        focusThrottle.checked();
        reloadInFlight = true;
        reloadEngines();
        runInBackground(hub::listProjects, view -> {
            reloadInFlight = false;
            applyView(view, true);
        }, error -> {
            reloadInFlight = false;
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
            // The entry's binding may have changed (a first open binds it), and with it the engine it opens with.
            applyEngineLabel(row);
            applyHealth(row);
            // A rebuilt row still shows the editor running on its project (VK-1683).
            row.setSessionPhase(sessions.find(row.getProjectPath()).map(EditorSessions.Live::phase).orElse(null));
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

    /**
     * Checks one row's file in the background; an answer only lands if that row is still listed and was not re-checked.
     * The folder's editor lock is not probed while an editor of this window has a project of that folder open: that
     * lock is its own (VK-1686).
     */
    private void check(ProjectViewModel row) {
        long token = row.startProbe();
        boolean probeLock = folderEditor(row) == FolderEditor.NONE;
        hub.checkProject(row.getEntry(), probeLock).whenComplete((result, error) -> Platform.runLater(() -> {
            if (row.isCurrentProbe(token) && projectsList.contains(row)) {
                row.setFileCheck(error == null && result != null
                        ? result
                        : new ProjectFileCheck.Unreadable("Could not check the project file: " + describe(error)));
                applyHealth(row);
            }
        }));
    }

    /** Works out the row's status from its file's last answer and the engines as last checked (VK-1686). */
    private void applyHealth(ProjectViewModel row) {
        ProjectFileCheck fileCheck = row.getFileCheck();
        if (fileCheck != null) {
            row.setStatus(ProjectHealthService.evaluate(fileCheck, engineOptions, row.getEntry().engineId(),
                    folderEditor(row)));
        }
    }

    /** Which editor of this window has a project of the row's folder open. */
    private FolderEditor folderEditor(ProjectViewModel row) {
        Path project = row.getProjectPath();
        if (sessions.find(project).isPresent()) {
            return FolderEditor.THIS_PROJECT;
        }
        return sessions.findInFolder(project).isPresent() ? FolderEditor.OTHER_PROJECT : FolderEditor.NONE;
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

    /**
     * Removes the selected entry from the list (never the project on disk) and offers to undo it (VK-1686). Only the
     * latest removal can be undone: a new one replaces the banner and makes the earlier one final.
     */
    private void removeSelectedProject() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || readOnly.get()) {
            return;
        }
        UUID id = selected.getId();
        runInBackground(() -> hub.removeProject(id), removed -> {
            applySavedView(removed.view());
            removed.removal().ifPresent(this::offerUndo);
        }, this::showSaveError);
    }

    private void offerUndo(RemovedProject.Removal removal) {
        lastRemoval = removal;
        banners.show(BANNER_REMOVED, BannerBar.Kind.INFO, "\"" + removal.entry().displayName()
                        + "\" was removed from the list. Its files were not touched.", true, "Undo",
                () -> undoRemoval(removal));
    }

    /** Puts the removed entry back as it was: its id, pin, time, engine, MCP setting and position. */
    private void undoRemoval(RemovedProject.Removal removal) {
        if (removal != lastRemoval) {
            return;
        }
        String name = removal.entry().displayName();
        lastRemoval = null;
        if (readOnly.get()) {
            banners.show(BANNER_REMOVED, BannerBar.Kind.WARNING, "The recent-projects list is read-only now, so \""
                    + name + "\" cannot be put back. Add it again with Add Existing… once the list can be changed.",
                    true);
            return;
        }
        banners.hide(BANNER_REMOVED);
        UUID id = removal.entry().id();
        runInBackground(() -> hub.restoreProject(removal), view -> {
            applySavedView(view);
            if (view.find(id).isPresent()) {
                selectById(id);
                projectsTable.scrollTo(projectsTable.getSelectionModel().getSelectedItem());
            } else {
                banners.show(BANNER_REMOVED, BannerBar.Kind.INFO, "\"" + name + "\" is already in the list again,"
                        + " so it was not put back a second time.", true);
            }
        }, error -> {
            showSaveError(error);
            // The removal still stands; Undo stays available, unless a later removal has taken the banner meanwhile.
            if (lastRemoval == null) {
                offerUndo(removal);
            }
        });
    }

    /** Whether Show in Explorer can select the row's project file. */
    private static boolean canShowInExplorer(ProjectViewModel row) {
        return row != null && row.getProjectPath() != null && row.getStatus().kind() != ProjectStatus.Kind.MISSING;
    }

    /** Opens Explorer on the selected project's folder with its file selected; off the FX thread. */
    private void showSelectedInExplorer() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (!canShowInExplorer(selected)) {
            return;
        }
        Path file = selected.getProjectPath();
        CompletableFuture.runAsync(() -> {
            try {
                hub.shell().showInFolder(file);
            } catch (IOException e) {
                throw new CompletionException(e);
            }
        }).whenComplete((ignored, error) -> {
            if (error != null) {
                runLaterQuietly(() -> banners.show(BANNER_SHOW, BannerBar.Kind.ERROR,
                        "Could not show " + file + " in Explorer: " + describe(error), true));
            }
        });
    }

    /** Puts the selected entry's path, as stored and shown, on the clipboard. */
    private void copySelectedPath() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || isBlank(selected.getEntry().rawPath())) {
            return;
        }
        ClipboardContent content = new ClipboardContent();
        content.putString(selected.getEntry().rawPath());
        Clipboard.getSystemClipboard().setContent(content);
    }

    private static boolean isBlank(String text) {
        return text == null || text.isBlank();
    }

    /**
     * Whether Locate… may relink the row (VK-1686): its project cannot be found or used where the entry says (or the
     * entry has no usable path), no editor of this window has it open, and the list may be changed right now.
     */
    private boolean canLocate(ProjectViewModel row) {
        if (row == null || readOnly.get() || launching.get() || choosingEngine.get() || locating.get()
                || addFlow.isBusy() || row.getStatusDisplay().running()) {
            return false;
        }
        if (row.getProjectPath() == null) {
            return true;
        }
        ProjectStatus.Kind kind = row.getStatus().kind();
        return kind == ProjectStatus.Kind.MISSING || kind == ProjectStatus.Kind.INVALID
                || kind == ProjectStatus.Kind.UNREADABLE;
    }

    /**
     * "Locate…": the user picks the project file where it is now; it is read in the background, then the entry is
     * pointed at it on the store thread, keeping its id, pin, time, engine binding and MCP setting (VK-1686).
     */
    private void locateSelected() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (!canLocate(selected)) {
            return;
        }
        FileChooser chooser = new FileChooser();
        chooser.setTitle("Locate \"" + selected.getName() + "\"");
        // No initial folder: finding one that still exists could block on an unreachable share.
        chooser.getExtensionFilters().addAll(
                new FileChooser.ExtensionFilter("VertexForge projects (*" + ProjectFiles.EXTENSION + ")",
                        "*" + ProjectFiles.EXTENSION),
                new FileChooser.ExtensionFilter("All files", "*.*"));
        File file = chooser.showOpenDialog(window());
        if (file == null) {
            return;
        }
        List<Path> picked = toPaths(List.of(file));
        if (picked.isEmpty()) {
            return;
        }
        UUID id = selected.getId();
        locating.set(true);
        hub.inspectProject(picked.getFirst()).whenComplete((result, error) -> Platform.runLater(() -> {
            try {
                onLocated(id, result, error);
            } catch (RuntimeException e) {
                // Never leave Locate… disabled; the global handler reports the error.
                locating.set(false);
                throw e;
            }
        }));
    }

    /** What the picked file turned out to be; {@code locating} is cleared once nothing more will happen. */
    private void onLocated(UUID id, PickedProject picked, Throwable error) {
        Optional<ProjectViewModel> row = rowById(id);
        if (row.isEmpty()) {
            locating.set(false);
            banners.show(BANNER_LOCATE, BannerBar.Kind.WARNING,
                    "The project was removed from the list meanwhile, so it was not relinked.", true);
            return;
        }
        if (error != null || picked == null) {
            locating.set(false);
            showErrorDialog("Locate Project", "Could not check the picked file: " + describe(error));
            return;
        }
        List<RecentEntry> listed = projectsList.stream().map(ProjectViewModel::getEntry).toList();
        switch (LocateDecision.decide(row.get().getEntry(), picked, listed)) {
            case LocateDecision.Refuse refuse -> {
                locating.set(false);
                showErrorDialog("Locate Project", refuse.message());
            }
            case LocateDecision.Recheck recheck -> {
                locating.set(false);
                check(row.get());
            }
            case LocateDecision.Relink relink -> {
                if (relink.confirmation() != null && !confirm("Locate Project", relink.confirmation())) {
                    locating.set(false);
                    return;
                }
                if (readOnly.get()) {
                    locating.set(false);
                    showErrorDialog("Locate Project", "The recent-projects list is read-only, so the project cannot be"
                            + " relinked right now.");
                    return;
                }
                runInBackground(() -> hub.relinkProject(id, relink.found()), relinked -> {
                    locating.set(false);
                    onRelinked(id, relinked);
                }, saveError -> {
                    locating.set(false);
                    showSaveError(saveError);
                });
            }
        }
    }

    private void onRelinked(UUID id, RelinkedProject relinked) {
        applySavedView(relinked.view());
        switch (relinked.outcome()) {
            case RELINKED -> {
                // A new path makes a new row, which starts as Checking and is checked.
                banners.hide(BANNER_LOCATE);
                selectById(id);
            }
            case UNCHANGED -> {
                banners.hide(BANNER_LOCATE);
                selectById(id);
                rowById(id).ifPresent(this::check);
            }
            case GONE -> banners.show(BANNER_LOCATE, BannerBar.Kind.WARNING,
                    "The project was removed from the list meanwhile, so it was not relinked.", true);
            case ALREADY_LISTED -> banners.show(BANNER_LOCATE, BannerBar.Kind.WARNING, "That project file is already"
                    + " in the list as \"" + relinked.conflict().map(RecentEntry::displayName).orElse("another entry")
                    + "\", so nothing was changed.", true);
        }
    }

    private Optional<ProjectViewModel> rowById(UUID id) {
        return projectsList.stream().filter(row -> row.getId().equals(id)).findFirst();
    }

    /** Asks an OK/Cancel question; {@code true} for OK. */
    private boolean confirm(String title, String message) {
        Alert alert = new Alert(Alert.AlertType.CONFIRMATION);
        alert.setTitle(title);
        alert.setHeaderText(null);
        alert.setContentText(message);
        alert.initOwner(window());
        return alert.showAndWait().filter(ButtonType.OK::equals).isPresent();
    }

    private void openSelectedProject() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || !selected.isOpenable() || launching.get() || choosingEngine.get()) {
            return;
        }
        launchProject(selected.getProjectPath(), selected.getEntry(), null);
    }

    /**
     * Opens {@code projectFile} with the engine it is bound to (VK-1681): the engines are checked in the background,
     * {@link LaunchPlan} decides, and the user is asked when it cannot. A project an editor of this window already
     * has open starts nothing (VK-1683). Once the editor is ready, the entry is marked as opened (and bound to its
     * engine on its first open) when it is listed and the list may be written, and the after-launch setting applies.
     *
     * @param entry        the project's entry, or {@code null} when it is not listed
     * @param fallbackName the name of a project that is not listed (its {@code projectName}, VK-1685); {@code null}
     *                     for the file's name without {@code .vfproj}
     * @return {@code false} when nothing happened because a launch or an engine choice is in progress
     */
    private boolean launchProject(Path projectFile, RecentEntry entry, String fallbackName) {
        if (launching.get() || choosingEngine.get()) {
            return false;
        }
        // One session per project: Open on a running project starts nothing (VK-1683).
        Optional<EditorSessions.Live> live = sessions.find(projectFile);
        if (live.isPresent()) {
            showAlreadyRunning(live.get());
            return true;
        }

        UUID id = entry == null ? null : entry.id();
        String engineId = entry == null ? null : entry.engineId();
        String name = entry != null ? entry.displayName()
                : fallbackName != null && !fallbackName.isBlank() ? fallbackName : projectName(projectFile);
        McpSetting mcp = entry == null ? McpSetting.DEFAULT : entry.mcp();

        // Disable UI to prevent double-clicks while the engines are checked and the editor starts
        launching.set(true);

        withEngines(options -> {
            try {
                switch (hub.planLaunch(options, engineId)) {
                    case LaunchPlan.Ready ready ->
                            startEditor(ready.engine(), projectFile, name, mcp, id, ready.bindTo());
                    case LaunchPlan.Choose choose -> {
                        boolean canRemember = id != null && !readOnly.get();
                        Optional<EngineChooserDialog.Choice> choice = EngineChooserDialog.showForOpen(window(),
                                choose.problem(), choose.candidates(), canRemember, choose.rememberByDefault(),
                                engineServices);
                        if (choice.isEmpty() || choice.get().engine().isEmpty()) {
                            launching.set(false);
                            return;
                        }
                        EngineOption engine = choice.get().engine().get();
                        startEditor(engine, projectFile, name, mcp, id,
                                choice.get().remember() ? engine.id() : Optional.empty());
                    }
                }
            } catch (RuntimeException e) {
                // Never leave the window disabled; the global handler reports the error.
                launching.set(false);
                throw e;
            }
        }, error -> {
            launching.set(false);
            showErrorDialog("Launch Failed", "Could not check the engines: " + describe(error));
        });
        return true;
    }

    /** The project's file name without {@code .vfproj} (in any letter case), for a project that is not listed. */
    private static String projectName(Path projectFile) {
        String fileName = projectFile.getFileName() == null ? projectFile.toString()
                : projectFile.getFileName().toString();
        return ProjectFiles.stem(fileName);
    }

    /**
     * Starts the editor of {@code engine} and hands it to a session (VK-1683). {@code launching} must already be set;
     * it is cleared once the process has started (or failed to), and the session is listed in the same FX task, so
     * no second launch of the project can slip in between.
     *
     * @param id     the project's entry, or {@code null} when it is not listed
     * @param bindTo the engine the entry is bound to once the editor is ready
     */
    private void startEditor(EngineOption engine, Path projectFile, String name, McpSetting mcp, UUID id,
                             Optional<UUID> bindTo) {
        if (mcp.enabled()) {
            sessions.usingMcpPort(mcp.port()).ifPresent(other -> banners.show(BANNER_SESSION_PREFIX + "port",
                    BannerBar.Kind.WARNING, "\"" + other.projectName() + "\" already runs with its MCP server on port "
                            + mcp.port() + ", so the MCP server of \"" + name + "\" cannot start. Give one of them"
                            + " another port (right-click the project, then MCP…).", true));
        }
        LaunchRequest request = new LaunchRequest(engine, projectFile, name, mcp, hub.settings().showEditorConsole());
        hub.launch(request).whenComplete((result, error) -> Platform.runLater(() -> {
            launching.set(false);
            if (error != null || result == null) {
                showErrorDialog("Launch Failed", "Unexpected error: " + describe(error));
                return;
            }
            switch (result) {
                case StartResult.Failed failed -> showErrorDialog("Launch Failed", failed.message());
                case StartResult.Started started -> beginSession(started.start(), mcp, id, bindTo);
            }
        }));
    }

    /** Lists and starts watching an editor that has just started. */
    private void beginSession(SessionStart start, McpSetting mcp, UUID id, Optional<UUID> bindTo) {
        EditorSession session = hub.newSession(start, MainController::runLaterQuietly,
                new BoundListener(id, bindTo));
        sessions.add(session, mcp);
        showSessionPhase(session, EditorSession.Phase.STARTING);
        session.begin();
    }

    /** Says that the project is already open in an editor this window started, and brings nothing up. */
    private void showAlreadyRunning(EditorSessions.Live live) {
        banners.show(sessionBanner(live.session()), BannerBar.Kind.INFO, "\"" + live.projectName()
                + "\" is already open in an editor this launcher started ("
                + live.phase().label().toLowerCase(Locale.ROOT) + "); nothing new was started.", true, "Open log",
                () -> openFile(live.session().start().log()));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Editor sessions (VK-1683)
    // ---------------------------------------------------------------------------------------------------------------

    /** One session's listener; it knows the entry to mark as opened once the editor is ready. FX thread. */
    private final class BoundListener implements EditorSession.Listener {
        private final UUID id;
        private final Optional<UUID> bindTo;

        BoundListener(UUID id, Optional<UUID> bindTo) {
            this.id = id;
            this.bindTo = bindTo;
        }

        @Override
        public void phaseChanged(EditorSession session, EditorSession.Phase phase) {
            showSessionPhase(session, phase);
            switch (phase) {
                case READY -> onSessionReady(session, id, bindTo);
                case NO_READY_SIGNAL -> onNoReadySignal(session);
                case LOADING -> banners.hide(sessionBanner(session));
                case STARTING, EXITED -> {
                    // STARTING is shown at the start; EXITED comes through exited().
                }
            }
        }

        @Override
        public void crashReported(EditorSession session, LaunchStatus status) {
            restoreWindow();
            banners.show(sessionBanner(session), BannerBar.Kind.ERROR, "The editor of \""
                    + session.start().projectName() + "\" crashed (" + EditorExitCode.describe(status.code())
                    + ") and is showing its crash message. The details follow when it closes.", true,
                    "Open log", () -> openFile(session.start().log()));
        }

        @Override
        public void exited(EditorSession session, SessionReport report) {
            onSessionExited(session, report);
        }
    }

    private static String sessionBanner(EditorSession session) {
        return BANNER_SESSION_PREFIX + session.start().log().getFileName();
    }

    /**
     * Shows the phase in the registry and on the project's row; the other projects of its folder are worked out again,
     * as the editor locks the whole folder (VK-1686).
     */
    private void showSessionPhase(EditorSession session, EditorSession.Phase phase) {
        sessions.setPhase(session, phase);
        Path project = EditorSessions.key(session.start().project());
        for (ProjectViewModel row : projectsList) {
            if (row.getProjectPath() == null) {
                continue;
            }
            Path rowProject = EditorSessions.key(row.getProjectPath());
            if (rowProject.equals(project)) {
                row.setSessionPhase(phase);
            }
            if (Objects.equals(rowProject.getParent(), project.getParent())) {
                applyHealth(row);
            }
        }
    }

    /**
     * The editor has opened its project: mark the entry as opened (and bind it on its first open), then apply the
     * after-launch setting. A failed save keeps the window as it is and says so.
     */
    private void onSessionReady(EditorSession session, UUID id, Optional<UUID> bindTo) {
        banners.hide(sessionBanner(session));
        if (id != null && !readOnly.get()) {
            runInBackground(() -> hub.markOpened(id, bindTo), view -> {
                applySavedView(view);
                applyAfterLaunch();
            }, this::showSaveError);
        } else {
            applyAfterLaunch();
        }
    }

    /**
     * Minimize, close or stay, once an editor is ready. Nothing happens while the user is busy in the window (a
     * launch, an engine choice, a creation or any other dialog); Close also waits until no other editor can still
     * fail to open its project, so no failure goes unreported.
     */
    private void applyAfterLaunch() {
        // Requests to add or open projects still waiting (VK-1685) keep the window up too.
        if (launching.get() || choosingEngine.get() || creationRunning() || modalDialogShowing()
                || addFlow.isBusy()) {
            return;
        }
        Stage stage = (Stage) window();
        switch (hub.settings().afterLaunch()) {
            case MINIMIZE -> stage.setIconified(true);
            case CLOSE -> {
                if (sessions.anyBeforeReady()) {
                    stage.setIconified(true);
                } else {
                    // A second launcher must no longer be told its request was taken (VK-1685).
                    beforeExit.run();
                    // stop() waits for the store thread; the editors keep running and their logs stay complete.
                    Platform.exit();
                }
            }
            case STAY -> {
                // Nothing to do.
            }
        }
    }

    /** Whether a dialog that blocks the main window is open. */
    private boolean modalDialogShowing() {
        Window main = window();
        for (Window other : Window.getWindows()) {
            if (other != main && other.isShowing() && other instanceof Stage stage
                    && stage.getModality() != Modality.NONE) {
                return true;
            }
        }
        return false;
    }

    private void onNoReadySignal(EditorSession session) {
        SessionStart start = session.start();
        String why = start.statusOmitted()
                .map(reason -> "The launcher cannot tell when it is ready: " + reason + ".")
                .orElse(start.contractKnown()
                        ? "It should have reported its state by now; it may be stuck before loading."
                        : "It may be an editor that does not report its state (one built before the launch"
                                + " contract); if the project did not open, its log says why.");
        // The launcher never minimized for this editor (that waits for Ready), so it is still up; it is not pulled in
        // front of the editor the user may be working in.
        banners.show(sessionBanner(session), BannerBar.Kind.WARNING, "The editor of \"" + start.projectName()
                + "\" is running but sent no ready signal after " + EditorSession.READY_TIMEOUT.toSeconds() + " s. "
                + why, true, "Open log", () -> openFile(start.log()));
    }

    /**
     * An editor ended: unlist it, re-check the projects, come back when this was the last editor or it failed, and
     * explain a failure.
     */
    private void onSessionExited(EditorSession session, SessionReport report) {
        sessions.remove(session);
        showSessionPhase(session, EditorSession.Phase.EXITED);
        banners.hide(sessionBanner(session));

        SessionOutcome outcome = report.outcome();
        boolean failed = !(outcome instanceof SessionOutcome.Normal);
        if (failed || sessions.liveCount() == 0) {
            restoreWindow();
        }
        // The project files may have changed while the editor ran (AC: the statuses are refreshed).
        for (ProjectViewModel row : projectsList) {
            check(row);
        }

        String name = session.start().projectName();
        Path log = report.log();
        switch (outcome) {
            case SessionOutcome.Normal normal -> {
                // A normal close: nothing to say.
            }
            case SessionOutcome.NoReadySignal noSignal -> banners.show(sessionBanner(session),
                    BannerBar.Kind.WARNING, outcome.headline(name), true, "Open log", () -> openFile(log));
            case SessionOutcome.ConsoleClosed closed -> banners.show(sessionBanner(session),
                    BannerBar.Kind.WARNING, outcome.headline(name), true, "Open log", () -> openFile(log));
            case SessionOutcome.Refused refused when !outcome.showsFailureDialog() -> banners.show(
                    sessionBanner(session), BannerBar.Kind.INFO, outcome.headline(name), true, "Open log",
                    () -> openFile(log));
            default -> SessionFailureDialog.show(window(), name, report, new SessionFailureDialog.Actions() {
                @Override
                public void openLog(SessionReport failure) {
                    openFile(failure.log());
                }

                @Override
                public void openCrashFolder(SessionReport failure) {
                    MainController.this.openCrashFolder(failure);
                }
            });
        }
    }

    /** Brings the window back from the taskbar and to the front, before any dialog is shown on it. */
    private void restoreWindow() {
        Stage stage = (Stage) window();
        if (stage.isIconified()) {
            stage.setIconified(false);
        }
        stage.toFront();
        stage.requestFocus();
    }

    /** Opens a file with its default program; off the FX thread, a failure is a banner. */
    private void openFile(Path file) {
        CompletableFuture.runAsync(() -> {
            try {
                hub.shell().open(file);
            } catch (IOException e) {
                throw new CompletionException(e);
            }
        }).whenComplete((ignored, error) -> {
            if (error != null) {
                runLaterQuietly(() -> banners.show(BANNER_SESSION_PREFIX + "open", BannerBar.Kind.ERROR,
                        "Could not open " + file + ": " + describe(error), true));
            }
        });
    }

    /** Shows the newest crash report in Explorer, else the crash folder, else says there is none. */
    private void openCrashFolder(SessionReport report) {
        CompletableFuture.supplyAsync(() -> {
            try {
                if (report.crashReport().isPresent()) {
                    hub.shell().showInFolder(report.crashReport().get().text());
                    return true;
                }
                if (Files.isDirectory(report.crashFolder())) {
                    hub.shell().open(report.crashFolder());
                    return true;
                }
                return false;
            } catch (IOException e) {
                throw new CompletionException(e);
            }
        }).whenComplete((opened, error) -> runLaterQuietly(() -> {
            if (error != null) {
                banners.show(BANNER_SESSION_PREFIX + "open", BannerBar.Kind.ERROR,
                        "Could not open the crash folder: " + describe(error), true);
            } else if (!Boolean.TRUE.equals(opened)) {
                banners.show(BANNER_SESSION_PREFIX + "open", BannerBar.Kind.INFO, "The editor has no crash folder ("
                        + report.crashFolder() + "): it has never crashed there.", true);
            }
        }));
    }

    /** {@link Platform#runLater}, ignoring a toolkit that has already exited (the launcher is closing). */
    private static void runLaterQuietly(Runnable action) {
        try {
            Platform.runLater(action);
        } catch (IllegalStateException e) {
            // The toolkit is gone.
        }
    }

    /** "Open with MCP" for the selected project (VK-1683). */
    private void editMcpOfSelected() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || readOnly.get() || launching.get() || choosingEngine.get()) {
            return;
        }
        UUID id = selected.getId();
        Optional<McpSetting> chosen = McpDialog.show(window(), selected.getName(), selected.getEntry().mcp());
        chosen.ifPresent(setting -> runInBackground(() -> hub.setMcp(id, setting), this::applySavedView,
                this::showSaveError));
    }

    @FXML
    private void onOpenSettings() {
        if (launching.get() || choosingEngine.get()) {
            return;
        }
        LauncherSettings current = hub.settings();
        Optional<SettingsDialog.Result> chosen = SettingsDialog.show(window(), current, hub.canSaveSettings(),
                hub.sessionLogsDirectory());
        chosen.ifPresent(result -> runInBackground(
                () -> hub.saveSettings(result.afterLaunch(), result.showEditorConsole()),
                saved -> banners.hide(BANNER_SETTINGS),
                error -> banners.show(BANNER_SETTINGS, BannerBar.Kind.ERROR,
                        "Could not save the settings: " + describe(error), true)));
    }

    @FXML
    private void onRefreshProjects() {
        // Re-read and re-check, whatever the focus throttle says; nothing is ever removed here.
        reload();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Existing projects (VK-1685)
    // ---------------------------------------------------------------------------------------------------------------

    /** "Add Existing…" and its "Project File…" item: one or more project files. */
    @FXML
    private void onAddExisting() {
        if (readOnly.get() || addFlow.isBusy()) {
            return;
        }
        FileChooser chooser = new FileChooser();
        chooser.setTitle("Add Existing Project");
        // The reader checks the extension again, in any letter case, so "All files" can only lead to a clear refusal.
        chooser.getExtensionFilters().addAll(
                new FileChooser.ExtensionFilter("VertexForge projects (*" + ProjectFiles.EXTENSION + ")",
                        "*" + ProjectFiles.EXTENSION),
                new FileChooser.ExtensionFilter("All files", "*.*"));
        List<File> files = chooser.showOpenMultipleDialog(window());
        if (files != null && !files.isEmpty()) {
            addProjects(toPaths(files));
        }
    }

    /** The "Project Folder…" item: a folder holding a project file. */
    @FXML
    private void onAddExistingFolder() {
        if (readOnly.get() || addFlow.isBusy()) {
            return;
        }
        DirectoryChooser chooser = new DirectoryChooser();
        chooser.setTitle("Add Existing Project Folder");
        File folder = chooser.showDialog(window());
        if (folder != null) {
            addProjects(toPaths(List.of(folder)));
        }
    }

    /** What the add flow needs from this window. FX thread, except where a method says otherwise. */
    private final class FlowServices implements AddExistingFlow.Services {

        @Override
        public CompletableFuture<PickedProject> inspect(Path path) {
            return hub.inspectProject(path);
        }

        @Override
        public CompletableFuture<AddedProject> add(PickedProject.Found found) {
            CompletableFuture<AddedProject> result = new CompletableFuture<>();
            runInBackground(() -> hub.addExisting(found), result::complete, result::completeExceptionally);
            return result;
        }

        @Override
        public Optional<PickedProject.Found> choose(PickedProject.Choose choose) {
            return ProjectChooserDialog.show(window(), choose);
        }

        @Override
        public boolean readOnly() {
            return readOnly.get();
        }

        @Override
        public boolean busy() {
            // No window yet (still starting, or the self-test's window-less scene): wait until it shows.
            return !firstLoad.isDone() || windowOrNull() == null || launching.get() || choosingEngine.get()
                    || modalDialogShowing();
        }

        @Override
        public void added(AddedProject added) {
            applySavedView(added.view());
            selectById(added.entry().id());
            projectsTable.scrollTo(projectsTable.getSelectionModel().getSelectedItem());
        }

        @Override
        public void saveFailed(Throwable error) {
            showSaveError(error);
        }

        @Override
        public boolean open(Path projectFile, RecentEntry entry, String name) {
            return launchProject(projectFile, entry, name);
        }

        @Override
        public void report(AddExistingFlow.Report report) {
            report.summary().ifPresentOrElse(
                    text -> banners.show(BANNER_ADD, report.hasWarning() ? BannerBar.Kind.WARNING
                            : BannerBar.Kind.INFO, text, true),
                    () -> banners.hide(BANNER_ADD));
            report.refusals().ifPresent(text -> showErrorDialog("Add Existing Project", text));
        }
    }

    @FXML
    private void onCreateProject() {
        if (creating.get() || readOnly.get()) {
            return;
        }
        Window owner = projectsTable.getScene().getWindow();

        // The dialog checks, creates and shows progress itself (VK-1679); it ends with the creation to follow up on.
        // It reads the engines and their templates itself too (VK-1689).
        List<CreationJob> started = new ArrayList<>();
        Optional<CreateProjectForm.Result> result = CreateProjectDialog.show(owner, createServices(started));
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
            public CompletableFuture<EngineOptions> engines() {
                // Also refreshes the window's engine column and banners; completes on the FX thread.
                return engineServices.refresh();
            }

            @Override
            public CompletableFuture<TemplateCatalogs.Answer> templates(EngineContent engine) {
                return hub.listTemplates(engine);
            }

            @Override
            public CompletableFuture<LocationCheck> checkLocation(Path location, String name,
                                                                  ProjectBlueprint blueprint) {
                return hub.checkLocation(location, name, blueprint);
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
                // Its first open: it is bound to the engine it opens with (VK-1681), like any other project.
                Optional<RecentEntry> listed = outcome.listed()
                        ? outcome.view().findByPath(outcome.projectPath())
                        : Optional.empty();
                launchProject(outcome.projectPath(), listed.orElse(null), null);
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
    // Engines (VK-1681)
    // ---------------------------------------------------------------------------------------------------------------

    @FXML
    private void onManageEngines() {
        // A launch or a Change Engine… may be about to open its own dialog: one modal flow at a time.
        if (launching.get() || choosingEngine.get()) {
            return;
        }
        EnginesDialog.show(window(), engineOptions, engineServices);
    }

    /** Binds the selected project to another engine, or back to the default one. */
    private void changeEngineOfSelected() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || readOnly.get() || launching.get() || choosingEngine.get()) {
            return;
        }
        UUID id = selected.getId();
        String name = selected.getName();
        String current = selected.getEntry().engineId();
        choosingEngine.set(true);
        withEngines(options -> {
            try {
                Optional<EngineChooserDialog.Choice> choice =
                        EngineChooserDialog.showForChange(window(), name, options, current, engineServices);
                if (choice.isEmpty()) {
                    return;
                }
                // "Use the default engine" unbinds the project; it is bound again when it next opens.
                UUID engineId = choice.get().engine().flatMap(EngineOption::id).orElse(null);
                runInBackground(() -> hub.bindEngine(id, engineId), this::applySavedView, this::showSaveError);
            } finally {
                choosingEngine.set(false);
            }
        }, error -> {
            choosingEngine.set(false);
            showErrorDialog("Change Engine", "Could not check the engines: " + describe(error));
        });
    }

    /** Lists and checks the engines again (startup, Refresh); a failure only shows a banner. */
    private void reloadEngines() {
        withEngines(options -> {
            // Shown by withEngines.
        }, error -> banners.show(BANNER_ENGINES_CHECK, BannerBar.Kind.ERROR,
                "The engines could not be checked: " + describe(error) + ". Press Refresh to try again.", true));
    }

    /**
     * Lists the engines on the store thread and checks them on their own threads (each with a timeout). The result
     * is shown, unless a later check has started meanwhile, and handed to {@code onReady}, on the FX thread. Exactly
     * one of {@code onReady} and {@code onFailure} is called, so a caller's in-progress state is always cleared.
     */
    private void withEngines(Consumer<EngineOptions> onReady, Consumer<Throwable> onFailure) {
        long token = ++engineToken;
        runInBackground(hub::listEngines, listing -> {
            try {
                showEngineStoreBanners(listing.registry());
                hub.checkEngines(listing).whenComplete((options, error) -> Platform.runLater(() -> {
                    if (error != null || options == null) {
                        onFailure.accept(error != null ? unwrap(error)
                                : new IllegalStateException("the engine check gave no answer"));
                        return;
                    }
                    try {
                        if (token == engineToken) {
                            applyEngineOptions(options);
                        }
                    } catch (RuntimeException e) {
                        onFailure.accept(e);
                        return;
                    }
                    onReady.accept(options);
                }));
            } catch (RuntimeException e) {
                onFailure.accept(e);
            }
        }, onFailure);
    }

    private void applyEngineOptions(EngineOptions options) {
        engineOptions = options;
        banners.hide(BANNER_ENGINES_CHECK);
        showOverrideBanner(options);
        for (ProjectViewModel row : projectsList) {
            applyEngineLabel(row);
            // Whether its schema needs a newer engine depends on the engines (VK-1686).
            applyHealth(row);
        }
    }

    private void applyEngineLabel(ProjectViewModel row) {
        row.setEngineText(EngineLabels.forProject(row.getEntry().engineId(), engineOptions));
    }

    /** The override decides every launch, so it is always on show while it is set. */
    private void showOverrideBanner(EngineOptions options) {
        Optional<EngineOption> override = options.override();
        if (override.isEmpty()) {
            banners.hide(BANNER_OVERRIDE);
            return;
        }
        EngineOption value = override.get();
        if (value.isOk()) {
            banners.show(BANNER_OVERRIDE, BannerBar.Kind.INFO, EditorOverride.ENV_VAR + " is set: every project"
                    + " opens with " + value.pathText() + " (" + value.versionText() + "), whatever engine it uses.",
                    false);
        } else {
            banners.show(BANNER_OVERRIDE, BannerBar.Kind.WARNING, EditorOverride.ENV_VAR + " is set to \""
                    + value.rawPath() + "\", which is ignored: " + value.health().reason() + ".", false);
        }
    }

    private void showEngineStoreBanners(EngineRegistryView view) {
        switch (view.status()) {
            case NEWER_READ_ONLY -> banners.show(BANNER_ENGINES_READ_ONLY, BannerBar.Kind.INFO,
                    "The engine list was saved by a newer version of the launcher, so it is read-only here: its"
                            + " engines can be used, but not added, removed or made the default.", false);
            case CORRUPT, CORRUPT_UNMOVABLE -> banners.show(BANNER_ENGINES_READ_ONLY, BannerBar.Kind.ERROR,
                    "The engine list is damaged and could not be set aside (" + view.detail()
                            + "). It is read-only until the file is fixed or removed. Press Refresh to try again.",
                    false);
            case UNREADABLE -> banners.show(BANNER_ENGINES_READ_ONLY, BannerBar.Kind.ERROR,
                    "The engine list could not be read (" + view.detail()
                            + "). It is read-only for now. Press Refresh to try again.", false);
            default -> banners.hide(BANNER_ENGINES_READ_ONLY);
        }
        view.quarantinedTo().ifPresent(this::showEngineQuarantineBanner);
    }

    /** Stays until dismissed, even after later reads find the new list fine. */
    private void showEngineQuarantineBanner(Path movedTo) {
        banners.show(BANNER_ENGINES_QUARANTINE, BannerBar.Kind.WARNING,
                "The engine list was damaged. It was kept as " + movedTo.getFileName() + " and a new list was"
                        + " started; projects that used its engines will ask which engine to use.", true);
    }

    private void showEngineSaveError(Throwable error) {
        banners.show(BANNER_ENGINES, BannerBar.Kind.ERROR,
                "Could not save the engine list: " + describe(error), true);
        if (error instanceof StoreWriteException storeError) {
            storeError.quarantinedTo().ifPresent(this::showEngineQuarantineBanner);
        }
    }

    private Window window() {
        return projectsTable.getScene().getWindow();
    }

    /** The main window, or {@code null} while the content is not in a showing window yet. */
    private Window windowOrNull() {
        Scene scene = projectsTable.getScene();
        Window window = scene == null ? null : scene.getWindow();
        return window != null && window.isShowing() ? window : null;
    }

    /**
     * The engine dialogs' requests. Every one is started on the FX thread, does its work in the background and
     * completes on the FX thread.
     */
    private final class ControllerEngineServices implements EngineServices {

        @Override
        public CompletableFuture<EngineOptions> refresh() {
            CompletableFuture<EngineOptions> result = new CompletableFuture<>();
            withEngines(result::complete, result::completeExceptionally);
            return result;
        }

        @Override
        public CompletableFuture<AddOutcome> add(Path picked) {
            CompletableFuture<AddOutcome> result = new CompletableFuture<>();
            hub.inspectEngine(picked).whenComplete((inspected, error) -> Platform.runLater(() -> {
                if (error != null || inspected == null) {
                    result.completeExceptionally(error != null ? unwrap(error)
                            : new IllegalStateException("the engine check gave no answer"));
                    return;
                }
                switch (inspected) {
                    case PickedEngine.Refused refused -> result.complete(AddOutcome.refused(refused.reason()));
                    case PickedEngine.Usable usable -> runInBackground(() -> hub.addEngine(usable), added -> {
                        banners.hide(BANNER_ENGINES);
                        withEngines(options -> result.complete(
                                        AddOutcome.added(options, added.engine().id(), added.existed())),
                                result::completeExceptionally);
                    }, saveError -> failChange(result, saveError));
                }
            }));
            return result;
        }

        @Override
        public CompletableFuture<EngineOptions> remove(UUID id) {
            return change(() -> hub.removeEngine(id));
        }

        @Override
        public CompletableFuture<EngineOptions> setDefault(UUID id) {
            return change(() -> hub.setDefaultEngine(id));
        }

        @Override
        public int projectsUsing(UUID id) {
            int count = 0;
            for (ProjectViewModel row : projectsList) {
                if (EngineRegistryCodec.uuid(row.getEntry().engineId()).equals(Optional.of(id))) {
                    count++;
                }
            }
            return count;
        }

        /** Saves a change on the store thread, then lists and checks the engines again. */
        private CompletableFuture<EngineOptions> change(Callable<EngineRegistryView> save) {
            CompletableFuture<EngineOptions> result = new CompletableFuture<>();
            runInBackground(save, saved -> {
                banners.hide(BANNER_ENGINES);
                withEngines(result::complete, result::completeExceptionally);
            }, error -> failChange(result, error));
            return result;
        }

        /** A change that was not saved: say so, and show the list's real state (it may have changed underneath). */
        private void failChange(CompletableFuture<?> result, Throwable error) {
            showEngineSaveError(error);
            result.completeExceptionally(error);
            if (error instanceof StoreWriteException) {
                reloadEngines();
            }
        }
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
