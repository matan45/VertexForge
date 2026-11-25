package com.vertexforge.launcher.project;

import com.vertexforge.launcher.service.ProjectCreator;

import java.io.IOException;
import java.nio.file.AccessDeniedException;
import java.nio.file.DirectoryIteratorException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;

/** The file-system work behind {@link ProjectInspector#system()} (VK-1685). Never throws, never writes. */
final class FileSystemInspection {

    static final String NOT_FOUND = "Was not found";

    /** Project files in a folder, by file name without regard to letter case (ties by exact name). */
    private static final Comparator<Path> BY_FILE_NAME = Comparator
            .comparing((Path path) -> path.getFileName().toString(), String.CASE_INSENSITIVE_ORDER)
            .thenComparing(path -> path.getFileName().toString());

    private FileSystemInspection() {
    }

    static PickedProject inspect(Path picked) {
        Objects.requireNonNull(picked, "picked");
        try {
            BasicFileAttributes attributes;
            try {
                // Follows links, so a link to a folder is a folder. Files.exists is not used: it reports a file it
                // may not read as absent.
                attributes = Files.readAttributes(picked, BasicFileAttributes.class);
            } catch (IOException e) {
                return refused(picked, PathFailures.describe(picked, e, NOT_FOUND));
            }
            if (attributes.isDirectory()) {
                return inspectFolder(picked);
            }
            return inspectFile(picked, attributes);
        } catch (InvalidPathException e) {
            return refused(picked, "Is not a valid path: " + e.getMessage());
        } catch (RuntimeException e) {
            return refused(picked, "Could not be checked: " + e);
        }
    }

    private static PickedProject inspectFolder(Path folder) {
        Path folderName = folder.getFileName();
        if (folderName != null && isStagingFolder(folderName.toString())) {
            return refused(folder, "Is a project the launcher had not finished creating");
        }

        List<Path> projectFiles = new ArrayList<>();
        try (DirectoryStream<Path> children = Files.newDirectoryStream(folder)) {
            for (Path child : children) {
                // Only the folder itself: subfolders (".vertexforge", a folder named "x.vfproj") never count.
                if (ProjectFiles.isProjectFile(child) && Files.isRegularFile(child)) {
                    projectFiles.add(child);
                }
            }
        } catch (IOException e) {
            return refused(folder, "Could not be listed: " + PathFailures.describe(folder, e, NOT_FOUND));
        } catch (DirectoryIteratorException e) {
            return refused(folder, "Could not be listed: " + PathFailures.describe(folder, e.getCause(), NOT_FOUND));
        }

        projectFiles.sort(BY_FILE_NAME);
        if (projectFiles.isEmpty()) {
            return refused(folder, "Contains no " + ProjectFiles.EXTENSION + " file");
        }
        if (projectFiles.size() == 1) {
            return inspectListedFile(projectFiles.getFirst());
        }
        List<PickedProject> candidates = new ArrayList<>(projectFiles.size());
        for (Path file : projectFiles) {
            candidates.add(inspectListedFile(file));
        }
        return new PickedProject.Choose(folder, candidates);
    }

    /** A file found in a folder: its attributes are read again, as it may have changed since the listing. */
    private static PickedProject inspectListedFile(Path file) {
        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(file, BasicFileAttributes.class);
        } catch (IOException e) {
            return refused(file, PathFailures.describe(file, e, NOT_FOUND));
        }
        if (attributes.isDirectory()) {
            return refused(file, "Is a folder, not a " + ProjectFiles.EXTENSION + " file");
        }
        return inspectFile(file, attributes);
    }

    private static PickedProject inspectFile(Path file, BasicFileAttributes attributes) {
        if (!ProjectFiles.isProjectFile(file)) {
            return refused(file, "Is not a " + ProjectFiles.EXTENSION + " file");
        }
        if (!attributes.isRegularFile()) {
            return refused(file, "Is not a file");
        }

        Path real;
        try {
            // Fixes letter case, removes ".." and expands 8.3 short names, so every spelling of the project is listed
            // under one path.
            real = file.toRealPath();
        } catch (NoSuchFileException e) {
            return refused(file, PathFailures.describe(file, e, NOT_FOUND));
        } catch (IOException e) {
            return refused(file, "Could not be opened: " + PathFailures.describe(file, e, NOT_FOUND));
        }

        ProjectFile project;
        try {
            project = ProjectFileReader.read(real);
        } catch (InvalidProjectFileException e) {
            return refused(file, e.getMessage());
        } catch (NoSuchFileException | AccessDeniedException e) {
            return refused(file, PathFailures.describe(file, e, NOT_FOUND));
        } catch (IOException e) {
            return refused(file, "Could not be read: " + PathFailures.describe(file, e, NOT_FOUND));
        }
        return new PickedProject.Found(real, project);
    }

    /** A folder a creation was building in when it stopped: {@code .<name>.creating-<id>} (VK-1679). */
    static boolean isStagingFolder(String folderName) {
        return folderName.startsWith(".") && folderName.contains(ProjectCreator.STAGING_INFIX);
    }

    private static PickedProject refused(Path picked, String reason) {
        return new PickedProject.Refused(picked, withoutFinalPeriod(reason));
    }

    /** Windows' own reasons often end with a period; refusals never do. */
    private static String withoutFinalPeriod(String reason) {
        return reason.endsWith(".") && !reason.endsWith("..") ? reason.substring(0, reason.length() - 1) : reason;
    }
}
