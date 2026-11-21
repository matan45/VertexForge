package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.env.RecycleBin;
import com.vertexforge.launcher.env.RecycleCheck;
import com.vertexforge.launcher.env.RecycleResult;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.InvalidProjectFileException;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileReader;
import com.vertexforge.launcher.project.ProjectFiles;
import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.FileTrees;
import com.vertexforge.launcher.util.RelativePathRules;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Stream;

/**
 * "Move to Recycle Bin…" (VK-1695): moves a project's folder to the Windows Recycle Bin, from where the user can put it
 * back, and never deletes it permanently.
 * <p>
 * It runs in two jobs. {@link #check} reads everything the dialog needs and refuses early, before the user types the
 * project's name: the folder must be a real folder of its own on a fixed drive ({@link DeleteGuards}), hold exactly
 * one project file whose {@code projectName} is the one asked about, and no editor or other launcher may hold it; then
 * Windows is asked whether it would recycle the folder at all ({@link RecycleBin#check}, which deletes nothing).
 * {@link #delete} checks all of that again, right before Windows moves the folder, because the dialog may have stayed
 * open for minutes. The lock is only probed, never held: Windows refuses to move a folder holding an open file, so a
 * held lock file would make every delete fail, and an editor that opens the project meanwhile makes the move fail the
 * same way, with nothing moved.
 */
public final class ProjectDeleter {

    /** How many times a move that Windows refused because a file was in use is tried. */
    static final int RECYCLE_ATTEMPTS = 3;
    static final Duration RECYCLE_RETRY_DELAY = Duration.ofMillis(300);
    /** What every refusal ends with: the promise the action keeps. */
    static final String NOTHING_CHANGED = " The launcher never deletes a project permanently; nothing was changed.";

    /**
     * What to move.
     *
     * @param projectFile      the project's {@code .vfproj}, as listed
     * @param workingDirectory its {@code workingDirectory} member as read, or {@code null}
     * @param projectName      its {@code projectName} as read: what the user must type, and what the file must still say
     * @param context          the folders and projects the delete must leave alone
     */
    public record DeleteRequest(Path projectFile, String workingDirectory, String projectName,
                                DeleteGuards.DeleteContext context) {
        public DeleteRequest {
            Objects.requireNonNull(projectFile, "projectFile");
            Objects.requireNonNull(projectName, "projectName");
            Objects.requireNonNull(context, "context");
        }
    }

    /**
     * What the dialog shows about the folder.
     *
     * @param folder     the project folder, as listed
     * @param realFolder its real path, which is what Windows moves
     * @param bytes      the size of its files
     * @param files      how many files it holds
     * @param folders    how many folders it holds, below it
     * @param notes      what the user should know: links in it, project files nested in it
     */
    public record DeletePreflight(Path folder, Path realFolder, long bytes, int files, int folders,
                                  List<String> notes) {
        public DeletePreflight {
            Objects.requireNonNull(folder, "folder");
            Objects.requireNonNull(realFolder, "realFolder");
            notes = List.copyOf(notes);
        }
    }

    /** How {@link #check} ended. */
    public sealed interface CheckOutcome {
        /** The folder can be moved to the Recycle Bin, as far as anything can tell now. */
        record Ready(DeletePreflight preflight) implements CheckOutcome {
            public Ready {
                Objects.requireNonNull(preflight, "preflight");
            }
        }

        /** The folder must not or cannot be moved: {@code reason} says why. Nothing was changed. */
        record Refused(String reason) implements CheckOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        /** The user cancelled; nothing was changed. */
        record Cancelled() implements CheckOutcome {
        }

        /** Something went wrong while looking; nothing was changed. */
        record Failed(String message) implements CheckOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    /** How {@link #delete} ended. */
    public sealed interface DeleteOutcome {
        /** The folder is in the Recycle Bin. */
        record Recycled(Path folder) implements DeleteOutcome {
            public Recycled {
                Objects.requireNonNull(folder, "folder");
            }
        }

