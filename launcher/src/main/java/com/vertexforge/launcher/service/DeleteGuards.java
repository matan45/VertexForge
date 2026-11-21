package com.vertexforge.launcher.service;

import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.util.PathContainment;

import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Stream;

/**
 * Where "Move to Recycle Bin…" refuses to take a project folder (VK-1695), whatever the project file says: on a drive
 * without a Recycle Bin, where a folder is one the user or the launcher needs ({@link ProtectedFolders}, the engines'
 * folders), or where the folder holds or lies in another listed project. Every comparison is made with the folder as
 * listed and as its real path, so neither a link nor another spelling slips past.
 */
public final class DeleteGuards {

    /**
     * What a delete is checked against, gathered on the FX thread when the user asks for it.
     *
     * @param protectedFolders    the user's and the launcher's folders
     * @param engineFolders       every known engine's install, working and content folders ({@link #engineFolders})
     * @param otherListedProjects the project files of every other entry in the list
     */
    public record DeleteContext(ProtectedFolders protectedFolders, List<Path> engineFolders,
                                List<Path> otherListedProjects) {
        public DeleteContext {
            Objects.requireNonNull(protectedFolders, "protectedFolders");
            engineFolders = List.copyOf(engineFolders);
            otherListedProjects = List.copyOf(otherListedProjects);
        }

        /** Nothing to check against but the drive (tests). */
        public static DeleteContext none() {
            return new DeleteContext(ProtectedFolders.none(), List.of(), List.of());
        }
    }

    private DeleteGuards() {
        // Utility class - prevent instantiation
    }

    /**
     * Why {@code folder} (as listed; {@code realFolder} is its real path) may not be moved to the Recycle Bin; empty
     * when nothing here stops it. Blocks on the file system briefly (real paths): a job's thread.
     */
    static Optional<String> refusal(Path folder, Path realFolder, DeleteContext context, DriveTypes drives) {
        Objects.requireNonNull(folder, "folder");
        Objects.requireNonNull(realFolder, "realFolder");
        Objects.requireNonNull(context, "context");
        Objects.requireNonNull(drives, "drives");

        Optional<String> drive = driveRefusal(realFolder, drives);
        if (drive.isPresent()) {
            return drive;
        }
        List<Path> spellings = List.of(folder, realFolder);

        for (Path kept : context.protectedFolders().neverThisOrAbove()) {
            if (holds(spellings, kept)) {
                return Optional.of(folder + " is, or holds, " + kept + ", a folder the launcher never deletes. Move "
                        + "the project into a folder of its own, or delete it by hand.");
            }
        }
        for (Path kept : context.protectedFolders().neverInside()) {
            if (inside(spellings, kept)) {
                return Optional.of(folder + " lies in the launcher's own folder " + kept + ", which the launcher "
                        + "never deletes from.");
            }
        }
        for (Path engine : context.engineFolders()) {
            if (holds(spellings, engine)) {
                return Optional.of(folder + " holds an engine's folder (" + engine + "), so deleting it would break "
                        + "that engine.");
            }
        }
        for (Path other : context.otherListedProjects()) {
            Path otherFolder = other.toAbsolutePath().normalize().getParent();
            if (otherFolder == null) {
                continue;
            }
            if (holds(spellings, other)) {
                return Optional.of(folder + " also holds the listed project " + other + ". Remove that project from "
                        + "the list or move it first.");
            }
            if (inside(spellings, otherFolder)) {
                return Optional.of(folder + " lies in the folder of the listed project " + other + ", so deleting it "
                        + "would change that project too.");
            }
        }
        return Optional.empty();
    }

    /**
     * Why the drive of {@code realFolder} cannot take it to a Recycle Bin; empty for a fixed drive. Only a fixed drive
     * has one: a network share, a mapped drive and a removable stick delete permanently. The shell's own check is
     * the last word; this one also stops a drive whose type the launcher cannot tell.
     */
    static Optional<String> driveRefusal(Path realFolder, DriveTypes drives) {
        Path root = realFolder.getRoot();
        if (root == null) {
            return Optional.of("The launcher could not tell which drive " + realFolder + " is on.");
        }
        String rootText = root.toString();
        if (rootText.startsWith("\\\\") || rootText.startsWith("//")) {
            return Optional.of("The project is on a network share (" + rootText + "), which has no Recycle Bin: it "
                    + "would be deleted for good. Delete it by hand if that is what you want.");
        }
        return switch (drives.kindOf(root)) {
            case LOCAL -> Optional.empty();
            case REMOTE -> Optional.of("The project is on a network drive (" + rootText + "), which has no Recycle "
                    + "Bin: it would be deleted for good. Delete it by hand if that is what you want.");
            case REMOVABLE -> Optional.of("The project is on a removable drive (" + rootText + "), which has no "
                    + "Recycle Bin: it would be deleted for good. Delete it by hand if that is what you want.");
            case OPTICAL, RAM_DISK -> Optional.of("The project is on a drive (" + rootText + ") that has no Recycle "
                    + "Bin. Delete it by hand if that is what you want.");
            case UNKNOWN -> Optional.of("The launcher could not tell what kind of drive " + rootText + " is, so it "
                    + "cannot be sure it has a Recycle Bin. Delete the project by hand if that is what you want.");
        };
    }

    /**
     * Every folder of every engine the launcher knows, the override included: its install folder, the folder it starts
     * in and the content folders its manifest names. A project folder that holds one would take the engine with it.
     */
    public static List<Path> engineFolders(EngineOptions options) {
        Objects.requireNonNull(options, "options");
        List<Path> folders = new ArrayList<>();
        Stream.concat(options.override().stream(), options.engines().stream()).forEach(engine -> add(folders, engine));
        return List.copyOf(folders);
    }

    private static void add(List<Path> folders, EngineOption engine) {
        if (engine.executable() == null) {
            return;
        }
        Path dir = engine.executable().toAbsolutePath().normalize().getParent();
        if (dir == null) {
            return;
        }
        folders.add(dir);
        if (engine.health().workingDirectory() != null) {
            folders.add(engine.health().workingDirectory().toAbsolutePath().normalize());
        }
        EngineManifest manifest = engine.health().manifest();
        if (manifest != null) {
            Stream.of(manifest.templatesDirIn(dir), manifest.scriptLibDirIn(dir), manifest.examplesDirIn(dir),
                    manifest.vfxLibraryDirIn(dir)).flatMap(Optional::stream).forEach(folders::add);
        }
    }

    /** Whether {@code kept}, or its real path, is one of {@code spellings} or lies in one. */
    private static boolean holds(List<Path> spellings, Path kept) {
        for (Path candidate : withRealPath(kept)) {
            for (Path spelling : spellings) {
                if (PathContainment.isWithin(spelling, candidate)) {
                    return true;
                }
            }
        }
        return false;
    }

    /** Whether one of {@code spellings} lies in {@code base} or its real path (being {@code base} counts too). */
    private static boolean inside(List<Path> spellings, Path base) {
        for (Path candidate : withRealPath(base)) {
            for (Path spelling : spellings) {
                if (PathContainment.isWithin(candidate, spelling)) {
                    return true;
                }
            }
        }
        return false;
    }

    /** {@code path} normalized, and its real path when it has one that differs. */
    private static List<Path> withRealPath(Path path) {
        Path normalized = path.toAbsolutePath().normalize();
        try {
            Path real = normalized.toRealPath();
            return real.toString().equalsIgnoreCase(normalized.toString()) ? List.of(normalized)
                    : List.of(normalized, real);
        } catch (IOException | SecurityException e) {
            return List.of(normalized);
        }
    }
}
