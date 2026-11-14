package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryCodec;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.geometry.Insets;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.ListCell;
import javafx.scene.control.ListView;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;

/**
 * Asks which engine to use (VK-1681): when Open cannot decide on its own (the project's engine is broken, missing or
 * unknown, or there are several engines and no default), and for "Change Engine…". The user can add an engine from
 * here too. Nothing here touches the file system on the FX thread.
 */
public final class EngineChooserDialog {

    /**
     * What the user chose.
     *
     * @param engine   the engine; empty for "Use the default engine" (Change Engine… only), which unbinds the project
     * @param remember whether to bind the project to {@code engine}
     */
    public record Choice(Optional<EngineOption> engine, boolean remember) {
        public Choice {
            engine = engine == null ? Optional.empty() : engine;
        }
    }

    /** How the chooser is used. */
    public enum Mode {
        /** Opening a project: offers "Use this engine for this project from now on". */
        OPEN,
        /** Change Engine…: the choice is always remembered, and "Use the default engine" is offered. */
        CHANGE
    }

    /** One row: an engine, or "Use the default engine" ({@code option == null}). */
    private record Row(EngineOption option) {
    }

    private static final ButtonType CHOOSE = new ButtonType("Open", ButtonBar.ButtonData.OK_DONE);
    private static final ButtonType USE = new ButtonType("Use", ButtonBar.ButtonData.OK_DONE);

    private final Dialog<Choice> dialog = new Dialog<>();
    private final Mode mode;
    private final EngineServices services;
    private final ListView<Row> list = new ListView<>();
    private final CheckBox rememberBox = new CheckBox("Use this engine for this project from now on");
    private final Label messageLabel = new Label();
    private final BooleanProperty busy = new SimpleBooleanProperty(false);
    private final boolean canRemember;

    /**
     * @param problem           why the launcher asks; shown at the top
     * @param candidates        the engines that work
     * @param canRemember       whether the project's binding may be written (it is listed and the list is writable)
     * @param rememberByDefault whether the remember box starts ticked
     * @param currentEngineId   the project's current binding (Change Engine…), preselected; or {@code null}
     */
    private EngineChooserDialog(Window owner, Mode mode, String title, String problem, List<EngineOption> candidates,
                                boolean canRemember, boolean rememberByDefault, String currentEngineId,
                                EngineServices services) {
        this.mode = Objects.requireNonNull(mode, "mode");
        this.services = Objects.requireNonNull(services, "services");
        this.canRemember = canRemember;
        dialog.setTitle(title);
        dialog.setHeaderText(mode == Mode.OPEN ? "Choose an engine" : "Choose the engine for this project");
        dialog.initOwner(owner);
        dialog.setResizable(true);

        ButtonType confirm = mode == Mode.OPEN ? CHOOSE : USE;
        DialogPane pane = new DialogPane();
        DialogStyle.apply(pane);
        pane.getStyleClass().add("engine-chooser-dialog");
        pane.getButtonTypes().addAll(confirm, ButtonType.CANCEL);
        pane.setContent(createContent(problem));
        dialog.setDialogPane(pane);

        Node confirmButton = pane.lookupButton(confirm);
        confirmButton.disableProperty().bind(busy.or(list.getSelectionModel().selectedItemProperty().isNull()));

        rememberBox.setSelected(canRemember && rememberByDefault);
        rememberBox.setVisible(mode == Mode.OPEN && canRemember);
        rememberBox.setManaged(rememberBox.isVisible());
        // An engine that could not be saved (no id) can be used, but not bound.
        list.getSelectionModel().selectedItemProperty().addListener((observable, was, now) ->
                rememberBox.setDisable(now == null || now.option() == null || now.option().id().isEmpty()));

        setRows(candidates, mode == Mode.CHANGE ? EngineRegistryCodec.uuid(currentEngineId) : Optional.empty(),
                mode == Mode.CHANGE && currentEngineId == null);

        dialog.setResultConverter(button -> {
            if (button != confirm) {
                return null;
            }
            Row row = list.getSelectionModel().getSelectedItem();
            if (row == null) {
                return null;
            }
            Optional<EngineOption> engine = Optional.ofNullable(row.option());
            boolean remember = mode == Mode.CHANGE
                    || (this.canRemember && rememberBox.isSelected() && engine.flatMap(EngineOption::id).isPresent());
            return new Choice(engine, remember);
        });
    }

