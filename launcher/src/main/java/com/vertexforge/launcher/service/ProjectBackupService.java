package com.vertexforge.launcher.service;

import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.TransientIo;

import java.io.IOException;
import java.nio.file.DirectoryNotEmptyException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Duration;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.regex.Pattern;

/**
 * Backs a project up into a zip beside its folder before an engine upgrades it (VK-1690: "Back up and open").
 * <p>
 * The zip holds the project folder as {@code <Folder>/…}, without the editor's state folder and the compiled scripts
 * ({@link ProjectContent#exclusions()}), and is called {@code <Folder>-backup-<saved version>-<UTC time>Z.zip}. It is
 * written as {@code <name>.partial}, read back in full, and only then renamed to its name, in one step that never
 * replaces anything; a name that is taken gets {@code -2}, {@code -3}, … So a zip with the final name is always
 * complete and verified. A failure or a cancel deletes the {@code .partial} this backup created, and nothing else; the
 * project itself is only ever read.
 */
public final class ProjectBackupService {

    /** The first part of every backup's name after the folder's. */
    public static final String BACKUP_INFIX = "-backup-";
    /** The version part of the name when the project does not say which engine saved it. */
    public static final String UNKNOWN_VERSION = "unknown";
    public static final String ZIP_EXTENSION = ".zip";
    /** Added to the zip's name while it is being written. */
    public static final String PARTIAL_EXTENSION = ".partial";
    /** How many names a backup tries ({@code name}, {@code name-2}, … {@code name-20}) before it gives up. */
    static final int MAX_NAME_ATTEMPTS = 20;

    /** The final rename is retried for about 5 s, as a project creation's is: an antivirus scan can hold the zip. */
    static final int COMMIT_ATTEMPTS = ProjectCreator.COMMIT_ATTEMPTS;
    static final Duration COMMIT_FIRST_DELAY = ProjectCreator.COMMIT_FIRST_DELAY;
    private static final int DELETE_ATTEMPTS = 5;
    private static final Duration DELETE_FIRST_DELAY = Duration.ofMillis(50);
    private static final Duration DELETE_MAX_DELAY = Duration.ofMillis(500);

    /** {@code yyyyMMdd-HHmmss} in UTC: no ':' (Windows forbids it in names), and names sort by time. */
    private static final DateTimeFormatter TIMESTAMP =
            DateTimeFormatter.ofPattern("uuuuMMdd-HHmmss").withZone(ZoneOffset.UTC);
    private static final Pattern VERSION_PATTERN = Pattern.compile("(0|[1-9]\\d*)(\\.(0|[1-9]\\d*)){2}");

    /**
     * What to back up.
     *
     * @param projectFile      the project's {@code .vfproj}
     * @param workingDirectory its {@code workingDirectory} member as written, or {@code null}
     * @param savedVersion     the bare {@code MAJOR.MINOR.PATCH} of the engine that saved it, for the zip's name, or
     *                         {@code null} when unknown (anything else also counts as unknown)
     */
    public record BackupRequest(Path projectFile, String workingDirectory, String savedVersion) {
        public BackupRequest {
            Objects.requireNonNull(projectFile, "projectFile");
        }
    }

    /** How a backup ended. */
    public sealed interface BackupOutcome {
        /**
         * The verified zip is in place.
         *
         * @param zip          the zip
         * @param zipBytes     its size
         * @param contentBytes the size of the files it holds
         * @param files        how many files it holds
         * @param notes        what the user should know: links left out, a free-space check that could not run
         */
        record Done(Path zip, long zipBytes, long contentBytes, int files, List<String> notes)
                implements BackupOutcome {
            public Done {
                Objects.requireNonNull(zip, "zip");
                notes = List.copyOf(notes);
            }
        }

        /** Nothing was written, because the backup cannot be made: {@code reason} says why. */
        record Refused(String reason) implements BackupOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        /** The user cancelled; nothing is left behind. */
        record Cancelled() implements BackupOutcome {
        }

        /**
         * Something went wrong; nothing is left behind unless {@code message} names a temporary file the backup could
         * not delete.
         */
        record Failed(String message) implements BackupOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    private final Clock clock;
    private final SpaceProbe space;
    private final DirectoryMover mover;
    private final Duration commitFirstDelay;
    private final Duration commitMaxDelay;

    public ProjectBackupService(Clock clock, SpaceProbe space) {
        this(clock, space, DirectoryMover.SYSTEM, COMMIT_FIRST_DELAY);
    }

