package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.service.JobMonitor;
import javafx.application.Platform;
import javafx.geometry.Insets;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressBar;
import javafx.scene.layout.VBox;
import javafx.stage.Modality;
import javafx.stage.Stage;
import javafx.stage.Window;

import java.util.Locale;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Shows a backup or a copy while it runs (VK-1690): a progress bar, how far it got, and Cancel. It is a window of its
 * own rather than a {@code Dialog}, so that Cancel and the window's close button only ask the job to stop (the job
 * cleans up first) and only {@link #close()} closes it.
 * <p>
 * The job reports from its own thread; reports are merged so the FX thread shows at most one per pulse.
 */
public final class FileJobDialog {

    /** One report from the job. */
    private record Progress(JobMonitor.Phase phase, long done, long total) {
    }

    private final Stage stage = new Stage();
    private final ProgressBar bar = new ProgressBar(ProgressBar.INDETERMINATE_PROGRESS);
    private final Label status = new Label("Starting…");
    private final AtomicReference<Progress> pending = new AtomicReference<>();
    private final Runnable cancel;
    private Button cancelButton;
    private boolean cancelling;
    private boolean closed;

    private FileJobDialog(Runnable cancel) {
        this.cancel = cancel;
    }

    /**
     * Shows the window and returns at once. FX thread.
     *
     * @param cancel asks the job to stop; called at most once
     */
    public static FileJobDialog show(Window owner, String title, String header, Runnable cancel) {
        Objects.requireNonNull(cancel, "cancel");
        FileJobDialog dialog = new FileJobDialog(cancel);
        dialog.build(owner, title, header);
        dialog.stage.show();
        return dialog;
    }

    private void build(Window owner, String title, String header) {
        DialogPane pane = new DialogPane();
        DialogStyle.apply(pane);
        pane.getStyleClass().add("file-job-dialog");
        pane.setHeaderText(header);
        pane.getButtonTypes().add(ButtonType.CANCEL);
        bar.setPrefWidth(480);
        status.setWrapText(true);
        status.setMaxWidth(480);
        VBox content = new VBox(10, bar, status);
        content.setPadding(new Insets(16));
        pane.setContent(content);

        // Outside a Dialog a pane's buttons do nothing on their own.
        cancelButton = (Button) pane.lookupButton(ButtonType.CANCEL);
        cancelButton.setOnAction(event -> requestCancel());

        stage.setTitle(title);
        if (owner != null) {
            stage.initOwner(owner);
        }
        stage.initModality(Modality.WINDOW_MODAL);
        stage.setResizable(false);
        stage.setScene(new Scene(pane));
        stage.setOnCloseRequest(event -> {
            event.consume();
            requestCancel();
        });
    }

    /** Reports how far the job got. Any thread. */
    public void progress(JobMonitor.Phase phase, long done, long total) {
        if (pending.getAndSet(new Progress(phase, done, total)) == null) {
            try {
                Platform.runLater(this::showPending);
            } catch (IllegalStateException e) {
                // The toolkit is gone: the launcher is closing.
            }
        }
    }

    /** Closes the window; the job has ended. FX thread. */
    public void close() {
        closed = true;
        stage.close();
    }

    private void requestCancel() {
        if (cancelling || closed) {
            return;
        }
        cancelling = true;
        cancelButton.setDisable(true);
        status.setText("Cancelling…");
        cancel.run();
    }

    private void showPending() {
        Progress progress = pending.getAndSet(null);
        if (progress == null || closed || cancelling) {
            return;
        }
        boolean bytes = progress.phase() != JobMonitor.Phase.SCANNING;
        if (progress.total() > 0) {
            bar.setProgress(Math.min(1.0, (double) progress.done() / progress.total()));
            status.setText(phaseText(progress.phase()) + ": " + (bytes
                    ? sizeText(progress.done()) + " of " + sizeText(progress.total())
                    : progress.done() + " of " + progress.total()));
        } else {
            bar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
            status.setText(phaseText(progress.phase()) + (bytes ? ": " + sizeText(progress.done())
                    : ": " + progress.done() + " found"));
        }
    }

    private static String phaseText(JobMonitor.Phase phase) {
        return switch (phase) {
            case SCANNING -> "Looking at the project's files";
            case WRITING -> "Writing the backup";
            case VERIFYING -> "Checking the backup";
            case COPYING -> "Copying";
        };
    }

    /** {@code 512 bytes}, {@code 1.5 KB}, {@code 3.2 GB} (1 KB = 1024 bytes). */
    static String sizeText(long bytes) {
        if (bytes < 1024) {
            return bytes + (bytes == 1 ? " byte" : " bytes");
        }
        String[] units = {"KB", "MB", "GB", "TB", "PB", "EB"};
        double value = bytes;
        int unit = -1;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            unit++;
        }
        return String.format(Locale.ROOT, "%.1f %s", value, units[unit]);
    }
}