        /** The folder was not moved, and nothing was changed: {@code reason} says why. */
        record Refused(String reason) implements DeleteOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        /** Something unexpected happened; {@code message} says where the folder is, as far as the launcher knows. */
        record Failed(String message) implements DeleteOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    private final RecycleBin bin;
    private final DriveTypes drives;
    private final EditorLockProbe locks;
    private final EditorLockProbe.Sleeper sleeper;
    private final Duration retryDelay;

    public ProjectDeleter(RecycleBin bin, DriveTypes drives, EditorLockProbe locks) {
        this(bin, drives, locks, duration -> Thread.sleep(duration), RECYCLE_RETRY_DELAY);
    }

    /** For tests: {@code sleeper} waits {@code retryDelay} between moves that a file in use stopped. */
    ProjectDeleter(RecycleBin bin, DriveTypes drives, EditorLockProbe locks, EditorLockProbe.Sleeper sleeper,
                   Duration retryDelay) {
        this.bin = Objects.requireNonNull(bin, "bin");
        this.drives = Objects.requireNonNull(drives, "drives");
        this.locks = Objects.requireNonNull(locks, "locks");
        this.sleeper = Objects.requireNonNull(sleeper, "sleeper");
        this.retryDelay = Objects.requireNonNull(retryDelay, "retryDelay");
    }

    /** Whether this machine has a Recycle Bin the launcher can use at all. */
    public boolean available() {
        return bin.available();
    }

    /**
     * Everything the dialog needs, or why the folder cannot be moved. Only reads; reports its scan to {@code monitor},
     * then {@link JobMonitor.Phase#VERIFYING} while Windows is asked; the monitor may cancel either. Blocks: a job's
     * thread. Never throws.
     */
    public CheckOutcome check(DeleteRequest request, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        try {
            Location location = locate(request);
            if (location instanceof Refusal refusal) {
                return new CheckOutcome.Refused(refusal.reason());
            }
            Located located = (Located) location;

            FolderTree tree;
            try {
                tree = FolderTree.scan(located.folder(), relative -> false, monitor);
            } catch (JobCancelledException e) {
                return new CheckOutcome.Cancelled();
            } catch (IOException e) {
                return new CheckOutcome.Failed("Could not read the project's files: " + FileErrors.describe(e));
            }
            Optional<String> nested = nestedProjects(tree);
            if (nested.isPresent()) {
                return new CheckOutcome.Refused(nested.get());
            }
            List<String> notes = new ArrayList<>(tree.notes());

            // Not COMMITTING: the user may still cancel while Windows looks (the shell may walk the whole folder).
            monitor.progress(JobMonitor.Phase.VERIFYING, 0, JobMonitor.UNKNOWN);
            RecycleCheck answer = bin.check(located.realFolder(), monitor::cancelRequested);
            // The dry run is a real operation that its sink stops before anything is deleted. Should Windows ever go
            // ahead without asking the sink first, the folder is gone: say so, never "nothing was changed".
            if (!Files.isDirectory(located.folder(), LinkOption.NOFOLLOW_LINKS)
                    || !Files.isRegularFile(request.projectFile(), LinkOption.NOFOLLOW_LINKS)) {
                return new CheckOutcome.Failed("While Windows was asked whether " + located.folder() + " can go to "
                        + "the Recycle Bin, it went ahead and moved or deleted it. Look for it in the Recycle Bin.");
            }
            switch (answer) {
                case RecycleCheck.Recyclable ignored -> {
                    // As it should be.
                }
                case RecycleCheck.NotRecyclable not -> {
                    return new CheckOutcome.Refused("Windows cannot move " + located.folder() + " to the Recycle Bin: "
                            + not.reason() + "." + NOTHING_CHANGED);
                }
                case RecycleCheck.Unknown unknown -> {
                    return new CheckOutcome.Refused("Windows could not say whether " + located.folder() + " can go to "
                            + "the Recycle Bin (" + unknown.reason() + ")." + NOTHING_CHANGED);
                }
                case RecycleCheck.Cancelled ignored -> {
                    return new CheckOutcome.Cancelled();
                }
            }
            return new CheckOutcome.Ready(new DeletePreflight(located.folder(), located.realFolder(),
                    tree.totalBytes(), tree.files().size(), tree.folders().size(), notes));
        } catch (IOException e) {
            // A monitor that makes the job fail (tests).
            return new CheckOutcome.Failed("The check stopped: " + FileErrors.describe(e));
        } catch (RuntimeException e) {
            return new CheckOutcome.Failed("The check stopped unexpectedly: " + e);
        }
    }