    /** For tests: {@code mover} makes the final rename, retried after {@code retryDelay}, doubling up to tenfold. */
    ProjectBackupService(Clock clock, SpaceProbe space, DirectoryMover mover, Duration retryDelay) {
        this.clock = Objects.requireNonNull(clock, "clock");
        this.space = Objects.requireNonNull(space, "space");
        this.mover = Objects.requireNonNull(mover, "mover");
        this.commitFirstDelay = Objects.requireNonNull(retryDelay, "retryDelay");
        this.commitMaxDelay = retryDelay.multipliedBy(10);
    }

    /**
     * Backs the project up, reporting to {@code monitor} and stopping when it asks to (until the final rename).
     * Blocks on the file system, so it only runs on a background thread. Never throws.
     */
    public BackupOutcome backUp(BackupRequest request, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        try {
            return run(request, monitor);
        } catch (RuntimeException e) {
            return new BackupOutcome.Failed("The backup stopped unexpectedly: " + e);
        }
    }

    private BackupOutcome run(BackupRequest request, JobMonitor monitor) {
        // 1: what the backup covers, from the project file alone.
        ProjectContent content = ProjectContent.of(request.projectFile(), request.workingDirectory());
        Optional<String> unavailable = content.unavailableReason();
        if (unavailable.isPresent()) {
            return new BackupOutcome.Refused(unavailable.get());
        }
        Path folder = content.folder();
        Path destination = folder.getParent();

        // 2: the zip goes beside the folder, never into it, even when a link makes the two meet.
        Path realFolder;
        Path realDestination;
        try {
            realFolder = folder.toRealPath();
            realDestination = destination.toRealPath();
        } catch (IOException e) {
            return new BackupOutcome.Failed("Could not read the project folder " + folder + ": " + describe(e));
        }
        if (realDestination.startsWith(realFolder)) {
            return new BackupOutcome.Refused("The folder beside the project, " + destination + ", is inside the "
                    + "project folder itself (" + realFolder + "), so a backup put there would be part of the project.");
        }

        // 3: the files.
        FolderTree tree;
        try {
            tree = FolderTree.scan(folder, content.excluded(), monitor);
        } catch (JobCancelledException e) {
            return new BackupOutcome.Cancelled();
        } catch (IOException e) {
            return new BackupOutcome.Failed("Could not read the project's files: " + describe(e));
        }
        // A backup without the project file would not restore to a project (a link is never followed).
        String projectFileName = content.projectFile().getFileName().toString();
        if (tree.files().stream().noneMatch(file -> file.relative().equalsIgnoreCase(projectFileName))) {
            return new BackupOutcome.Refused("The project file " + content.projectFile() + " is not a file in the "
                    + "project folder (a link is never backed up), so the backup would have none.");
        }

        // 4: the name, and whether its longest form fits Windows' path limit.
        String rootEntry = folder.getFileName().toString();
        String stem = rootEntry + BACKUP_INFIX + versionPart(request.savedVersion()) + "-"
                + TIMESTAMP.format(clock.instant()) + "Z";
        String longest = destination.resolve(candidate(stem, MAX_NAME_ATTEMPTS)).toString();
        if (longest.length() > ProjectNameRules.MAX_PATH_CHARS) {
            return new BackupOutcome.Refused("The backup's path would be up to " + longest.length()
                    + " characters long, more than the " + ProjectNameRules.MAX_PATH_CHARS + " Windows allows. Move "
                    + "the project to a folder with a shorter path, or shorten its folder's name.");
        }

        // 5: room for the zip.
        List<String> notes = new ArrayList<>(tree.notes());
        long required = FolderZipWriter.requiredSpace(tree, rootEntry);
        try {
            long usable = space.usableBytes(destination);
            if (usable < required) {
                return new BackupOutcome.Refused("Not enough free space for the backup in " + destination
                        + ": it needs up to " + JobMonitor.formatBytes(required) + ", but only "
                        + JobMonitor.formatBytes(usable) + " is free.");
            }
        } catch (IOException e) {
            notes.add("Could not check the free space in " + destination + " (" + describe(e)
                    + "); the backup went ahead.");
        }

        // 6: write and verify the .partial. From its creation on it is ours, and the only thing a failure deletes.
        FolderZipWriter.Written written = null;
        try {
            for (int attempt = 1; written == null; attempt++) {
                Path partial = destination.resolve(candidate(stem, attempt) + PARTIAL_EXTENSION);
                try {
                    written = FolderZipWriter.write(tree, rootEntry, partial, monitor);
                } catch (FileAlreadyExistsException e) {
                    // A .partial of an earlier backup that stopped; it is not ours to delete. (The writer creates its
                    // target first, so it wrote nothing here.)
                    if (attempt >= MAX_NAME_ATTEMPTS) {
                        throw e;
                    }
                }
            }
            FolderZipWriter.verify(written, monitor);
            monitor.checkCancelled();
        } catch (JobCancelledException e) {
            return abandon(written, new BackupOutcome.Cancelled());
        } catch (IOException | RuntimeException e) {
            return abandon(written, new BackupOutcome.Failed("Could not write the backup: " + describe(e)));
        } catch (Error e) {
            // Out of memory and the like: still take the .partial away (the writer removes its own on a failure
            // while writing), then let FileJob report the Error.
            abandon(written, new BackupOutcome.Failed("The backup stopped unexpectedly: " + e));
            throw e;
        }

        long zipBytes;
        try {
            zipBytes = Files.size(written.zip());
        } catch (IOException e) {
            return abandon(written, new BackupOutcome.Failed("Could not read the backup back: " + describe(e)));
        }

        // 7: the commit. A cancel is no longer possible.
        return commit(written, destination, stem, zipBytes, tree, notes);
    }

