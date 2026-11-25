package com.vertexforge.launcher.project;

import java.nio.file.Path;

/**
 * Turns a path picked with "Add Existing…", dropped on the window or given on the command line into a project
 * (VK-1685). An inspection blocks, possibly for many seconds on an unreachable network share, so it only ever runs on
 * a background thread with a timeout. It never throws and never writes anything.
 */
@FunctionalInterface
public interface ProjectInspector {

    /**
     * A project file, or a folder that holds one: {@link PickedProject.Found}, {@link PickedProject.Choose} for a
     * folder with several, or {@link PickedProject.Refused}.
     *
     * @throws NullPointerException when {@code picked} is {@code null}
     */
    PickedProject inspect(Path picked);

    /** The real file system. */
    static ProjectInspector system() {
        return FileSystemInspection::inspect;
    }
}
