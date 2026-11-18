package com.vertexforge.launcher.model;

import java.util.Objects;

/**
 * Whether a listed project can be opened right now, and if not, why (VK-1678; the health kinds are VK-1686's). A
 * status never removes an entry; it only decides how the row looks and whether Open is enabled (only {@code OK}).
 * <p>
 * The kinds are in the order the Status column sorts them, after the rows an editor of this launcher has open.
 *
 * @param kind   the category
 * @param label  the Status column's text; {@code null} for the kind's own (Needs newer engine names the version)
 * @param reason a human-readable explanation, shown as the status tooltip
 */
public record ProjectStatus(Kind kind, String label, String reason) {

    public enum Kind {
        CHECKING("Checking…"),
        OK("OK"),
        /** Another editor has the project's folder open (its lock is held, VK-1682). */
        ALREADY_OPEN("Already open"),
        /** No engine Open could start reads the project's schema version: the editor would exit with code 4. */
        NEEDS_NEWER_ENGINE("Needs newer VertexForge"),
        /** The file is not a project the editor would open (it would exit with code 3). */
        INVALID("Invalid"),
        MISSING("Missing"),
        /** The file could not be looked at: access denied, a failing share, no answer in time. */
        UNREADABLE("Unreadable");

        private final String label;

        Kind(String label) {
            this.label = label;
        }

        public String label() {
            return label;
        }
    }

    /** Every row starts here until its check answers. */
    public static final ProjectStatus CHECKING =
            new ProjectStatus(Kind.CHECKING, "Checking whether the project file can be opened");

    public ProjectStatus {
        Objects.requireNonNull(kind, "kind");
        label = label == null || label.isBlank() ? kind.label() : label;
        reason = reason == null ? "" : reason;
    }

    /** A status labelled with its kind's own text. */
    public ProjectStatus(Kind kind, String reason) {
        this(kind, null, reason);
    }

    public static ProjectStatus ok() {
        return new ProjectStatus(Kind.OK, "The project file can be opened");
    }

    /** OK, with a note for the tooltip (a check that could not be made, VK-1686). */
    public static ProjectStatus ok(String note) {
        return new ProjectStatus(Kind.OK, note);
    }

    public static ProjectStatus alreadyOpen(String reason) {
        return new ProjectStatus(Kind.ALREADY_OPEN, reason);
    }

    /** @param label the column's text, which names the version the project needs ("Needs VertexForge 2.1.0") */
    public static ProjectStatus needsNewerEngine(String label, String reason) {
        return new ProjectStatus(Kind.NEEDS_NEWER_ENGINE, label, reason);
    }

    public static ProjectStatus invalid(String reason) {
        return new ProjectStatus(Kind.INVALID, reason);
    }

    public static ProjectStatus missing(String reason) {
        return new ProjectStatus(Kind.MISSING, reason);
    }

    public static ProjectStatus unreadable(String reason) {
        return new ProjectStatus(Kind.UNREADABLE, reason);
    }

    public boolean isOk() {
        return kind == Kind.OK;
    }
}
