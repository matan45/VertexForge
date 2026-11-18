package com.vertexforge.launcher.project;

import java.util.Objects;

/**
 * What a background check found at a listed project's path (VK-1686): the file's own state, before the engines are
 * taken into account. {@code ProjectHealthService.evaluate} turns it into the row's status.
 */
public sealed interface ProjectFileCheck {

    /** The file (or its drive) is not there. */
    record Missing(String reason) implements ProjectFileCheck {
        public Missing {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /** The file could not be looked at or read: access denied, a failing share, no answer in time. */
    record Unreadable(String reason) implements ProjectFileCheck {
        public Unreadable {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /** The file was read but is not a project file the launcher can use ({@link ProjectFileReader}). */
    record Invalid(String reason) implements ProjectFileCheck {
        public Invalid {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /**
     * The file was read.
     *
     * @param file what it says; {@link ProjectFile#structureProblem()} may still name what the editor would refuse
     * @param lock whether an editor holds the project's folder
     */
    record Readable(ProjectFile file, EditorLockState lock) implements ProjectFileCheck {
        public Readable {
            Objects.requireNonNull(file, "file");
            Objects.requireNonNull(lock, "lock");
        }
    }
}
