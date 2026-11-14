package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import javafx.beans.binding.Bindings;
import javafx.beans.binding.BooleanBinding;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ReadOnlyObjectWrapper;
import javafx.beans.property.ReadOnlyStringWrapper;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.geometry.Insets;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.TableCell;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.FileChooser;
import javafx.stage.Window;

import java.io.File;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;
import java.util.function.Consumer;

/**
 * The Engines dialog (VK-1681): every registered engine with its health, the {@code VERTEXFORGE_EDITOR_PATH} override,
 * and Add Engine…, Remove, Set as Default and Refresh. Removing only unregisters an engine; its files are never
 * touched. Nothing here touches the file system on the FX thread.
 */
public final class EnginesDialog {

    private final Dialog<ButtonType> dialog = new Dialog<>();
    private final EngineServices services;
    private final TableView<EngineOption> table = new TableView<>();
    private final Label overrideLabel = new Label();
    private final Label readOnlyLabel = new Label();
    private final Label messageLabel = new Label();
    private final BooleanProperty busy = new SimpleBooleanProperty(false);
    private final BooleanProperty readOnly = new SimpleBooleanProperty(false);

    private EnginesDialog(Window owner, EngineOptions current, EngineServices services) {
        this.services = Objects.requireNonNull(services, "services");
        dialog.setTitle("Engines");
        dialog.setHeaderText("VertexForge engine installs");
        dialog.initOwner(owner);
        dialog.setResizable(true);

        DialogPane pane = new DialogPane();
        DialogStyle.apply(pane);
        pane.getStyleClass().add("engines-dialog");
        pane.setContent(createContent());
        pane.getButtonTypes().add(ButtonType.CLOSE);
        dialog.setDialogPane(pane);

        if (current != null) {
            showOptions(current);
        }
    }

    /** Shows the dialog over {@code owner} and waits until it closes. {@code current} may be {@code null}. */
    public static void show(Window owner, EngineOptions current, EngineServices services) {
        EnginesDialog engines = new EnginesDialog(owner, current, services);
        // A fresh check: an engine may have been built, moved or deleted since the window last looked.
        engines.run(services.refresh(), engines::showOptions);
        engines.dialog.showAndWait();
    }

    private VBox createContent() {
        table.setPlaceholder(new Label("Checking the engines…"));
        table.setPrefHeight(260);
        table.setPrefWidth(760);
        table.setColumnResizePolicy(TableView.CONSTRAINED_RESIZE_POLICY_FLEX_LAST_COLUMN);

        TableColumn<EngineOption, String> engineColumn = new TableColumn<>("Engine");
        engineColumn.setPrefWidth(230);
        engineColumn.setCellValueFactory(cell -> new ReadOnlyStringWrapper(cell.getValue().label()));

        TableColumn<EngineOption, EngineHealth> statusColumn = new TableColumn<>("Status");
        statusColumn.setPrefWidth(90);
        statusColumn.setCellValueFactory(cell -> new ReadOnlyObjectWrapper<>(cell.getValue().health()));
        statusColumn.setCellFactory(column -> new TableCell<>() {
            @Override
            protected void updateItem(EngineHealth health, boolean empty) {
                super.updateItem(health, empty);
                if (empty || health == null) {
                    setText(null);
                    setTooltip(null);
                } else {
                    setText(health.label());
                    setTooltip(health.reason().isEmpty() ? null : new Tooltip(health.reason()));
                }
            }
        });

        TableColumn<EngineOption, String> pathColumn = new TableColumn<>("Path");
        pathColumn.setPrefWidth(420);
        pathColumn.setCellValueFactory(cell -> new ReadOnlyStringWrapper(cell.getValue().pathText()));

        table.getColumns().add(engineColumn);
        table.getColumns().add(statusColumn);
        table.getColumns().add(pathColumn);

        Button addButton = new Button("Add Engine…");
        Button removeButton = new Button("Remove");
        Button defaultButton = new Button("Set as Default");
        Button refreshButton = new Button("Refresh");
        addButton.setOnAction(event -> onAdd());
        removeButton.setOnAction(event -> onRemove());
        defaultButton.setOnAction(event -> onSetDefault());
        refreshButton.setOnAction(event -> run(services.refresh(), this::showOptions));

        var selected = table.getSelectionModel().selectedItemProperty();
        BooleanBinding noSavedSelection = Bindings.createBooleanBinding(
                () -> selected.get() == null || selected.get().id().isEmpty(), selected);
        // The running launcher's own bundled engine would only be registered again at the next start.
        BooleanBinding bundledSelected = Bindings.createBooleanBinding(
                () -> selected.get() != null && selected.get().bundled(), selected);
        BooleanBinding cannotBeDefault = Bindings.createBooleanBinding(
                () -> selected.get() == null || selected.get().isDefault() || !selected.get().isOk(), selected);
        addButton.disableProperty().bind(busy.or(readOnly));
        removeButton.disableProperty().bind(busy.or(readOnly).or(noSavedSelection).or(bundledSelected));
        defaultButton.disableProperty().bind(busy.or(readOnly).or(noSavedSelection).or(cannotBeDefault));
        refreshButton.disableProperty().bind(busy);

        HBox actions = new HBox(8, addButton, removeButton, defaultButton, refreshButton);
        actions.getStyleClass().add("engine-actions");

        for (Label label : new Label[]{overrideLabel, readOnlyLabel, messageLabel}) {
            label.setWrapText(true);
            label.setMaxWidth(760);
            label.visibleProperty().bind(label.textProperty().isNotEmpty());
            label.managedProperty().bind(label.visibleProperty());
        }
        overrideLabel.getStyleClass().add("engine-problem");
        readOnlyLabel.getStyleClass().add("engine-problem");
        messageLabel.getStyleClass().add("engine-note");

        Label hint = new Label("Add an engine by choosing its Editor.exe or its " + EngineManifest.FILE_NAME
                + ". Removing an engine only takes it off this list; its files are never touched.");
        hint.setWrapText(true);
        hint.setMaxWidth(760);
        hint.getStyleClass().add("engine-note");

        VBox content = new VBox(10, overrideLabel, readOnlyLabel, table, actions, messageLabel, hint);
        VBox.setVgrow(table, Priority.ALWAYS);
        content.setPadding(new Insets(16));
        return content;
    }

