package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.model.McpSetting;
import javafx.beans.binding.Bindings;
import javafx.beans.binding.BooleanBinding;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.TextField;
import javafx.scene.layout.HBox;
import javafx.scene.layout.VBox;
import javafx.stage.Window;

import java.util.Optional;
import java.util.OptionalInt;

/**
 * "Open with MCP" for one project (VK-1683): whether its editor starts with its MCP server on, and on which port.
 * Two projects open at the same time need different ports.
 */
public final class McpDialog {

    private static final ButtonType SAVE = new ButtonType("Save", ButtonBar.ButtonData.OK_DONE);

    private McpDialog() {
        // Shown through show()
    }

    /** Asks for the project's setting; empty when the user cancelled. */
    public static Optional<McpSetting> show(Window owner, String projectName, McpSetting current) {
        Dialog<McpSetting> dialog = new Dialog<>();
        dialog.setTitle("MCP");
        dialog.setHeaderText("MCP for \"" + projectName + "\"");
        dialog.initOwner(owner);

        DialogPane pane = new DialogPane();
        DialogStyle.apply(pane);
        pane.getButtonTypes().addAll(SAVE, ButtonType.CANCEL);

        CheckBox enabledBox = new CheckBox("Open with MCP, so an AI agent can control the editor");
        enabledBox.setSelected(current.enabled());

        Label portLabel = new Label("Port:");
        portLabel.getStyleClass().add("form-label");
        TextField portField = new TextField(Integer.toString(current.port()));
        portField.setPrefColumnCount(6);
        HBox portRow = new HBox(8, portLabel, portField);
        portRow.setAlignment(Pos.CENTER_LEFT);

        Label error = new Label("The port must be a number from " + McpSetting.MIN_PORT + " to "
                + McpSetting.MAX_PORT + ".");
        error.getStyleClass().add("validation-error");

        Label note = new Label();
        note.setWrapText(true);
        note.setMaxWidth(520);
        note.getStyleClass().add("dialog-note");
        note.textProperty().bind(Bindings.createStringBinding(() -> {
            OptionalInt port = McpSetting.parsePort(portField.getText());
            String shown = port.isPresent() ? Integer.toString(port.getAsInt()) : "<port>";
            return "The editor starts with --mcp --mcp-port " + shown + ". Connect Claude Code with:\n"
                    + "claude mcp add --transport http vertexforge http://127.0.0.1:" + shown + "/mcp\n"
                    + "A token is never passed on the command line (other programs can read it); set one in the"
                    + " editor's Preferences > AI / MCP if you want one.";
        }, portField.textProperty()));

        BooleanBinding portInvalid = Bindings.createBooleanBinding(
                () -> McpSetting.parsePort(portField.getText()).isEmpty(), portField.textProperty());
        error.visibleProperty().bind(portInvalid);
        error.managedProperty().bind(portInvalid);

        VBox content = new VBox(10, enabledBox, portRow, error, note);
        content.setPadding(new Insets(16));
        pane.setContent(content);
        dialog.setDialogPane(pane);

        Node saveButton = pane.lookupButton(SAVE);
        saveButton.disableProperty().bind(portInvalid);

        dialog.setResultConverter(button -> {
            if (button != SAVE) {
                return null;
            }
            OptionalInt port = McpSetting.parsePort(portField.getText());
            return port.isPresent() ? new McpSetting(enabledBox.isSelected(), port.getAsInt()) : null;
        });
        return dialog.showAndWait();
    }
}
