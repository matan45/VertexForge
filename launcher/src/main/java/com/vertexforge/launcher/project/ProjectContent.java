package com.vertexforge.launcher.project;

import com.vertexforge.launcher.util.PathContainment;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.function.Predicate;

/**
 * What a project's backup or copy covers (VK-1690): its folder, minus what the editor rebuilds. Pure: worked out from
 * the path texts alone, without the file system.
 * <p>
 * A backup or a copy takes the folder that holds the {@code .vfproj}, so it can only cover a project whose working
 * directory (where its assets live) is that folder or lies in it. The working directory is resolved as the editor
 * resolves it ({@code ProjectServiceImpl::loadProject}): a relative one against the project file's folder, then
 * normalized. Paths are compared name by name, ignoring case, as Windows does.
 * <p>
 * Left out are the editor's state folder (its lock, Play-mode saves and config) and the compiled scripts, which the
 * editor rebuilds from the scripts: exactly what a new project's {@code .gitignore} lists.
 *
 * @param projectFile              the {@code .vfproj}, absolute and normalized
 * @param folder                   the folder that holds it; {@code null} only when the project file has no parent
 * @param workingDirectory         the resolved working directory; {@code null} when it is {@link Scope#INVALID} or the
 *                                 folder is {@link Scope#NO_PARENT}
 * @param scope                    whether the folder covers the working directory
 * @param relativeWorkingDirectory for {@link Scope#INSIDE}, the working directory relative to the folder,
 *                                 '/'-separated, {@code "."} for the folder itself; {@code null} otherwise
 */
