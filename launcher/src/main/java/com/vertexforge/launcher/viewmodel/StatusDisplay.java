package com.vertexforge.launcher.viewmodel;

import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.session.EditorSession;

import java.util.Comparator;
import java.util.Objects;

/**
 * What the Status column shows (VK-1683): the project file's status, unless an editor this launcher started has the
 * project open, which then wins.
 *
 * @param status  the project file's status (from the background checks)
 * @param session the phase of this launcher's editor on the project, or {@code null} when none runs
 */
public record StatusDisplay(ProjectStatus status, EditorSession.Phase session) {

    /** Running projects first, then by the file's status. */
    public static final Comparator<StatusDisplay> ORDER = Comparator
            .comparing((StatusDisplay display) -> display.session() == null)
            .thenComparing(display -> display.status().kind());

    public StatusDisplay {
        Objects.requireNonNull(status, "status");
    }

    public boolean running() {
        return session != null && session != EditorSession.Phase.EXITED;
    }

    public String label() {
        return running() ? session.label() : status.label();
    }

    /** The cell's tooltip; empty for none. */
    public String tooltip() {
        if (running()) {
            return switch (session) {
                case STARTING -> "The editor is starting.";
                case LOADING -> "The editor is loading the project.";
                case READY -> "Open in an editor this launcher started.";
                case NO_READY_SIGNAL -> "The editor runs, but it never reported that it was ready.";
                case EXITED -> "";
            };
        }
        return status.reason();
    }
}
