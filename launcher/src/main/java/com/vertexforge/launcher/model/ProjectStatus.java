package com.vertexforge.launcher.model;

import java.util.Objects;

/**
 * Whether a listed project's file can be opened right now (VK-1678). A status never removes an entry; it only
 * decides how the row looks and whether Open is enabled. VK-1686 adds more kinds.
 *
 * @param kind   the category
 * @param reason a human-readable explanation, shown as the status tooltip
 */
public record ProjectStatus(Kind kind, String reason) {

    public enum Kind {
        CHECKING("Checking…"),
        OK("OK"),
        MISSING("Missing"),
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
        reason = reason == null ? "" : reason;
    }

    public static ProjectStatus ok() {
        return new ProjectStatus(Kind.OK, "The project file can be opened");
    }

    public static ProjectStatus missing(String reason) {
        return new ProjectStatus(Kind.MISSING, reason);
    }

    public static ProjectStatus unreadable(String reason) {
        return new ProjectStatus(Kind.UNREADABLE, reason);
    }

    public String label() {
        return kind.label();
    }

    public boolean isOk() {
        return kind == Kind.OK;
    }
}
