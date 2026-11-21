package com.vertexforge.launcher.dialog;

import java.nio.file.Path;
import java.util.Objects;

/**
 * What "Open a copy" (VK-1690) and "Duplicate…" (VK-1695) ask: the name of the copy's folder. Toolkit-free;
 * {@link CopyNameDialog} shows it.
 *
 * @param projectName   the project being copied, for the header
 * @param location      the folder the copy is made in (the one holding the project's folder)
 * @param suggestedName the name the field starts with
 * @param problem       why the last name was refused (it exists, the path is too long, too little space), or
 *                      {@code null}
 * @param purpose       what the copy is for, which decides the dialog's words
 */
public record CopyNamePrompt(String projectName, Path location, String suggestedName, String problem,
                             Purpose purpose) {

    /** What the copy is for. */
    public enum Purpose {
        /** "Open a copy" (VK-1690): the copy opens, the original stays as it is. */
        OPEN_COPY("Open a Copy", "Copy and open"),
        /** "Duplicate…" (VK-1695): the copy is listed, and nothing opens. */
        DUPLICATE("Duplicate Project", "Duplicate");

        private final String title;
        private final String button;

        Purpose(String title, String button) {
            this.title = title;
            this.button = button;
        }

        /** The dialog's title. */
        public String title() {
            return title;
        }

        /** The text of the button that makes the copy. */
        public String button() {
            return button;
        }
    }

    public CopyNamePrompt {
        Objects.requireNonNull(projectName, "projectName");
        Objects.requireNonNull(location, "location");
        Objects.requireNonNull(suggestedName, "suggestedName");
        Objects.requireNonNull(purpose, "purpose");
    }

    /** An "Open a copy" prompt ({@link Purpose#OPEN_COPY}). */
    public CopyNamePrompt(String projectName, Path location, String suggestedName, String problem) {
        this(projectName, location, suggestedName, problem, Purpose.OPEN_COPY);
    }

    /** The dialog's header. */
    public String header() {
        return switch (purpose) {
            case OPEN_COPY -> "Copy \"" + projectName + "\" and open the copy";
            case DUPLICATE -> "Duplicate \"" + projectName + "\"";
        };
    }

    /** What the copy will be, above the name field. */
    public String intro() {
        return switch (purpose) {
            case OPEN_COPY -> "The project folder is copied into " + location
                    + " under the name below, and the copy opens. This project stays exactly as it is.";
            case DUPLICATE -> "The project folder is copied into " + location + " under the name below. The copy "
                    + "takes that name, in its project file (renamed after it) and its scripts project, and is "
                    + "added to the list; it does not open. This project stays exactly as it is.";
        };
    }
}
