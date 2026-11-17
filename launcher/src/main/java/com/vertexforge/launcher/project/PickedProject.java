package com.vertexforge.launcher.project;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;

/**
 * What a picked or dropped path turned out to be (VK-1685): a project, a folder holding several, or something the
 * launcher refuses.
 */
public sealed interface PickedProject {

    /** The path this answer is about. */
    Path picked();

    /**
     * A project file that can be listed.
     *
     * @param file    the real path of the {@code .vfproj} ({@link Path#toRealPath}), the one the list stores
     * @param project what the file says
     */
    record Found(Path file, ProjectFile project) implements PickedProject {
        public Found {
            Objects.requireNonNull(file, "file");
            Objects.requireNonNull(project, "project");
        }

        @Override
        public Path picked() {
            return file;
        }
    }

    /**
     * A folder with more than one project file: the user picks one.
     *
     * @param folder     the folder as it was picked
     * @param candidates each project file in it, already inspected ({@link Found} or {@link Refused}), sorted by file
     *                   name without regard to letter case
     */
    record Choose(Path folder, List<PickedProject> candidates) implements PickedProject {
        public Choose {
            Objects.requireNonNull(folder, "folder");
            candidates = List.copyOf(candidates);
        }

        @Override
        public Path picked() {
            return folder;
        }
    }

    /**
     * A path that is not added.
     *
     * @param picked the path as it was picked (or the project file inside a picked folder)
     * @param reason why, for the user; it never repeats the path, which the window shows in front of it
     */
    record Refused(Path picked, String reason) implements PickedProject {
        public Refused {
            Objects.requireNonNull(picked, "picked");
            Objects.requireNonNull(reason, "reason");
        }
    }
}
