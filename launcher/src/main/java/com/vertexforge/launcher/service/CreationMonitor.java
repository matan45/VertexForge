package com.vertexforge.launcher.service;

import java.io.IOException;

/**
 * Watches a project creation (VK-1679): it hears about every file written and can ask the creation to stop. Both
 * methods are called on the thread that creates the project.
 */
public interface CreationMonitor {

    /** Watches nothing and never cancels. */
    CreationMonitor NONE = new CreationMonitor() {
    };

    /**
     * Called after each file is written into the staging folder.
     *
     * @throws IOException to make the creation fail at this point (tests inject faults here); the staging folder is
     *                     then removed as for any other failure
     */
    default void progress(int done, int total) throws IOException {
    }

    /**
     * Whether the user asked to stop. Checked after every file and once more just before the project is moved into
     * place; after that the creation finishes whatever this says.
     */
    default boolean cancelRequested() {
        return false;
    }
}
