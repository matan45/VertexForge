package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.PickedProject;

import java.util.List;
import java.util.Objects;

/**
 * What "Locate…" does with the project file picked for a listed entry (VK-1686). Free of the JavaFX toolkit, so the
 * rules run in unit tests; {@code MainController} only shows the result.
 */
public sealed interface LocateDecision {

    /** Nothing is changed; the message says why. */
    record Refuse(String message) implements LocateDecision {
        public Refuse {
            Objects.requireNonNull(message, "message");
        }
    }

    /** The entry already points at the picked file: check it again, write nothing. */
    record Recheck() implements LocateDecision {
    }

    /**
     * Point the entry at the picked file.
     *
     * @param confirmation the question to ask first (the picked project has another name), or {@code null}
     */
    record Relink(PickedProject.Found found, String confirmation) implements LocateDecision {
        public Relink {
            Objects.requireNonNull(found, "found");
        }
    }

    /**
     * @param entry  the entry Locate… was chosen on
     * @param picked what {@code ProjectHub.inspectProject} found at the picked path
     * @param listed every listed entry, to refuse a file another entry already lists
     */
    static LocateDecision decide(RecentEntry entry, PickedProject picked, List<RecentEntry> listed) {
        Objects.requireNonNull(entry, "entry");
        Objects.requireNonNull(picked, "picked");
        Objects.requireNonNull(listed, "listed");
        return switch (picked) {
            case PickedProject.Refused refused -> new Refuse(refused.picked() + ": " + refused.reason() + ".");
            case PickedProject.Choose choose -> new Refuse(choose.folder() + " holds more than one project file;"
                    + " pick the project file itself.");
            case PickedProject.Found found -> decideFound(entry, found, listed);
        };
    }

    private static LocateDecision decideFound(RecentEntry entry, PickedProject.Found found, List<RecentEntry> listed) {
        // Paths compare as the file system does (without regard to letter case on Windows).
        if (found.file().equals(entry.path())) {
            return new Recheck();
        }
        for (RecentEntry other : listed) {
            if (!other.id().equals(entry.id()) && found.file().equals(other.path())) {
                return new Refuse(found.file() + " is already in the list as \"" + other.displayName()
                        + "\". Remove one of the two entries instead.");
            }
        }
        String pickedName = found.project().projectName();
        String listedName = entry.name();
        if (listedName != null && !listedName.isBlank() && !listedName.equals(pickedName)) {
            return new Relink(found, "The project you picked is called \"" + pickedName + "\", not \"" + listedName
                    + "\". Point \"" + listedName + "\" at it anyway? Its pin, engine and Last Opened time move with"
                    + " it, and it is renamed \"" + pickedName + "\".");
        }
        return new Relink(found, null);
    }
}
