package com.vertexforge.launcher.dialog;

import java.nio.file.Path;
import java.util.Objects;

/**
 * What "Open a copy" asks: the name of the copy's folder (VK-1690). Toolkit-free; {@link CopyNameDialog} shows it.
 *
 * @param projectName   the project being copied, for the header
 * @param location      the folder the copy is made in (the one holding the project's folder)
 * @param suggestedName the name the field starts with
 * @param problem       why the last name was refused (it exists, the path is too long, too little space), or
 *                      {@code null}
 */
public record CopyNamePrompt(String projectName, Path location, String suggestedName, String problem) {
    public CopyNamePrompt {
        Objects.requireNonNull(projectName, "projectName");
        Objects.requireNonNull(location, "location");
        Objects.requireNonNull(suggestedName, "suggestedName");
    }
}