    /** Asks which engine opens a project; empty when the user cancelled. */
    public static Optional<Choice> showForOpen(Window owner, String problem, List<EngineOption> candidates,
                                               boolean canRemember, boolean rememberByDefault,
                                               EngineServices services) {
        return new EngineChooserDialog(owner, Mode.OPEN, "Open Project", problem, candidates, canRemember,
                rememberByDefault, null, services).dialog.showAndWait();
    }

    /** Asks which engine a project is bound to from now on; empty when the user cancelled. */
    public static Optional<Choice> showForChange(Window owner, String projectName, EngineOptions options,
                                                 String currentEngineId, EngineServices services) {
        return new EngineChooserDialog(owner, Mode.CHANGE, "Change Engine",
                "Which engine should open \"" + projectName + "\"?", bindable(options), true, true,
                currentEngineId, services).dialog.showAndWait();
    }

    /** The engines a project can be bound to: those that work and are saved (they have an id). */
    private static List<EngineOption> bindable(EngineOptions options) {
        return options.usable().stream().filter(engine -> engine.id().isPresent()).toList();
    }

    private VBox createContent(String problem) {
        Label problemLabel = new Label(problem);
        problemLabel.setWrapText(true);
        problemLabel.setMaxWidth(640);
        problemLabel.getStyleClass().add("engine-problem");
        problemLabel.setVisible(!problem.isEmpty());
        problemLabel.setManaged(!problem.isEmpty());

        list.setPrefSize(640, 200);
        list.setPlaceholder(new Label("No working engine. Use Add Engine…"));
        list.setCellFactory(view -> new ListCell<>() {
            @Override
            protected void updateItem(Row row, boolean empty) {
                super.updateItem(row, empty);
                if (empty || row == null) {
                    setText(null);
                } else if (row.option() == null) {
                    setText("Use the default engine (the project is bound to it when it next opens)");
                } else {
                    setText(row.option().label() + "    " + row.option().pathText());
                }
            }
        });

        Button addButton = new Button("Add Engine…");
        addButton.setOnAction(event -> onAdd());
        addButton.disableProperty().bind(busy);
        HBox actions = new HBox(8, addButton);
        actions.getStyleClass().add("engine-actions");

        messageLabel.setWrapText(true);
        messageLabel.setMaxWidth(640);
        messageLabel.getStyleClass().add("engine-note");
        messageLabel.visibleProperty().bind(messageLabel.textProperty().isNotEmpty());
        messageLabel.managedProperty().bind(messageLabel.visibleProperty());

        VBox content = new VBox(10, problemLabel, list, rememberBox, actions, messageLabel);
        VBox.setVgrow(list, Priority.ALWAYS);
        content.setPadding(new Insets(16));
        return content;
    }

    /**
     * @param select       the engine to select, if listed
     * @param selectDefault whether to select the "Use the default engine" row
     */
    private void setRows(List<EngineOption> candidates, Optional<UUID> select, boolean selectDefault) {
        List<Row> rows = new ArrayList<>();
        if (mode == Mode.CHANGE) {
            rows.add(new Row(null));
        }
        for (EngineOption candidate : candidates) {
            rows.add(new Row(candidate));
        }
        list.getItems().setAll(rows);

        Row toSelect = null;
        for (Row row : rows) {
            if (row.option() == null ? selectDefault
                    : select.isPresent() && row.option().id().equals(select)) {
                toSelect = row;
                break;
            }
        }
        if (toSelect == null && mode == Mode.OPEN && rows.size() == 1) {
            toSelect = rows.getFirst();
        }
        if (toSelect != null) {
            list.getSelectionModel().select(toSelect);
        }
    }

    private void onAdd() {
        File file = EnginesDialog.chooseEngineFile(dialog.getDialogPane().getScene().getWindow());
        if (file == null) {
            return;
        }
        messageLabel.setText("Checking " + file + "…");
        busy.set(true);
        CompletableFuture<EngineServices.AddOutcome> request = services.add(file.toPath());
        request.whenComplete((outcome, error) -> {
            busy.set(false);
            if (error != null) {
                messageLabel.setText("Failed: " + EnginesDialog.describe(error));
                return;
            }
            if (outcome.isRefused()) {
                messageLabel.setText("Not added: " + outcome.refusal());
                return;
            }
            List<EngineOption> candidates = mode == Mode.CHANGE
                    ? bindable(outcome.options())
                    : outcome.options().usable();
            setRows(candidates, outcome.engineId(), false);
            messageLabel.setText(outcome.existed() ? "This engine is registered already." : "Engine added.");
        });
    }

}
