package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.dialog.CopyNameDialog;
import com.vertexforge.launcher.dialog.CopyNamePrompt;
import com.vertexforge.launcher.dialog.CreateProjectDialog;
import com.vertexforge.launcher.dialog.CreateProjectForm;
import com.vertexforge.launcher.dialog.DialogFactory;
import com.vertexforge.launcher.dialog.EngineChooserDialog;
import com.vertexforge.launcher.dialog.EngineServices;
import com.vertexforge.launcher.dialog.EnginesDialog;
import com.vertexforge.launcher.dialog.FileJobDialog;
import com.vertexforge.launcher.dialog.LibraryUpdateDialog;
import com.vertexforge.launcher.dialog.LibraryUpdatePrompt;
import com.vertexforge.launcher.dialog.McpDialog;
import com.vertexforge.launcher.dialog.ProjectChooserDialog;
import com.vertexforge.launcher.dialog.SessionFailureDialog;
import com.vertexforge.launcher.dialog.SettingsDialog;
import com.vertexforge.launcher.dialog.SettingsForm;
import com.vertexforge.launcher.dialog.VersionDialog;
import com.vertexforge.launcher.dialog.VersionPrompt;
import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineLabels;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryCodec;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.engine.EngineResolver;
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
import com.vertexforge.launcher.service.FileJob;
import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.LocationChecker.LocationCheck;
import com.vertexforge.launcher.service.ProjectBackupService;
import com.vertexforge.launcher.service.ProjectCopier;
import com.vertexforge.launcher.service.ProjectHealthService;
import com.vertexforge.launcher.service.ProjectHealthService.FolderEditor;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.service.ScriptLibraryStatusService;
import com.vertexforge.launcher.service.ScriptLibraryUpdater;
import com.vertexforge.launcher.service.ProjectNameRules;
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
import com.vertexforge.launcher.settings.LauncherState;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.ui.BannerBar;
import com.vertexforge.launcher.viewmodel.ProjectOrder;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import com.vertexforge.launcher.viewmodel.StatusDisplay;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.value.ChangeListener;
import javafx.beans.value.ObservableValue;
import javafx.beans.value.WeakChangeListener;
import javafx.collections.ListChangeListener;
import javafx.collections.ObservableList;
import javafx.css.PseudoClass;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.Node;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.ContentDisplay;
import javafx.scene.control.Label;
import javafx.scene.control.MenuItem;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.SeparatorMenuItem;
import javafx.scene.control.SplitMenuButton;
import javafx.scene.control.TableCell;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.TextField;
import javafx.scene.control.Tooltip;
import javafx.scene.input.Clipboard;
import javafx.scene.input.ClipboardContent;
import javafx.scene.input.DragEvent;
import javafx.scene.input.Dragboard;
import javafx.scene.input.KeyCode;
import javafx.scene.input.KeyCodeCombination;
import javafx.scene.input.KeyCombination;
import javafx.scene.input.KeyEvent;
import javafx.scene.input.MouseButton;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.shape.SVGPath;
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
import java.util.LinkedHashMap;
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
    /** What Open did before the editor started: a backup, a copy, a cancelled job (VK-1690). */
    static final String BANNER_VERSION = "version";
    static final String BANNER_LIBRARY = "script-library";
    /** The list has projects but no engine can open them (VK-1694). */
    static final String BANNER_NO_ENGINE = "no-engine";

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

    /** The empty screen's "Add an engine" part (VK-1694). */
    @FXML
    private VBox noEngineBox;

    @FXML
    private Label noEngineText;

    @FXML
    private Button addEngineButton;

    @FXML
    private Button emptyCreateButton;

    @FXML
    private Button emptyAddButton;

    /** Why the empty screen's buttons are disabled: the list is read-only (VK-1694). */
    @FXML
    private Label readOnlyHint;

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

    /** Refresh; F5 presses it (VK-1694). */
    @FXML
    private Button refreshButton;

    /** Filters the list as you type (VK-1694). */
    @FXML
    private TextField searchField;

    /** Every listed row, and the search's sorted view of them that the table shows (VK-1694). */
    private final ProjectTableModel tableModel = new ProjectTableModel();
    /** Every listed row, whether the search shows it or not: the checks and every lookup work on it. */
    private final ObservableList<ProjectViewModel> projectsList = tableModel.source();
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
    /** Opens projects once it is clear that their engine reads them (VK-1690). */
    private final OpenProjectFlow openFlow = new OpenProjectFlow(new OpenServices(),
            MainController::runLaterQuietly);
    /** Updates a project's script library to its engine's (VK-1691). */
    private final ScriptLibraryUpdateFlow libraryFlow = new ScriptLibraryUpdateFlow(new LibraryServices(),
            MainController::runLaterQuietly);

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

    /** The table's placeholder while the search hides nothing: "Loading…", or why the list could not be read. */
    private Node listPlaceholder;
    /** Remembers the table's sort (VK-1694); set in {@link #initialize}. */
    private TableSortMemory<ProjectViewModel> sortMemory;
    /** The window's shortcuts are installed once the table is in a scene (VK-1694). */
    private boolean shortcutsInstalled;
    /** The list has been read at least once, so the empty screen may say it is empty (VK-1694). */
    private boolean listRead;
    /** The engines' first check has answered (or failed), so the empty screen may say whether one works (VK-1694). */
    private boolean enginesChecked;

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
        setupSearch();
        setupShortcuts();
        // Before the first read, so the list is shown sorted at once.
        setupSortMemory();

        // Until the first read answers, show the (empty) table with a loading note rather than "no projects".
        setListPlaceholder(new Label("Loading…"));
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

    /** Types {@code text} into the search box (the self-test's check of the filter, VK-1694). FX thread. */
    public void setSearchQuery(String text) {
        searchField.setText(text);
    }

    /** Saves a sort the user chose that is still waiting for its delay: the launcher is closing (VK-1694). FX thread. */
    public void saveSortNow() {
        if (sortMemory != null) {
            sortMemory.saveNow();
        }
    }

    /**
     * Asks a creation, a backup or a copy in progress to stop (the launcher is closing; VK-1690 runs its backups and
     * copies on the creation thread too). FX thread.
     */
    public void cancelCreation() {
        if (activeJob != null) {
            activeJob.cancel();
        }
        openFlow.cancelJob();
        libraryFlow.cancelJob();
    }

    /** Whether a creation, a backup, a copy or a script-library job (VK-1691) is in progress. FX thread. */
    public boolean creationRunning() {
        return activeJob != null || openFlow.jobRunning() || libraryFlow.jobRunning();
    }

    private void setupTableColumns() {
        nameColumn.setCellValueFactory(cell -> cell.getValue().nameProperty());
        // A pin before the name of a pinned project (VK-1694).
        nameColumn.setCellFactory(column -> new NameCell());
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
                    setGraphic(null);
                } else {
                    setText(display.label());
                    setTooltip(display.tooltip().isEmpty() ? null : new Tooltip(display.tooltip()));
                    // The script library's tag after the status (VK-1691).
                    String tag = display.libraryTag();
                    if (tag.isEmpty()) {
                        setGraphic(null);
                    } else {
                        Label label = new Label(tag);
                        label.getStyleClass().add("library-tag");
                        setGraphic(label);
                        setContentDisplay(ContentDisplay.RIGHT);
                    }
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
        projectsTable.setItems(tableModel.sorted());
        projectsTable.getSelectionModel().setSelectionMode(SelectionMode.SINGLE);
        projectsTable.disableProperty().bind(launching);
        // Pinned projects stay on top under any column sort (VK-1694). The default policy refuses a SortedList whose
        // comparator is not bound to the table's own, and would undo the header click.
        projectsTable.setSortPolicy(table -> {
            tableModel.applyComparator(ProjectOrder.pinnedFirst(table.getComparator()));
            return true;
        });

        // Double-click to open project
        projectsTable.setOnMouseClicked(event -> {
            if (event.getButton() == MouseButton.PRIMARY && event.getClickCount() == 2) {
                openSelectedProject();
            }
        });

        // Enter opens the project; Delete removes it from the list, which the banner can undo (VK-1694).
        projectsTable.setOnKeyPressed(event -> {
            if (event.getCode() == KeyCode.ENTER) {
                openSelectedProject();
            } else if (event.getCode() == KeyCode.DELETE) {
                removeSelectedProject();
            }
        });

        // Context menu for right-click actions
        setupContextMenu();
    }

    private void setupContextMenu() {
        ContextMenu contextMenu = new ContextMenu();

        // VK-1694: a pinned project stays at the top of the list and is never trimmed.
        MenuItem pinItem = new MenuItem("Pin to Top");
        pinItem.setOnAction(e -> togglePinOfSelected());

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

        // VK-1691: brings the project's script library up to its engine's (it writes the project, not the list).
        MenuItem updateLibraryItem = new MenuItem("Update Script Library…");
        updateLibraryItem.setOnAction(e -> updateLibraryOfSelected());

        // VK-1683: "Open with MCP" and its port, stored on the entry.
        MenuItem mcpItem = new MenuItem("MCP…");
        mcpItem.setOnAction(e -> editMcpOfSelected());
        mcpItem.disableProperty().bind(readOnly.or(launching).or(choosingEngine));

        MenuItem removeItem = new MenuItem("Remove from List");
        removeItem.setOnAction(e -> removeSelectedProject());
        removeItem.disableProperty().bind(readOnly);

        contextMenu.getItems().addAll(pinItem, new SeparatorMenuItem(), showItem, copyPathItem, new SeparatorMenuItem(),
                locateItem, changeEngineItem, updateLibraryItem, mcpItem, new SeparatorMenuItem(), removeItem);
        // These depend on the row's status, which changes in the background; each handler checks again.
        contextMenu.setOnShowing(event -> {
            ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
            pinItem.setText(selected != null && selected.isPinned() ? "Unpin" : "Pin to Top");
            pinItem.setDisable(selected == null || readOnly.get());
            showItem.setDisable(!canShowInExplorer(selected));
            copyPathItem.setDisable(selected == null || isBlank(selected.getEntry().rawPath()));
            locateItem.setDisable(!canLocate(selected));
            updateLibraryItem.setDisable(!canUpdateLibrary(selected));
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

    /** The Name column's cell: the project's name, after a pin when the project is pinned (VK-1694). */
    private static final class NameCell extends TableCell<ProjectViewModel, String> {
        private final SVGPath pin = pinGlyph();
        private final ChangeListener<Boolean> pinListener = (observable, was, now) -> showPin(now);
        /** Weak: a cell the table drops without emptying it first must not stay registered on its last row. */
        private final WeakChangeListener<Boolean> weakPinListener = new WeakChangeListener<>(pinListener);
        private ProjectViewModel watched;

        @Override
        protected void updateItem(String name, boolean empty) {
            super.updateItem(name, empty);
            // A recycled cell stops following its previous row.
            if (watched != null) {
                watched.pinnedProperty().removeListener(weakPinListener);
                watched = null;
            }
            setText(empty ? null : name);
            ProjectViewModel row = empty ? null : rowAt(getTableView(), getIndex());
            if (row == null) {
                showPin(false);
            } else {
                watched = row;
                row.pinnedProperty().addListener(weakPinListener);
                showPin(row.isPinned());
            }
        }

        private static ProjectViewModel rowAt(TableView<ProjectViewModel> table, int index) {
            if (table == null || index < 0 || index >= table.getItems().size()) {
                return null;
            }
            return table.getItems().get(index);
        }

        private void showPin(boolean pinned) {
            setGraphic(pinned ? pin : null);
        }
    }

    /** A small push pin, 12 by 12 pixels, styled by {@code .pin-glyph}. */
    private static SVGPath pinGlyph() {
        SVGPath pin = new SVGPath();
        pin.setContent("M4 0H8V1H7V4L9 6V7H6.5V11L6 12L5.5 11V7H3V6L5 4V1H4Z");
        pin.getStyleClass().add("pin-glyph");
        return pin;
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
        // The empty screen's buttons do what the sidebar's do, under the same rules (VK-1694).
        emptyCreateButton.disableProperty().bind(createProjectButton.disableProperty());
        emptyAddButton.disableProperty().bind(addExistingButton.disableProperty());
        addEngineButton.disableProperty().bind(enginesButton.disableProperty());
        readOnlyHint.visibleProperty().bind(readOnly);
        readOnlyHint.managedProperty().bind(readOnly);
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

    /**
     * The search box filters the list as you type (VK-1694): every word must occur in a project's name, path or
     * engine. Enter opens the selected match, Down moves into the list, and Esc clears the box (or, when it is
     * already empty, goes back to the list).
     */
    private void setupSearch() {
        searchField.textProperty().addListener((observable, was, now) -> applySearch(now));
        searchField.setOnAction(event -> openSelectedProject());
        searchField.addEventFilter(KeyEvent.KEY_PRESSED, event -> {
            if (event.getCode() == KeyCode.ESCAPE) {
                if (searchField.getText().isEmpty()) {
                    projectsTable.requestFocus();
                } else {
                    searchField.clear();
                }
                event.consume();
            } else if (event.getCode() == KeyCode.DOWN && !tableModel.sorted().isEmpty()) {
                if (projectsTable.getSelectionModel().isEmpty()) {
                    projectsTable.getSelectionModel().selectFirst();
                }
                projectsTable.requestFocus();
                event.consume();
            }
        });
    }

    /**
     * Filters the rows. The selected project stays selected while it matches; otherwise the first match is selected,
     * so that Enter opens it. A hidden row is never left selected: Open and Delete would act on a row nobody sees.
     */
    private void applySearch(String query) {
        ProjectViewModel previous = projectsTable.getSelectionModel().getSelectedItem();
        tableModel.setQuery(query);
        if (previous != null && tableModel.isVisible(previous)) {
            projectsTable.getSelectionModel().select(previous);
        } else if (tableModel.filtering() && !tableModel.sorted().isEmpty()) {
            projectsTable.getSelectionModel().selectFirst();
        } else {
            projectsTable.getSelectionModel().clearSelection();
        }
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected != null) {
            projectsTable.scrollTo(selected);
        }
        updatePlaceholder();
    }

    /**
     * The window's shortcuts (VK-1694): Ctrl+N creates a project, Ctrl+O adds an existing one, Ctrl+F goes to the search
     * box and F5 refreshes. They press the buttons, so they do what the buttons do and nothing while a button is
     * disabled. Enter and Delete belong to the list (and Enter to the search box), so typing is never taken from a field.
     */
    private void setupShortcuts() {
        createProjectButton.setTooltip(new Tooltip("Create a new project (Ctrl+N)"));
        addExistingButton.setTooltip(new Tooltip("Add a project that is already on disk (Ctrl+O)"));
        refreshButton.setTooltip(new Tooltip("Check every project and engine again (F5)"));
        searchField.setTooltip(new Tooltip("Filter by name, path or engine (Ctrl+F); Esc clears"));
        projectsTable.sceneProperty().addListener((observable, was, scene) -> installShortcuts(scene));
        installShortcuts(projectsTable.getScene());
    }

    private void installShortcuts(Scene scene) {
        if (scene == null || shortcutsInstalled) {
            return;
        }
        shortcutsInstalled = true;
        scene.getAccelerators().put(new KeyCodeCombination(KeyCode.N, KeyCombination.SHORTCUT_DOWN),
                createProjectButton::fire);
        scene.getAccelerators().put(new KeyCodeCombination(KeyCode.O, KeyCombination.SHORTCUT_DOWN),
                addExistingButton::fire);
        scene.getAccelerators().put(new KeyCodeCombination(KeyCode.F, KeyCombination.SHORTCUT_DOWN), this::focusSearch);
        scene.getAccelerators().put(new KeyCodeCombination(KeyCode.F5), refreshButton::fire);
    }

    /** Ctrl+F: the search box, with its text selected; nothing while the list is not shown. */
    private void focusSearch() {
        if (projectsTablePane.isVisible() && !searchField.isDisabled()) {
            searchField.requestFocus();
            searchField.selectAll();
        }
    }

    /** Puts the remembered sort back and remembers every sort the user chooses from now on (VK-1694). */
    private void setupSortMemory() {
        Map<String, TableColumn<ProjectViewModel, ?>> columns = new LinkedHashMap<>();
        columns.put(ProjectSort.NAME, nameColumn);
        columns.put(ProjectSort.STATUS, statusColumn);
        columns.put(ProjectSort.ENGINE, engineColumn);
        columns.put(ProjectSort.PATH, pathColumn);
        columns.put(ProjectSort.LAST_OPENED, lastOpenedColumn);
        sortMemory = new TableSortMemory<>(projectsTable, columns, this::saveSortLater);
        sortMemory.restore(hub.launcherState().projectSort());
    }

    /** Hands a sort to the store thread; dropped when the launcher has already stopped it. */
    private void saveSortLater(List<LauncherState.SortKey> sort) {
        try {
            background.execute(() -> hub.saveProjectSort(sort));
        } catch (RejectedExecutionException e) {
            // The store thread has stopped: the launcher is closing.
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
            setListPlaceholder(new Label("The recent-projects list could not be read"));
            updateEmptyState();
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
        // One sort for the whole read, not one per changed row (VK-1694).
        tableModel.batch(() -> {
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

            // The table shows them through the search, pinned first and in the user's column sort (VK-1694).
            tableModel.setAll(rows);
        });
        listRead = true;
        updateEmptyState();
        if (selectedId != null) {
            selectById(selectedId);
        }
        updatePlaceholder();

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
            checkLibrary(row, fileCheck);
        }
    }

    /**
     * Checks the row's script library against the library of the engine it opens with (VK-1691), in the background,
     * when its file is OK and that engine is known; only the latest answer lands.
     */
    private void checkLibrary(ProjectViewModel row, ProjectFileCheck fileCheck) {
        long token = row.startLibraryCheck();
        Path projectFile = row.getProjectPath();
        if (!(fileCheck instanceof ProjectFileCheck.Readable readable) || !row.getStatus().isOk()
                || engineOptions == null || projectFile == null) {
            row.setLibrary(ScriptLibraryStatusService.LibraryState.UNKNOWN);
            return;
        }
        Optional<ScriptLibraryStatusService.EngineLibrary> library =
                switch (EngineResolver.resolve(engineOptions, row.getEntry().engineId())) {
                    case LaunchPlan.Ready ready -> ScriptLibraryStatusService.EngineLibrary.of(ready.engine());
                    case LaunchPlan.Choose ignored -> Optional.empty();
                };
        if (library.isEmpty()) {
            row.setLibrary(ScriptLibraryStatusService.LibraryState.UNKNOWN);
            return;
        }
        hub.checkScriptLibrary(projectFile, readable.file().workingDirectory(), library.get())
                .whenComplete((state, error) -> runLaterQuietly(() -> {
                    if (row.isCurrentLibraryCheck(token) && projectsList.contains(row)) {
                        row.setLibrary(error == null ? state : ScriptLibraryStatusService.LibraryState.UNKNOWN);
                    }
                }));
    }

    /** Whether "Update Script Library…" can start on {@code row} now (VK-1691). */
    private boolean canUpdateLibrary(ProjectViewModel row) {
        return row != null && row.getProjectPath() != null && row.getStatus().isOk() && !launching.get()
                && !choosingEngine.get() && !creating.get() && !creationRunning();
    }

    /** Updates the selected project's script library to its engine's (VK-1691). */
    private void updateLibraryOfSelected() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (!canUpdateLibrary(selected)) {
            return;
        }
        // Like a launch, the flow keeps the window's buttons disabled until it ends (through release()).
        launching.set(true);
        libraryFlow.update(new ScriptLibraryUpdateFlow.Request(selected.getProjectPath(), selected.getEntry(),
                selected.getName()));
    }

    /** Which editor of this window has a project of the row's folder open. */
    private FolderEditor folderEditor(ProjectViewModel row) {
        return folderEditor(row.getProjectPath());
    }

    /** Which editor of this window has a project of {@code project}'s folder open. */
    private FolderEditor folderEditor(Path project) {
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

    /**
     * The empty screen and the no-engine banner (VK-1694): with no projects, the screen offers Create Project and Add
     * Existing… and, when no engine works, Add Engine…; above a list with projects, a banner offers Add Engine…. Nothing
     * is said about the engines before their first check has answered.
     */
    private void updateEmptyState() {
        EmptyStates.EmptyState state = EmptyStates.of(listRead, enginesChecked, projectsList.size(),
                engineOptions);
        showEmptyState(state.showEmptyScreen());
        boolean noEngine = state.problem().noEngine();
        noEngineBox.setVisible(noEngine);
        noEngineBox.setManaged(noEngine);
        noEngineText.setText(EmptyStates.message(state.problem()));
        if (state.engineBanner()) {
            String message = EmptyStates.message(state.problem());
            // Only a new message replaces the banner, so it does not flicker on every engine check.
            if (!message.equals(banners.messageOf(BANNER_NO_ENGINE))) {
                banners.show(BANNER_NO_ENGINE, BannerBar.Kind.WARNING, message, false, "Add Engine…",
                        this::onAddEngine);
            }
        } else {
            banners.hide(BANNER_NO_ENGINE);
        }
    }

    private void showEmptyState(boolean show) {
        emptyStatePane.setVisible(show);
        emptyStatePane.setManaged(show);
        projectsTablePane.setVisible(!show);
        projectsTablePane.setManaged(!show);
    }

    /** The table's placeholder when the search hides nothing (VK-1694). */
    private void setListPlaceholder(Node placeholder) {
        listPlaceholder = placeholder;
        updatePlaceholder();
    }

    /** "No projects match …" while the search hides every listed project; else the list's own placeholder. */
    private void updatePlaceholder() {
        if (tableModel.filtering() && !projectsList.isEmpty() && tableModel.sorted().isEmpty()) {
            projectsTable.setPlaceholder(new Label("No projects match \"" + tableModel.query().strip() + "\""));
        } else {
            projectsTable.setPlaceholder(listPlaceholder);
        }
    }

    /** Selects the row with this id when the search shows it; a hidden row is never selected (VK-1694). */
    private void selectById(UUID id) {
        rowById(id).filter(tableModel::isVisible).ifPresent(row -> projectsTable.getSelectionModel().select(row));
    }

    /**
     * Selects the row with this id and scrolls to it. A project that was just added, created, relinked or put back must
     * be seen, so the search is cleared first when it hides the row (VK-1694).
     */
    private void revealById(UUID id) {
        rowById(id).ifPresent(this::reveal);
    }

    private void revealByPath(Path projectPath) {
        projectsList.stream()
                .filter(row -> projectPath.equals(row.getProjectPath()))
                .findFirst()
                .ifPresent(this::reveal);
    }

    private void reveal(ProjectViewModel row) {
        if (!tableModel.isVisible(row)) {
            searchField.clear();
        }
        projectsTable.getSelectionModel().select(row);
        projectsTable.scrollTo(row);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Actions
    // ---------------------------------------------------------------------------------------------------------------

    /** Pins the selected project to the top of the list, or unpins it (VK-1694). */
    private void togglePinOfSelected() {
        ProjectViewModel selected = projectsTable.getSelectionModel().getSelectedItem();
        if (selected == null || readOnly.get()) {
            return;
        }
        UUID id = selected.getId();
        boolean pin = !selected.isPinned();
        runInBackground(() -> hub.setPinned(id, pin), view -> {
            applySavedView(view);
            // The row moved to its new place in the list; keep it in view.
            rowById(id).filter(tableModel::isVisible).ifPresent(projectsTable::scrollTo);
        }, this::showSaveError);
    }

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
                revealById(id);
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
        showInFolder(selected.getProjectPath());
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
                revealById(id);
            }
            case UNCHANGED -> {
                banners.hide(BANNER_LOCATE);
                revealById(id);
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
        return DialogFactory.confirm(window(), title, message);
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
     * {@link LaunchPlan} decides, and the user is asked when it cannot. {@link OpenProjectFlow} then compares the
     * project with that engine and asks before an upgrade (VK-1690). A project an editor of this window already
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

        String name = entry != null ? entry.displayName()
                : fallbackName != null && !fallbackName.isBlank() ? fallbackName : projectName(projectFile);
        McpSetting mcp = entry == null ? McpSetting.DEFAULT : entry.mcp();

        // Disable UI to prevent double-clicks while the engines are checked and the editor starts; the flow clears it
        // (through OpenServices.start or release) on every path.
        launching.set(true);
        openFlow.open(new OpenProjectFlow.Request(projectFile, entry, name, mcp));
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
        LauncherSettings settings = hub.settings();
        LaunchRequest request = new LaunchRequest(engine, projectFile, name, mcp, settings.showEditorConsole(),
                settings.logRetention());
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
        int pinned = (int) projectsList.stream().filter(row -> row.getEntry().pinned()).count();
        SettingsForm form = new SettingsForm(hub.settings(), hub.canSaveSettings(), readOnly.get(),
                projectsList.size(), pinned, engineOptions);
        SettingsDialog.show(window(), form, hub.sessionLogsDirectory(), this::openLogsFolder)
                .ifPresent(this::applySettings);
    }

    /**
     * Saves what the Settings dialog chose (VK-1694): the settings file first; once it is saved, the list is trimmed to a
     * lowered cap and the default engine is written to the engines file. A failed save changes nothing.
     */
    private void applySettings(SettingsForm.Result result) {
        runInBackground(() -> hub.saveSettings(result.settings()), saved -> {
            banners.hide(BANNER_SETTINGS);
            if (result.projectsToRemove() > 0 && !readOnly.get()) {
                runInBackground(hub::trimProjects, this::applySavedView, this::showSaveError);
            }
            // Its failure shows the engines' own banner.
            result.defaultEngine().ifPresent(change -> engineServices.setDefault(change.engineId().orElse(null)));
        }, error -> banners.show(BANNER_SETTINGS, BannerBar.Kind.ERROR,
                "Could not save the settings, so nothing was changed: " + describe(error), true));
    }

    /** Opens the per-launch logs folder in Explorer, creating it first; off the FX thread, a failure is a banner. */
    private void openLogsFolder() {
        Path folder = hub.sessionLogsDirectory();
        CompletableFuture.runAsync(() -> {
            try {
                Files.createDirectories(folder);
                hub.shell().open(folder);
            } catch (IOException e) {
                throw new CompletionException(e);
            }
        }).whenComplete((ignored, error) -> {
            if (error != null) {
                runLaterQuietly(() -> banners.show(BANNER_SETTINGS, BannerBar.Kind.ERROR,
                        "Could not open the logs folder " + folder + ": " + describe(error), true));
            }
        });
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
            revealById(added.entry().id());
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

    /** What the open flow needs from this window (VK-1690). FX thread, except where a method says otherwise. */
    private final class OpenServices implements OpenProjectFlow.Services {

        @Override
        public CompletableFuture<EngineOptions> engines() {
            // Also refreshes the window's engine column and banners; completes on the FX thread.
            return engineServices.refresh();
        }

        @Override
        public CompletableFuture<ProjectFileCheck> read(Path projectFile, boolean probeLock) {
            return hub.checkProjectFile(projectFile, probeLock);
        }

        @Override
        public FolderEditor folderEditor(Path projectFile) {
            return MainController.this.folderEditor(projectFile);
        }

        @Override
        public boolean readOnly() {
            return readOnly.get();
        }

        @Override
        public Optional<OpenProjectFlow.Picked> chooseEngine(OpenProjectFlow.EnginePrompt prompt) {
            return EngineChooserDialog.showForOpen(window(), prompt.header(), prompt.problem(), prompt.candidates(),
                            prompt.canRemember(), prompt.rememberByDefault(), prompt.offered(), engineServices)
                    .flatMap(choice -> choice.engine()
                            .map(engine -> new OpenProjectFlow.Picked(engine, choice.remember())));
        }

        @Override
        public VersionPrompt.Choice ask(VersionPrompt prompt) {
            return VersionDialog.show(window(), prompt);
        }

        @Override
        public Optional<String> askCopyName(CopyNamePrompt prompt) {
            return CopyNameDialog.show(window(), prompt,
                    name -> ProjectNameRules.checkName(name).map(ProjectNameRules.Problem::message));
        }

        @Override
        public OpenProjectFlow.JobView showJob(String title, String header, Runnable cancel) {
            FileJobDialog dialog = FileJobDialog.show(window(), title, header, cancel);
            return new OpenProjectFlow.JobView() {
                @Override
                public void progress(JobMonitor.Phase phase, long done, long total) {
                    dialog.progress(phase, done, total);
                }

                @Override
                public void close() {
                    dialog.close();
                }
            };
        }

        @Override
        public FileJob<ProjectBackupService.BackupOutcome> startBackup(ProjectBackupService.BackupRequest request,
                                                                       FileJob.ProgressListener listener) {
            // The creation thread runs one disk job at a time, and the launcher's shutdown cancels and waits for it.
            return FileJob.start(creation, monitor -> hub.backUpProject(request, monitor), listener,
                    ProjectBackupService.BackupOutcome.Failed::new);
        }

        @Override
        public FileJob<ProjectCopier.CopyOutcome> startCopy(ProjectCopier.CopyRequest request,
                                                            FileJob.ProgressListener listener) {
            return FileJob.start(creation, monitor -> hub.copyProject(request, monitor), listener,
                    ProjectCopier.CopyOutcome.Failed::new);
        }

        @Override
        public CompletableFuture<AddedProject> listCopy(Path projectFile, String name, Optional<UUID> engineId) {
            CompletableFuture<AddedProject> result = new CompletableFuture<>();
            runInBackground(() -> hub.listCopy(projectFile, name, engineId), result::complete,
                    result::completeExceptionally);
            return result;
        }

        @Override
        public void listed(AddedProject added) {
            applySavedView(added.view());
            revealById(added.entry().id());
        }

        @Override
        public void saveFailed(Throwable error) {
            showSaveError(error);
        }

        @Override
        public void rememberWarned(UUID id) {
            runInBackground(() -> hub.setUnknownVersionWarned(id), MainController.this::applySavedView,
                    MainController.this::showSaveError);
        }

        @Override
        public void start(EngineOption engine, Path projectFile, String name, McpSetting mcp, UUID id,
                          Optional<UUID> bindTo) {
            startEditor(engine, projectFile, name, mcp, id, bindTo);
        }

        @Override
        public void release() {
            launching.set(false);
        }

        @Override
        public void error(String title, String message) {
            showErrorDialog(title, message);
        }

        @Override
        public void notice(String message, Path reveal) {
            if (reveal == null) {
                banners.show(BANNER_VERSION, BannerBar.Kind.INFO, message, true);
            } else {
                banners.show(BANNER_VERSION, BannerBar.Kind.INFO, message, true, "Show in folder",
                        () -> showInFolder(reveal));
            }
        }

        @Override
        public CompletableFuture<Boolean> libraryUpdateInterrupted(Path projectFile, String workingDirectory) {
            return hub.scriptLibraryUpdateInterrupted(projectFile, workingDirectory);
        }

        @Override
        public boolean confirmLibraryRecovery(String name) {
            return confirm("Open Project", "An update of the script library of \"" + name + "\" did not finish. "
                    + "The launcher finishes it (or puts the old library back) before the project opens.\n\n"
                    + "Finish it and open the project?");
        }

        @Override
        public FileJob<ScriptLibraryUpdater.RecoveryOutcome> startLibraryRecovery(Path projectFile,
                                                                                String workingDirectory) {
            return FileJob.start(creation, monitor -> hub.recoverScriptLibrary(projectFile, workingDirectory),
                    FileJob.ProgressListener.NONE, ScriptLibraryUpdater.RecoveryOutcome.Failed::new);
        }
    }

    /** What the script-library update needs from this window (VK-1691). FX thread, except where a method says so. */
    private final class LibraryServices implements ScriptLibraryUpdateFlow.Services {

        @Override
        public CompletableFuture<EngineOptions> engines() {
            return engineServices.refresh();
        }

        @Override
        public CompletableFuture<ProjectFileCheck> read(Path projectFile) {
            return hub.checkProjectFile(projectFile, true);
        }

        @Override
        public FolderEditor folderEditor(Path projectFile) {
            return MainController.this.folderEditor(projectFile);
        }

        @Override
        public Optional<EngineOption> chooseEngine(OpenProjectFlow.EnginePrompt prompt) {
            return EngineChooserDialog.showForOpen(window(), prompt.header(), prompt.problem(), prompt.candidates(),
                            prompt.canRemember(), prompt.rememberByDefault(), prompt.offered(), engineServices)
                    .flatMap(EngineChooserDialog.Choice::engine);
        }

        @Override
        public boolean confirmRecovery(String name, String detail) {
            return confirm(ScriptLibraryUpdateFlow.TITLE, detail + " The launcher finishes it (or puts the old "
                    + "library back) first.\n\nFinish the update of \"" + name + "\" now?");
        }

        @Override
        public Optional<Boolean> ask(LibraryUpdatePrompt prompt) {
            return LibraryUpdateDialog.show(window(), prompt);
        }

        @Override
        public OpenProjectFlow.JobView showJob(String title, String header, Runnable cancel) {
            FileJobDialog dialog = FileJobDialog.show(window(), title, header, ScriptLibraryUpdateFlow::phaseText,
                    cancel);
            return new OpenProjectFlow.JobView() {
                @Override
                public void progress(JobMonitor.Phase phase, long done, long total) {
                    dialog.progress(phase, done, total);
                }

                @Override
                public void close() {
                    dialog.close();
                }
            };
        }

        @Override
        public FileJob<ScriptLibraryUpdater.PreviewOutcome> startPreview(ScriptLibraryUpdater.Request request,
                                                                         FileJob.ProgressListener listener) {
            // The creation thread runs one disk job at a time, and the launcher's shutdown cancels and waits for it.
            return FileJob.start(creation, monitor -> hub.previewScriptLibrary(request, monitor), listener,
                    ScriptLibraryUpdater.PreviewOutcome.Failed::new);
        }

        @Override
        public FileJob<ScriptLibraryUpdater.UpdateOutcome> startUpdate(ScriptLibraryUpdater.Request request,
                                                                       String fingerprint, boolean keepMine,
                                                                       FileJob.ProgressListener listener) {
            return FileJob.start(creation, monitor -> hub.updateScriptLibrary(request, fingerprint, keepMine, monitor),
                    listener, ScriptLibraryUpdater.UpdateOutcome.Failed::new);
        }

        @Override
        public FileJob<ScriptLibraryUpdater.RecoveryOutcome> startRecovery(Path projectFile, String workingDirectory) {
            return FileJob.start(creation, monitor -> hub.recoverScriptLibrary(projectFile, workingDirectory),
                    FileJob.ProgressListener.NONE, ScriptLibraryUpdater.RecoveryOutcome.Failed::new);
        }

        @Override
        public void changed(Path projectFile) {
            for (ProjectViewModel row : projectsList) {
                if (projectFile.equals(row.getProjectPath())) {
                    check(row);
                }
            }
        }

        @Override
        public void release() {
            launching.set(false);
        }

        @Override
        public void error(String title, String message) {
            showErrorDialog(title, message);
        }

        @Override
        public void notice(String message, Path reveal) {
            if (reveal == null) {
                banners.show(BANNER_LIBRARY, BannerBar.Kind.INFO, message, true);
            } else {
                banners.show(BANNER_LIBRARY, BannerBar.Kind.INFO, message, true, "Show backup",
                        () -> showInFolder(reveal));
            }
        }
    }

    /** Opens Explorer on {@code file}'s folder with the file selected; off the FX thread. */
    private void showInFolder(Path file) {
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

    @FXML
    private void onCreateProject() {
        if (creating.get() || readOnly.get()) {
            return;
        }
        Window owner = projectsTable.getScene().getWindow();

        // The dialog checks, creates and shows progress itself (VK-1679); it ends with the creation to follow up on.
        // It reads the engines and their templates itself too (VK-1689).
        List<CreationJob> started = new ArrayList<>();
        Optional<CreateProjectForm.Result> result = CreateProjectDialog.show(owner, createServices(started),
                hub.settings().defaultProjectLocation());
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
        revealByPath(outcome.projectPath());
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
        DialogFactory.error(window(), title, message);
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

    /** "Add Engine…" of the empty screen and of the no-engine banner: the Engines dialog, adding at once (VK-1694). */
    @FXML
    private void onAddEngine() {
        if (launching.get() || choosingEngine.get()) {
            return;
        }
        EnginesDialog.showAdding(window(), engineOptions, engineServices);
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
        }, error -> {
            banners.show(BANNER_ENGINES_CHECK, BannerBar.Kind.ERROR,
                    "The engines could not be checked: " + describe(error) + ". Press Refresh to try again.", true);
            // The empty screen no longer waits for an answer; it says nothing about the engines.
            enginesChecked = true;
            updateEmptyState();
        });
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
        enginesChecked = true;
        // One sort for all the rows, not one per changed row (VK-1694).
        tableModel.batch(() -> {
            for (ProjectViewModel row : projectsList) {
                applyEngineLabel(row);
                // Whether its schema needs a newer engine depends on the engines (VK-1686).
                applyHealth(row);
            }
        });
        updateEmptyState();
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
