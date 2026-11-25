package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.LauncherEnvironment;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * The folders "Move to Recycle Bin…" never takes (VK-1695), whatever a project file says about itself. A
 * {@code .vfproj} can sit anywhere, Add Existing… lists any one, and the delete takes the folder that holds it: a
 * project file saved straight into Documents must not cost the user their Documents folder, even when the Recycle Bin
 * could give it back. So the project folder may not be one of these folders, nor hold one:
 * <ul>
 *   <li>the user's profile and its own folders (Desktop, Documents, Downloads, Pictures, Music, Videos, Saved Games),
 *       both as Windows names them now (their known folders, which follow a redirection into OneDrive or another
 *       drive) and at their usual place;</li>
 *   <li>the folder Create Project starts in (Settings' default project location), added per delete;</li>
 *   <li>the folders the system's environment names: the profile, OneDrive, Windows, Program Files, ProgramData, the
 *       public profile, AppData and the temp folders;</li>
 *   <li>the launcher's own settings folder ({@code ~/.vertexforge}), which it may not be inside either, and the
 *       running launcher's install folder.</li>
 * </ul>
 * Engine folders are added per delete ({@link DeleteGuards#engineFolders}), since engines change while the launcher
 * runs.
 *
 * @param neverThisOrAbove absolute, normalized folders a project folder may neither be nor hold
 * @param neverInside      absolute, normalized folders a project folder may not lie in
 */
public record ProtectedFolders(List<Path> neverThisOrAbove, List<Path> neverInside) {

    /** The environment variables whose folders are protected, as Windows sets them. */
    static final List<String> VARIABLES = List.of("USERPROFILE", "OneDrive", "OneDriveConsumer", "OneDriveCommercial",
            "SystemRoot", "windir", "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "ProgramData", "PUBLIC",
            "APPDATA", "LOCALAPPDATA", "TEMP", "TMP");
    /** The profile's own folders, protected at their usual place too. */
    static final List<String> PROFILE_FOLDERS = List.of("Desktop", "Documents", "Downloads", "Pictures", "Music",
            "Videos", "Saved Games", "OneDrive");

    public ProtectedFolders {
        neverThisOrAbove = List.copyOf(neverThisOrAbove);
        neverInside = List.copyOf(neverInside);
    }

    /** Protects nothing (a hub without an environment, tests). */
    public static ProtectedFolders none() {
        return new ProtectedFolders(List.of(), List.of());
    }

    /** The folders of {@code environment}, as listed above. Reads no file: the known folders are asked of Windows. */
    public static ProtectedFolders of(LauncherEnvironment environment) {
        Objects.requireNonNull(environment, "environment");
        List<Path> folders = new ArrayList<>();
        Path configDir = environment.configDir().toAbsolutePath().normalize();
        folders.add(configDir);
        Path home = configDir.getParent();
        if (home != null) {
            folders.add(home);
            for (String name : PROFILE_FOLDERS) {
                folders.add(home.resolve(name));
            }
        }
        for (Path known : environment.knownFolders().userFolders()) {
            absolute(known.toString()).ifPresent(folders::add);
        }
        for (String variable : VARIABLES) {
            String value = environment.env().apply(variable);
            if (value != null) {
                absolute(value).ifPresent(folders::add);
            }
        }
        environment.launcherExecutable().map(Path::getParent).ifPresent(folders::add);
        return new ProtectedFolders(distinct(folders), List.of(configDir));
    }

    /** These folders and {@code more}, which a project folder may neither be nor hold either. */
    public ProtectedFolders with(List<Path> more) {
        List<Path> folders = new ArrayList<>(neverThisOrAbove);
        for (Path folder : more) {
            if (folder.isAbsolute()) {
                folders.add(folder.normalize());
            }
        }
        return new ProtectedFolders(distinct(folders), neverInside);
    }

    /** {@code text} as an absolute, normalized path; empty when it is blank, relative or not a valid path. */
    static Optional<Path> absolute(String text) {
        if (text == null || text.isBlank()) {
            return Optional.empty();
        }
        try {
            Path path = Path.of(text.strip());
            return path.isAbsolute() ? Optional.of(path.normalize()) : Optional.empty();
        } catch (InvalidPathException e) {
            return Optional.empty();
        }
    }

    /** {@code paths} without repeats, compared as Windows compares names (ignoring case). */
    private static List<Path> distinct(List<Path> paths) {
        List<Path> distinct = new ArrayList<>();
        for (Path path : paths) {
            boolean seen = false;
            for (Path kept : distinct) {
                seen |= kept.toString().equalsIgnoreCase(path.toString());
            }
            if (!seen) {
                distinct.add(path);
            }
        }
        return distinct;
    }
}