    /**
     * Moves the project folder to the Recycle Bin when everything {@link #check} checked still holds and
     * {@code typed} is the project's name, exactly. Reports {@link JobMonitor.Phase#COMMITTING} first: once it runs it
     * cannot be cancelled. Blocks: a job's thread. Never throws.
     */
    public DeleteOutcome delete(DeleteRequest request, String typed, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        try {
            monitor.progress(JobMonitor.Phase.COMMITTING, 0, 1);
            Location location = locate(request);
            if (location instanceof Refusal refusal) {
                return new DeleteOutcome.Refused(refusal.reason());
            }
            Located located = (Located) location;
            if (typed == null || !typed.equals(located.projectName())) {
                return new DeleteOutcome.Refused("The name typed does not match the project's name, \""
                        + located.projectName() + "\"." + NOTHING_CHANGED);
            }
            // A project put inside the folder since the check would go with it.
            Optional<String> nested = nestedProjects(FolderTree.scan(located.folder(), relative -> false,
                    JobMonitor.NONE));
            if (nested.isPresent()) {
                return new DeleteOutcome.Refused(nested.get());
            }

            RecycleResult result = null;
            for (int attempt = 1; attempt <= RECYCLE_ATTEMPTS; attempt++) {
                if (attempt > 1) {
                    sleeper.sleep(retryDelay);
                    Optional<String> opened = lockRefusal(located.folder());
                    if (opened.isPresent()) {
                        return new DeleteOutcome.Refused(opened.get());
                    }
                }
                result = bin.recycle(located.realFolder());
                if (!(result instanceof RecycleResult.Refused refused) || !refused.retryable()) {
                    break;
                }
            }
            return outcome(located.folder(), result);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return new DeleteOutcome.Failed("The move was interrupted. Check whether " + request.projectFile()
                    + " is still there, or in the Recycle Bin.");
        } catch (IOException e) {
            return new DeleteOutcome.Failed("The move stopped: " + FileErrors.describe(e));
        } catch (RuntimeException e) {
            return new DeleteOutcome.Failed("The move stopped unexpectedly: " + e + ". Check whether "
                    + request.projectFile() + " is still there, or in the Recycle Bin.");
        }
    }

    /** What the last move's {@code result} means, checked against the folder itself. */
    private static DeleteOutcome outcome(Path folder, RecycleResult result) {
        boolean gone = Files.notExists(folder, LinkOption.NOFOLLOW_LINKS);
        return switch (result) {
            case RecycleResult.Recycled ignored -> gone
                    ? new DeleteOutcome.Recycled(folder)
                    : new DeleteOutcome.Failed("Windows reported " + folder + " moved to the Recycle Bin, but it is "
                            + "still there. Look in the Recycle Bin and in the folder before you try again.");
            case RecycleResult.NotRecyclable not -> gone
                    ? goneAnyway(folder, not.reason())
                    : new DeleteOutcome.Refused("Windows cannot move " + folder + " to the Recycle Bin: "
                            + not.reason() + "." + NOTHING_CHANGED);
            case RecycleResult.Refused refused -> gone
                    ? goneAnyway(folder, refused.reason())
                    : new DeleteOutcome.Refused("Windows did not move " + folder + ": " + refused.reason()
                            + ". Close the program that uses it, then try again." + NOTHING_CHANGED);
            case RecycleResult.Failed failed -> gone
                    ? goneAnyway(folder, failed.message())
                    : new DeleteOutcome.Failed("Could not move " + folder + " to the Recycle Bin: " + failed.message()
                            + ". Check that everything in it is still there.");
        };
    }

