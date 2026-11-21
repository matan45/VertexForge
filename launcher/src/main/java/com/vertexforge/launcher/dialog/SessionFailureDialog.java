package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.session.CrashReports.CrashReport;
import com.vertexforge.launcher.session.SessionReport;
import javafx.event.ActionEvent;
import javafx.geometry.Insets;
import javafx.scene.Node;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Dialog;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.TextArea;
import javafx.scene.input.Clipboard;
import javafx.scene.input.ClipboardContent;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.Modality;
import javafx.stage.Window;

import java.util.Objects;

/**
 * Why an editor the launcher started failed (VK-1683): the exit code and the editor's reason, the newest crash report
 * from its {@code crashes/} folder, and the last lines of its per-launch log, with Open log, Open crash folder and Copy.
 * <p>
 * It is not modal, so a failure that arrives while another dialog is open never blocks that dialog, and several
 * failures each get their own window. Its buttons keep it open; only Close (or X) closes it.
 */
public final class SessionFailureDialog {

    /** What the buttons do; each call returns at once (the work runs in the background). */
    public interface Actions {
        void openLog(SessionReport report);

        void openCrashFolder(SessionReport report);
    }

    private static final ButtonType OPEN_LOG = new ButtonType("Open log", ButtonBar.ButtonData.LEFT);
    private static final ButtonType OPEN_CRASH_FOLDER = new ButtonType("Open crash folder", ButtonBar.ButtonData.LEFT);
    private static final ButtonType COPY = new ButtonType("Copy", ButtonBar.ButtonData.LEFT);

    private SessionFailureDialog() {
        // Shown through show()
    }

    /** Shows the dialog for {@code report} and returns at once. FX thread. */
    public static void show(Window owner, String projectName, SessionReport report, Actions actions) {
        Objects.requireNonNull(report, "report");
        Objects.requireNonNull(actions, "actions");

        Dialog<Void> dialog = DialogFactory.dialog(owner, "Editor Failed");
        dialog.setHeaderText(report.outcome().headline(projectName));
        dialog.initModality(Modality.NONE);
        dialog.setResizable(true);

        DialogPane pane = DialogFactory.pane();
        pane.getStyleClass().add("session-failure-dialog");
        pane.getButtonTypes().addAll(OPEN_LOG, OPEN_CRASH_FOLDER, COPY, ButtonType.CLOSE);
        pane.setContent(createContent(report));
        dialog.setDialogPane(pane);

        String text = copyText(projectName, report);
        keepOpen(pane, OPEN_LOG, () -> actions.openLog(report));
        keepOpen(pane, OPEN_CRASH_FOLDER, () -> actions.openCrashFolder(report));
        keepOpen(pane, COPY, () -> {
            ClipboardContent content = new ClipboardContent();
            content.putString(text);
            Clipboard.getSystemClipboard().setContent(content);
        });

        dialog.show();
    }

    /** Runs {@code action} for the button and consumes the event, so the dialog stays open. */
    private static void keepOpen(DialogPane pane, ButtonType type, Runnable action) {
        Node button = pane.lookupButton(type);
        button.addEventFilter(ActionEvent.ACTION, event -> {
            event.consume();
            action.run();
        });
    }

    private static VBox createContent(SessionReport report) {
        Label details = new Label(report.outcome().details());
        details.setWrapText(true);
        details.setMaxWidth(720);
        details.getStyleClass().add("failure-summary");

        Label logLabel = new Label("Log: " + report.log());
        logLabel.setWrapText(true);
        logLabel.setMaxWidth(720);
        logLabel.getStyleClass().add("dialog-note");

        TextArea area = new TextArea(detailsText(report));
        area.setEditable(false);
        area.setWrapText(false);
        area.setPrefColumnCount(100);
        area.setPrefRowCount(20);
        area.getStyleClass().add("failure-details");
        VBox.setVgrow(area, Priority.ALWAYS);

        VBox content = new VBox(10, details, area, logLabel);
        content.setPadding(new Insets(16));
        return content;
    }

    /** The crash report (when there is one), then the log's last lines. */
    static String detailsText(SessionReport report) {
        StringBuilder text = new StringBuilder();
        if (report.crashReport().isPresent()) {
            CrashReport crash = report.crashReport().get();
            text.append("Crash report ").append(crash.text()).append('\n');
            crash.dump().ifPresent(dump -> text.append("Crash dump   ").append(dump).append('\n'));
            text.append('\n').append(crash.content().strip()).append("\n\n");
        }
        if (report.logTail().isEmpty()) {
            text.append("(The log has no output from the editor.)");
        } else {
            text.append("Last ").append(report.logTail().size()).append(" lines of the log:\n");
            for (String line : report.logTail()) {
                text.append(line).append('\n');
            }
        }
        return text.toString();
    }

    /** Everything the dialog shows, as plain text for a bug report. */
    static String copyText(String projectName, SessionReport report) {
        return report.outcome().headline(projectName) + "\n"
                + report.outcome().details() + "\n"
                + "Log: " + report.log() + "\n"
                + "Working directory: " + report.workingDirectory() + "\n\n"
                + detailsText(report);
    }
}
