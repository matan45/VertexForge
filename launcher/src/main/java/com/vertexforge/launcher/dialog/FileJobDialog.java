package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.service.JobMonitor;
import javafx.application.Platform;
import javafx.geometry.Insets;
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
import java.util.function.Function;

/**
 * Shows a backup, a copy (VK-1690) or a script-library update (VK-1691) while it runs: a progress bar, how far it got, and Cancel. It is a window of its
 * own rather than a {@code Dialog}, so that Cancel and the window's close button only ask the job to stop (the job
 * cleans up first) and only {@link #close()} closes it.
 * <p>
 * The job reports from its own thread; reports are merged so the FX thread shows at most one per pulse.
 */
public final class FileJobDialog {

    /** One report from the job. */
    private record Progress(JobMonitor.Phase phase, long done, long total) {
    }

    /** Built by {@link #build}, before anything else uses it. */
    private Stage stage;
    private final ProgressBar bar = new ProgressBar(ProgressBar.INDETERMINATE_PROGRESS);
    private final Label status = new Label("Starting…");
    private final AtomicReference<Progress> pending = new AtomicReference<>();
    private final Runnable cancel;
    private final Function<JobMonitor.Phase, String> phaseTexts;
    private Button cancelButton;
    private boolean cancelling;
    private boolean committing;
    private boolean closed;

    private FileJobDialog(Runnable cancel, Function<JobMonitor.Phase, String> phaseTexts) {
        this.cancel = cancel;
        this.phaseTexts = phaseTexts;
    }

    /**
     * Shows the window for a backup or a copy and returns at once. FX thread.
     *
     * @param cancel asks the job to stop; called at most once
     */
    public static FileJobDialog show(Window owner, String title, String header, Runnable cancel) {
        return show(owner, title, header, FileJobDialog::backupPhaseText, cancel);
    }

    /**
     * Shows the window and returns at once, naming each phase with {@code phaseTexts} (VK-1691). Once the job reports
     * {@link JobMonitor.Phase#COMMITTING}, Cancel is disabled: the job can no longer stop. FX thread.
     *
     * @param cancel asks the job to stop; called at most once
     */
    public static FileJobDialog show(Window owner, String title, String header,
                                     Function<JobMonitor.Phase, String> phaseTexts, Runnable cancel) {
        Objects.requireNonNull(phaseTexts, "phaseTexts");
        Objects.requireNonNull(cancel, "cancel");
        FileJobDialog dialog = new FileJobDialog(cancel, phaseTexts);
        dialog.build(owner, title, header);
        dialog.stage.show();
        return dialog;
    }

    private void build(Window owner, String title, String header) {
        DialogPane pane = DialogFactory.pane();
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

        stage = DialogFactory.stage(owner, Modality.WINDOW_MODAL, title, pane);
        stage.setResizable(false);
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
        if (cancelling || committing || closed) {
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
        if (progress.phase() == JobMonitor.Phase.COMMITTING && !committing) {
            committing = true;
            cancelButton.setDisable(true);
        }
        boolean bytes = progress.phase() != JobMonitor.Phase.SCANNING
                && progress.phase() != JobMonitor.Phase.COMMITTING;
        if (progress.total() > 0) {
            bar.setProgress(Math.min(1.0, (double) progress.done() / progress.total()));
            status.setText(phaseTexts.apply(progress.phase()) + ": " + (bytes
                    ? sizeText(progress.done()) + " of " + sizeText(progress.total())
                    : progress.done() + " of " + progress.total()));
        } else {
            bar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
            // A step whose size is not known and that has counted nothing yet (VK-1695: Windows checking whether a
            // folder can go to the Recycle Bin) shows just its name.
            boolean nothingCounted = bytes && progress.done() <= 0;
            status.setText(phaseTexts.apply(progress.phase()) + (nothingCounted ? ""
                    : bytes ? ": " + sizeText(progress.done()) : ": " + progress.done() + " found"));
        }
    }

    /** The phase texts of a project backup or copy (VK-1690). */
    static String backupPhaseText(JobMonitor.Phase phase) {
        return switch (phase) {
            case SCANNING -> "Looking at the project's files";
            case WRITING -> "Writing the backup";
            case VERIFYING -> "Checking the backup";
            case COPYING -> "Copying";
            case COMMITTING -> "Finishing";
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