public record ProjectContent(Path projectFile, Path folder, Path workingDirectory, Scope scope,
                             String relativeWorkingDirectory) {

    /** Whether a project's folder covers its working directory. */
    public enum Scope {
        /** The working directory is the folder or lies in it: a backup or a copy can cover the project. */
        INSIDE,
        /** The working directory lies outside the folder (an absolute path elsewhere, or a '..' that leaves it). */
        OUTSIDE,
        /** The folder is a drive root (or there is none), so nothing can be put beside it. */
        NO_PARENT,
        /** The working directory is missing, blank or not a valid path. */
        INVALID
    }

    /** The editor's state folder in a project folder: lock, Play-mode saves and config (VK-1682, VK-1684). */
    public static final String STATE_FOLDER = EditorLockProbe.STATE_FOLDER;
    /** The compiled scripts, relative to the working directory ({@code ScriptingAdapter}: scripts/compiled/). */
    public static final String COMPILED_SCRIPTS = "scripts/compiled";

    public ProjectContent {
        Objects.requireNonNull(projectFile, "projectFile");
        Objects.requireNonNull(scope, "scope");
        if (scope == Scope.INSIDE && (folder == null || workingDirectory == null || relativeWorkingDirectory == null)) {
            throw new IllegalArgumentException("an INSIDE project has a folder and a working directory");
        }
    }

    /**
     * Works out what {@code projectFile}'s folder covers, given its {@code workingDirectory} member as written.
     *
     * @param projectFile      the {@code .vfproj}; a relative path is made absolute against the current folder
     * @param workingDirectory the member's text, or {@code null} when it is missing or not a string
     */
    public static ProjectContent of(Path projectFile, String workingDirectory) {
        Objects.requireNonNull(projectFile, "projectFile");
        Path file = projectFile.toAbsolutePath().normalize();
        Path folder = file.getParent();
        if (folder == null || folder.getParent() == null) {
            return new ProjectContent(file, folder, null, Scope.NO_PARENT, null);
        }
        if (workingDirectory == null || workingDirectory.isBlank()) {
            return new ProjectContent(file, folder, null, Scope.INVALID, null);
        }

        Path resolved;
        try {
            resolved = folder.resolve(Path.of(workingDirectory)).normalize();
        } catch (InvalidPathException e) {
            return new ProjectContent(file, folder, null, Scope.INVALID, null);
        }
        List<String> below = resolved.isAbsolute() ? namesBelow(folder, resolved) : null;
        if (below == null) {
            // Not absolute even now: a drive-relative path on another drive ("D:assets"), which is not in the folder.
            return new ProjectContent(file, folder, resolved, Scope.OUTSIDE, null);
        }
        return new ProjectContent(file, folder, resolved, Scope.INSIDE, below.isEmpty() ? "." : String.join("/", below));
    }

    /**
     * The '/'-relative paths, relative to the folder, that a backup or a copy leaves out: the state folder, and the
     * compiled scripts below the working directory (only for an {@link Scope#INSIDE} project, the only kind that is
     * backed up or copied).
     */
    public List<String> exclusions() {
        List<String> exclusions = new ArrayList<>();
        exclusions.add(STATE_FOLDER);
        if (relativeWorkingDirectory != null) {
            exclusions.add((relativeWorkingDirectory.equals(".") ? "" : relativeWorkingDirectory + "/")
                    + COMPILED_SCRIPTS);
        }
        return List.copyOf(exclusions);
    }

    /**
     * Whether a '/'-relative path is left out: it equals an {@linkplain #exclusions() exclusion} or lies in one,
     * ignoring case.
     */
    public Predicate<String> excluded() {
        List<String> exclusions = exclusions();
        return relative -> {
            for (String exclusion : exclusions) {
                if (relative.equalsIgnoreCase(exclusion)
                        || (relative.length() > exclusion.length() && relative.charAt(exclusion.length()) == '/'
                        && relative.regionMatches(true, 0, exclusion, 0, exclusion.length()))) {
                    return true;
                }
            }
            return false;
        };
    }

    /** Why this project cannot be backed up or copied; empty when it can ({@link Scope#INSIDE}). */
    public Optional<String> unavailableReason() {
        return switch (scope) {
            case INSIDE -> Optional.empty();
            case OUTSIDE -> Optional.of("Its working directory (" + workingDirectory + ") lies outside the project "
                    + "folder, so a backup or a copy could not cover the project's content.");
            case NO_PARENT -> Optional.of("The project folder is a drive root, so there is no folder beside it to put "
                    + "a backup or a copy in.");
            case INVALID -> Optional.of("Its working directory is missing or is not a valid path, so a backup or a "
                    + "copy could not tell which files belong to the project.");
        };
    }

    /**
     * A path relative to the working directory, as a '/'-relative path from the folder: {@code relativeToWd} itself
     * for a working directory of {@code "."}, else prefixed with it. Only for an {@link Scope#INSIDE} project.
     *
     * @throws IllegalStateException when the project is not {@link Scope#INSIDE}
     */
    public String relativeToFolder(String relativeToWd) {
        Objects.requireNonNull(relativeToWd, "relativeToWd");
        if (relativeWorkingDirectory == null) {
            throw new IllegalStateException("only an INSIDE project has paths relative to its folder");
        }
        return relativeWorkingDirectory.equals(".") ? relativeToWd : relativeWorkingDirectory + "/" + relativeToWd;
    }

    /**
     * Where the editor reads the scripts project, relative to the folder (VK-1695): always
     * {@code <workingDirectory>/scripts/scripts.mtproj} ({@code ScriptingServiceImpl}). Only for an
     * {@link Scope#INSIDE} project.
     */
    public String scriptsProjectRelative() {
        return relativeToFolder(SCRIPTS_PROJECT);
    }

    /** The scripts project relative to the working directory ({@code ScriptingServiceImpl}: scripts/scripts.mtproj). */
    public static final String SCRIPTS_PROJECT = "scripts/scripts.mtproj";

    /**
     * The names of {@code path} after those of {@code base} when {@code path} is {@code base} or lies in it, compared
     * name by name and ignoring case; {@code null} otherwise. Both are absolute and normalized.
     */
    static List<String> namesBelow(Path base, Path path) {
        return PathContainment.below(base, path);
    }
}