    /** Windows reported a problem, but the folder is gone: where it may be, with what Windows said. */
    private static DeleteOutcome goneAnyway(Path folder, String reason) {
        return new DeleteOutcome.Failed("Windows reported a problem (" + reason + "), but " + folder + " is gone. "
                + "Look for it in the Recycle Bin; if it is not there, Windows may have deleted it permanently.");
    }

    /**
     * Why the folder holds other projects, or empty when it holds none: a project file anywhere below its top level
     * (the editor's state folder aside) means the folder is more than this one project, so it is never moved.
     */
    static Optional<String> nestedProjects(FolderTree tree) {
        List<String> nested = new ArrayList<>();
        for (FolderTree.FileEntry file : tree.files()) {
            if (file.relative().indexOf('/') >= 0 && !RelativePathRules.isEditorState(file.relative())
                    && ProjectFiles.isProjectFileName(file.path().getFileName().toString())) {
                nested.add(file.relative());
            }
        }
        if (nested.isEmpty()) {
            return Optional.empty();
        }
        return Optional.of("The project folder holds other project files (" + String.join(", ",
                nested.subList(0, Math.min(nested.size(), 5))) + (nested.size() > 5 ? ", …" : "") + "), so moving it "
                + "would take those projects too. Move them out first, or delete the folder by hand."
                + NOTHING_CHANGED);
    }

    /** What {@link #locate} found: the project, ready to be moved. */
    private record Located(Path folder, Path realFolder, String projectName) implements Location {
    }

    /** Why {@link #locate} stopped. */
    private record Refusal(String reason) implements Location {
    }

    private sealed interface Location permits Located, Refusal {
    }