    private void showOptions(EngineOptions options) {
        UUID selectedId = Optional.ofNullable(table.getSelectionModel().getSelectedItem())
                .flatMap(EngineOption::id)
                .orElse(null);
        table.getItems().setAll(options.engines());
        table.setPlaceholder(new Label("No engine is registered yet. Use Add Engine…"));
        if (selectedId != null) {
            select(selectedId);
        }

        readOnly.set(options.registry().readOnly());
        readOnlyLabel.setText(options.registry().readOnly()
                ? "The engine list cannot be changed here (" + describeStatus(options) + "). Its engines can still"
                        + " be used."
                : "");

        overrideLabel.setText(options.override().map(EnginesDialog::describeOverride).orElse(""));
    }

    private static String describeStatus(EngineOptions options) {
        return switch (options.registry().status()) {
            case NEWER_READ_ONLY -> "it was saved by a newer version of the launcher";
            case CORRUPT, CORRUPT_UNMOVABLE -> "the file is damaged: " + options.registry().detail();
            case UNREADABLE -> "the file could not be read: " + options.registry().detail();
            default -> options.registry().status().name();
        };
    }

    private static String describeOverride(EngineOption override) {
        return override.isOk()
                ? EditorOverride.ENV_VAR + " is set: every project opens with " + override.pathText() + " ("
                        + override.versionText() + ")."
                : EditorOverride.ENV_VAR + " is set to \"" + override.rawPath() + "\" but is ignored: "
                        + override.health().reason() + ".";
    }

    private void select(UUID id) {
        for (EngineOption option : table.getItems()) {
            if (option.id().equals(Optional.of(id))) {
                table.getSelectionModel().select(option);
                table.scrollTo(option);
                return;
            }
        }
    }

    private void onAdd() {
        Window window = dialog.getDialogPane().getScene().getWindow();
        File file = chooseEngineFile(window);
        if (file == null) {
            return;
        }
        messageLabel.setText("Checking " + file + "…");
        run(services.add(file.toPath()), outcome -> {
            if (outcome.isRefused()) {
                messageLabel.setText("Not added: " + outcome.refusal());
                return;
            }
            showOptions(outcome.options());
            outcome.engineId().ifPresent(this::select);
            messageLabel.setText(outcome.existed() ? "This engine is registered already." : "Engine added.");
        });
    }

    /** The Add Engine file picker; shared with the engine chooser. */
    static File chooseEngineFile(Window owner) {
        FileChooser chooser = new FileChooser();
        chooser.setTitle("Add Engine: choose Editor.exe or " + EngineManifest.FILE_NAME);
        chooser.getExtensionFilters().addAll(
                new FileChooser.ExtensionFilter("Editor or engine manifest (*.exe, *.json)", "*.exe", "*.json"),
                new FileChooser.ExtensionFilter("All files", "*.*"));
        return chooser.showOpenDialog(owner);
    }

    private void onRemove() {
        EngineOption selected = table.getSelectionModel().getSelectedItem();
        if (selected == null || selected.id().isEmpty()) {
            return;
        }
        UUID id = selected.id().get();
        int bound = services.projectsUsing(id);
        String usage = bound == 0
                ? "No listed project uses it."
                : bound + (bound == 1 ? " listed project uses it" : " listed projects use it")
                        + "; they will ask for another engine the next time they are opened.";
        Alert confirm = DialogStyle.alert(Alert.AlertType.CONFIRMATION, dialog.getDialogPane().getScene().getWindow(),
                "Remove Engine", "Remove " + selected.label() + " (" + selected.pathText() + ") from the list?\n\n"
                        + usage + "\nThe engine's files are not touched.");
        Optional<ButtonType> answer = confirm.showAndWait();
        if (answer.isEmpty() || answer.get() != ButtonType.OK) {
            return;
        }
        run(services.remove(id), options -> {
            showOptions(options);
            messageLabel.setText("Engine removed from the list.");
        });
    }

    private void onSetDefault() {
        EngineOption selected = table.getSelectionModel().getSelectedItem();
        if (selected == null || selected.id().isEmpty()) {
            return;
        }
        run(services.setDefault(selected.id().get()), options -> {
            showOptions(options);
            messageLabel.setText(selected.versionText() + " is now the default engine for projects that have none.");
        });
    }

    /** Runs one background request at a time; its result (or failure) is shown on the FX thread. */
    private <T> void run(CompletableFuture<T> request, Consumer<T> onResult) {
        busy.set(true);
        request.whenComplete((value, error) -> {
            busy.set(false);
            if (error != null) {
                messageLabel.setText("Failed: " + describe(error));
                return;
            }
            onResult.accept(value);
        });
    }

    static String describe(Throwable error) {
        Throwable cause = error;
        while ((cause instanceof CompletionException || cause instanceof ExecutionException)
                && cause.getCause() != null) {
            cause = cause.getCause();
        }
        return cause.getMessage() != null ? cause.getMessage() : cause.getClass().getSimpleName();
    }
}