    private BackupOutcome commit(FolderZipWriter.Written written, Path destination, String stem, long zipBytes,
                                 FolderTree tree, List<String> notes) {
        Path partial = written.zip();
        for (int attempt = 1; attempt <= MAX_NAME_ATTEMPTS; attempt++) {
            Path zip = destination.resolve(candidate(stem, attempt));
            try {
                TransientIo.retry(() -> mover.move(partial, zip), COMMIT_ATTEMPTS, commitFirstDelay, commitMaxDelay,
                        "moving the backup into place");
            } catch (FileAlreadyExistsException | DirectoryNotEmptyException e) {
                // Something has this name already; it is never replaced. Try the next one.
                continue;
            } catch (IOException | RuntimeException e) {
                return abandon(written, new BackupOutcome.Failed("Could not move the backup into place: "
                        + describe(e) + ". Another program (often an antivirus scan) may be using it. Try again."));
            }

            // Files.move returns quietly when source and target are the same file; make sure the rename happened.
            if (!Files.isRegularFile(zip, LinkOption.NOFOLLOW_LINKS) || Files.exists(partial, LinkOption.NOFOLLOW_LINKS)) {
                return abandon(written, new BackupOutcome.Failed("Could not move the backup into place: " + partial
                        + " is still there."));
            }
            return new BackupOutcome.Done(zip, zipBytes, written.contentBytes(), tree.files().size(), notes);
        }
        return abandon(written, new BackupOutcome.Failed("Could not find a free name for the backup in "
                + destination + ": " + candidate(stem, 1) + " up to " + candidate(stem, MAX_NAME_ATTEMPTS)
                + " are all taken."));
    }

    /**
     * Deletes the {@code .partial} this backup wrote (when it got that far) and returns {@code outcome}; when the file
     * cannot be deleted, a failure that says so instead.
     */
    private static BackupOutcome abandon(FolderZipWriter.Written written, BackupOutcome outcome) {
        if (written == null) {
            return outcome;
        }
        Path partial = written.zip();
        try {
            TransientIo.retry(() -> Files.deleteIfExists(partial), DELETE_ATTEMPTS, DELETE_FIRST_DELAY,
                    DELETE_MAX_DELAY, "deleting " + partial);
            return outcome;
        } catch (IOException e) {
            String left = "The temporary file " + partial + " could not be deleted (" + describe(e)
                    + "); you can delete it.";
            return switch (outcome) {
                case BackupOutcome.Failed failed -> new BackupOutcome.Failed(failed.message() + " " + left);
                case BackupOutcome.Cancelled ignored -> new BackupOutcome.Failed("The backup was cancelled. " + left);
                default -> new BackupOutcome.Failed(left);
            };
        }
    }

    /** {@code stem.zip} for the first attempt, {@code stem-N.zip} after that. */
    static String candidate(String stem, int attempt) {
        return (attempt == 1 ? stem : stem + "-" + attempt) + ZIP_EXTENSION;
    }

    private static String versionPart(String savedVersion) {
        return savedVersion != null && VERSION_PATTERN.matcher(savedVersion).matches() ? savedVersion : UNKNOWN_VERSION;
    }

    private static String describe(Throwable error) {
        return FileErrors.describe(error);
    }
}