    /**
     * Every check that does not need the scan, in order: what the project covers, the folder itself, its path's
     * length, the project file, the guarded folders, then the locks. Blocks briefly.
     */
    private Location locate(DeleteRequest request) {
        ProjectContent content = ProjectContent.of(request.projectFile(), request.workingDirectory());
        switch (content.scope()) {
            case INSIDE -> {
                // Everything the project holds is in its folder.
            }
            case OUTSIDE -> {
                return new Refusal("Its working directory (" + content.workingDirectory() + ") lies outside the "
                        + "project folder, so moving the folder would leave the project's content behind. Delete it "
                        + "by hand." + NOTHING_CHANGED);
            }
            case NO_PARENT -> {
                return new Refusal("The project file is at the top of a drive, so its folder is the whole drive."
                        + NOTHING_CHANGED);
            }
            case INVALID -> {
                return new Refusal("Its working directory is missing or is not a valid path, so the launcher cannot "
                        + "tell which files belong to the project." + NOTHING_CHANGED);
            }
        }
        Path folder = content.folder();

        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(folder, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        } catch (NoSuchFileException e) {
            return new Refusal("The project folder " + folder + " is no longer there.");
        } catch (IOException e) {
            return new Refusal("Could not read the project folder " + folder + ": " + FileErrors.describe(e) + "."
                    + NOTHING_CHANGED);
        }
        if (!attributes.isDirectory() || (attributes.isOther() && !FileTrees.isOwnFolder(folder))) {
            return new Refusal("The project folder " + folder + " is a link to another folder, not a folder of its "
                    + "own. Delete it by hand." + NOTHING_CHANGED);
        }
        Path realFolder;
        try {
            realFolder = folder.toRealPath();
        } catch (IOException e) {
            return new Refusal("Could not read the project folder " + folder + ": " + FileErrors.describe(e) + "."
                    + NOTHING_CHANGED);
        }
        if (realFolder.toString().length() > ProjectNameRules.MAX_PATH_CHARS) {
            return new Refusal("The project folder's path is longer than Windows' " + ProjectNameRules.MAX_PATH_CHARS
                    + " characters, so the Recycle Bin cannot take it. Delete it by hand." + NOTHING_CHANGED);
        }

        // The project file itself, never through a link: a linked .vfproj would make its link's folder "the project".
        try {
            BasicFileAttributes file = Files.readAttributes(content.projectFile(), BasicFileAttributes.class,
                    LinkOption.NOFOLLOW_LINKS);
            if (file.isSymbolicLink() || file.isDirectory()) {
                return new Refusal("The project file " + content.projectFile() + " is a link or a folder, not a file "
                        + "of the project folder. Delete it by hand." + NOTHING_CHANGED);
            }
        } catch (NoSuchFileException e) {
            return new Refusal("The project file " + content.projectFile() + " is no longer there." + NOTHING_CHANGED);
        } catch (IOException e) {
            return new Refusal("Could not read the project file " + content.projectFile() + ": "
                    + FileErrors.describe(e) + "." + NOTHING_CHANGED);
        }

        String projectName;
        try {
            ProjectFile fresh = ProjectFileReader.read(content.projectFile());
            projectName = fresh.projectName();
            // The checks above used the working directory as first read; one changed since cannot be trusted.
            if (!Objects.equals(fresh.workingDirectory(), request.workingDirectory())) {
                return new Refusal("The project file's working directory changed since it was read (now \""
                        + fresh.workingDirectory() + "\"). Try again." + NOTHING_CHANGED);
            }
        } catch (NoSuchFileException e) {
            return new Refusal("The project file " + content.projectFile() + " is no longer there." + NOTHING_CHANGED);
        } catch (InvalidProjectFileException e) {
            return new Refusal("The project file cannot be read as a project: " + e.getMessage() + "."
                    + NOTHING_CHANGED);
        } catch (IOException e) {
            return new Refusal("Could not read the project file " + content.projectFile() + ": "
                    + FileErrors.describe(e) + "." + NOTHING_CHANGED);
        }
        if (!projectName.equals(request.projectName())) {
            return new Refusal("The project file now names the project \"" + projectName + "\", not \""
                    + request.projectName() + "\". Check what is in the folder, then try again." + NOTHING_CHANGED);
        }

        List<String> projectFiles;
        try (Stream<Path> children = Files.list(folder)) {
            projectFiles = children.filter(ProjectFiles::isProjectFile)
                    .map(child -> child.getFileName().toString())
                    .sorted()
                    .toList();
        } catch (IOException e) {
            return new Refusal("Could not list the project folder " + folder + ": " + FileErrors.describe(e) + "."
                    + NOTHING_CHANGED);
        }
        if (projectFiles.size() != 1) {
            return new Refusal("The project folder holds " + projectFiles.size() + " project files ("
                    + String.join(", ", projectFiles) + "), so moving it would take the other projects too. Delete "
                    + "it by hand." + NOTHING_CHANGED);
        }

        Optional<String> guarded = DeleteGuards.refusal(folder, realFolder, request.context(), drives);
        if (guarded.isPresent()) {
            return new Refusal(guarded.get() + NOTHING_CHANGED);
        }
        Optional<String> opened = lockRefusal(folder);
        if (opened.isPresent()) {
            return new Refusal(opened.get());
        }
        return new Located(folder, realFolder, projectName);
    }

    /** Why an editor or another launcher stops the move; empty when neither holds the folder. */
    private Optional<String> lockRefusal(Path folder) {
        EditorLockState editor = locks.probe(folder);
        Optional<String> refusal = describeLock(editor);
        if (refusal.isPresent()) {
            return refusal;
        }
        return describeLock(locks.probeLauncher(folder));
    }

    static Optional<String> describeLock(EditorLockState state) {
        return switch (state) {
            case EditorLockState.Held held -> Optional.of("The project is open in the editor"
                    + (held.pid().isPresent() ? " (process " + held.pid().getAsLong() + ")" : "")
                    + ". Close the editor, then try again." + NOTHING_CHANGED);
            case EditorLockState.Unknown unknown -> Optional.of(EditorLockProbe.HELD_BY_LAUNCHER.equals(unknown.reason())
                    ? "Another VertexForge launcher is changing this project right now. Try again once it is done."
                    + NOTHING_CHANGED
                    : "Could not check whether the editor has the project open (" + unknown.reason() + ")."
                    + NOTHING_CHANGED);
            case EditorLockState.Free ignored -> Optional.empty();
            case EditorLockState.Skipped ignored -> Optional.empty();
        };
    }
}
