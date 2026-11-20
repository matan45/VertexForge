package com.vertexforge.launcher.viewmodel;

import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.service.ScriptLibraryStatusService.LibraryState;
import com.vertexforge.launcher.session.EditorSession;

import java.util.Comparator;
import java.util.Objects;

/**
 * What the Status column shows (VK-1683): the project file's status, unless an editor this launcher started has the
 * project open, which then wins.
 *
 * <p>
 * A project whose file is OK can also carry a tag about its script library (VK-1691), such as "Library outdated".
 *
 * @param status  the project file's status (from the background checks)
 * @param session the phase of this launcher's editor on the project, or {@code null} when none runs
 * @param library the state of the project's script library ({@link LibraryState#UNKNOWN} when not known)
 */
public record StatusDisplay(ProjectStatus status, EditorSession.Phase session, LibraryState library) {

    /** Running projects first, then by the file's status. */
    public static final Comparator<StatusDisplay> ORDER = Comparator
            .comparing((StatusDisplay display) -> display.session() == null)
            .thenComparing(display -> display.status().kind());

    public StatusDisplay {
        Objects.requireNonNull(status, "status");
        library = library == null ? LibraryState.UNKNOWN : library;
    }

    /** Without a library state. */
    public StatusDisplay(ProjectStatus status, EditorSession.Phase session) {
        this(status, session, LibraryState.UNKNOWN);
    }

    /** The script library's tag (VK-1691); empty when there is none to show (or the editor runs). */
    public String libraryTag() {
        return running() || !status.isOk() ? "" : library.tag();
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
        String tag = libraryTag();
        if (tag.isEmpty()) {
            return status.reason();
        }
        return status.reason().isEmpty() ? library.tooltip() : status.reason() + "\n" + library.tooltip();
    }
}
