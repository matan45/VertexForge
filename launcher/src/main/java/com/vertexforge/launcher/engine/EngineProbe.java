package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.util.Volumes;

import java.io.IOException;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.Locale;
import java.util.Optional;

/**
 * Looks at one engine install and says whether it can start the editor (VK-1681). A probe blocks, possibly for many
 * seconds on an unreachable network share, so it only ever runs on {@link EngineHealthChecker}'s threads. It never
 * throws, and it never writes anything.
 */
@FunctionalInterface
public interface EngineProbe {

    EngineHealth probe(Path executable);

    /** The real file system. */
    static EngineProbe system() {
        return EngineProbe::probeFileSystem;
    }

    /**
     * Turns a file picked with "Add Engine…" into an engine: an {@code engine.json} stands for the editor it names
     * beside it, an {@code .exe} for itself. The engine must check OK. Blocks like a probe; never throws.
     */
    static PickedEngine inspectPicked(Path picked, EngineProbe probe) {
        try {
            if (picked == null || picked.getFileName() == null) {
                return new PickedEngine.Refused("Choose the engine's Editor.exe or its " + EngineManifest.FILE_NAME);
            }
            String name = picked.getFileName().toString();
            Path executable;
            if (name.equalsIgnoreCase(EngineManifest.FILE_NAME)) {
                Path dir = picked.toAbsolutePath().getParent();
                EngineManifest manifest;
                try {
                    manifest = EngineManifest.read(picked);
                } catch (NoSuchFileException e) {
                    return new PickedEngine.Refused(picked + " was not found");
                } catch (InvalidManifestException e) {
                    return new PickedEngine.Refused(picked + " is not a valid engine manifest: " + e.getMessage());
                } catch (IOException e) {
                    return new PickedEngine.Refused(picked + " could not be read: " + reasonOf(e));
                }
                executable = dir.resolve(manifest.editorExe());
            } else if (name.toLowerCase(Locale.ROOT).endsWith(".exe")) {
                executable = picked.toAbsolutePath();
            } else {
                return new PickedEngine.Refused(name + " is neither an editor (.exe) nor an "
                        + EngineManifest.FILE_NAME);
            }

            Path real;
            try {
                real = executable.toRealPath();
            } catch (NoSuchFileException e) {
                return new PickedEngine.Refused(executable + " was not found");
            } catch (IOException e) {
                return new PickedEngine.Refused(executable + " could not be opened: " + reasonOf(e));
            }

            EngineHealth health = probe.probe(real);
            if (health == null) {
                return new PickedEngine.Refused("Could not check " + real);
            }
            if (!health.isOk()) {
                return new PickedEngine.Refused("This engine cannot be used: " + health.reason());
            }
            return new PickedEngine.Usable(real, health);
        } catch (InvalidPathException e) {
            return new PickedEngine.Refused("Not a valid path: " + e.getMessage());
        } catch (RuntimeException e) {
            return new PickedEngine.Refused("Could not check the engine: " + e);
        }
    }

    /**
     * The editor's attributes, then the manifest beside it (if any), then the folder it starts in.
     * {@code Files.exists} is not used: it reports a file it may not read as absent.
     */
    private static EngineHealth probeFileSystem(Path executable) {
        try {
            if (executable == null || !executable.isAbsolute() || executable.getFileName() == null) {
                return EngineHealth.broken("The stored path is not a valid absolute path");
            }

            BasicFileAttributes attributes;
            try {
                attributes = Files.readAttributes(executable, BasicFileAttributes.class);
            } catch (NoSuchFileException e) {
                return Volumes.isAbsentDriveLetter(executable)
                        ? missingDrive(executable)
                        : EngineHealth.broken(executable.getFileName() + " was not found: the engine was moved or"
                                + " deleted");
            } catch (AccessDeniedException e) {
                return EngineHealth.broken("Access to " + executable + " was denied");
            } catch (FileSystemException e) {
                // The volume is there but did not answer (a network share that is down, a device that is not ready).
                return Volumes.isAbsentDriveLetter(executable)
                        ? missingDrive(executable)
                        : EngineHealth.missing("The engine's location cannot be reached: " + reasonOf(e));
            } catch (IOException e) {
                return EngineHealth.broken("Could not check " + executable + ": " + reasonOf(e));
            }
            if (!attributes.isRegularFile()) {
                return EngineHealth.broken(executable + " is not a file");
            }
            if (!Files.isExecutable(executable)) {
                return EngineHealth.broken(executable + " cannot be run");
            }

            Path dir = executable.getParent();
            Optional<EngineManifest> manifest;
            try {
                manifest = EngineManifest.readIfPresent(dir);
            } catch (InvalidManifestException e) {
                return EngineHealth.broken(EngineManifest.FILE_NAME + " is not valid: " + e.getMessage());
            } catch (IOException e) {
                return EngineHealth.broken(EngineManifest.FILE_NAME + " could not be read: " + reasonOf(e));
            }

            if (manifest.isEmpty()) {
                return EngineHealth.ok(null, dir);
            }
            EngineManifest found = manifest.get();
            String exeName = executable.getFileName().toString();
            if (!found.editorExe().equalsIgnoreCase(exeName)) {
                return EngineHealth.broken(EngineManifest.FILE_NAME + " belongs to " + found.editorExe() + ", not "
                        + exeName, found);
            }
            Path workingDirectory = found.workingDirectoryIn(dir);
            if (!Files.isDirectory(workingDirectory)) {
                return EngineHealth.broken("Its working directory " + workingDirectory + " does not exist", found);
            }
            return EngineHealth.ok(found, workingDirectory);
        } catch (InvalidPathException e) {
            return EngineHealth.broken("Not a valid path: " + e.getMessage());
        } catch (RuntimeException e) {
            return EngineHealth.broken("Could not check the engine: " + e);
        }
    }

    private static EngineHealth missingDrive(Path executable) {
        return EngineHealth.missing("Drive " + executable.getRoot() + " is not available");
    }

    private static String reasonOf(IOException e) {
        if (e instanceof FileSystemException fileSystemException && fileSystemException.getReason() != null) {
            return fileSystemException.getReason();
        }
        return e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
    }
}
