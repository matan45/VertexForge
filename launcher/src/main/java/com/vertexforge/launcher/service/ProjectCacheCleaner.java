package com.vertexforge.launcher.service;

import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.InvalidProjectFileException;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.project.ProjectFileReader;
import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.FileTrees;
import com.vertexforge.launcher.util.RelativePathRules;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * "Clean Caches…" (VK-1695): deletes the build-output folders an engine says it rebuilds, and nothing else. The
 * folders come from the engine's {@code engine.json} ({@code projectCacheDirs}, relative to the project's working
 * directory; today {@code scripts/compiled}), never from the project, so a project file cannot point the delete at
 * anything else.
 * <p>
 * A listed folder is cleaned only when it is a real folder in the project, reached without a link: a link or junction
 * at it or on the way to it, a file in its place, or a folder that holds the project's own scripts is left alone and
 * reported. The folders are deleted permanently (they are rebuilt, so there is nothing to restore), while the launcher
 * {@linkplain EditorLockProbe#hold holds} the project, so an editor started meanwhile refuses to open it. The preview
 * only probes the lock, as it writes nothing.
 */
public final class ProjectCacheCleaner {

    /**
     * What to clean.
     *
     * @param projectFile      the project's {@code .vfproj}
     * @param workingDirectory its {@code workingDirectory} member as read, or {@code null}
     * @param cacheDirs        the engine's {@code projectCacheDirs}, relative to the working directory
     */
    public record CleanRequest(Path projectFile, String workingDirectory, List<String> cacheDirs) {
        public CleanRequest {
            Objects.requireNonNull(projectFile, "projectFile");
            cacheDirs = List.copyOf(cacheDirs);
        }
    }

    /**
     * One listed folder, as found.
     *
     * @param entry    the engine's entry
     * @param relative the folder relative to the project folder, '/'-separated
     * @param path     the folder
     * @param state    whether it is there to be cleaned
     * @param bytes    the size of its files ({@link State#PRESENT} only)
     * @param files    how many files it holds ({@link State#PRESENT} only)
     * @param reason   why it is left alone ({@link State#SKIPPED} only); empty otherwise
     */
    public record CacheFolder(String entry, String relative, Path path, State state, long bytes, int files,
                              String reason) {
        public enum State {
            /** A real folder in the project: it is cleaned. */
            PRESENT,
            /** Not there: nothing to clean. */
            MISSING,
            /** There, but left alone ({@link #reason()} says why). */
            SKIPPED
        }

        public CacheFolder {
            Objects.requireNonNull(entry, "entry");
            Objects.requireNonNull(relative, "relative");
            Objects.requireNonNull(path, "path");
            Objects.requireNonNull(state, "state");
            reason = reason == null ? "" : reason;
        }
    }

    /** How {@link #preview} ended. */
    public sealed interface PreviewOutcome {
        /** Every listed folder, in the engine's order. */
        record Ready(List<CacheFolder> folders) implements PreviewOutcome {
            public Ready {
                folders = List.copyOf(folders);
            }

            /** The folders that would be cleaned. */
            public List<CacheFolder> present() {
                return folders.stream().filter(folder -> folder.state() == CacheFolder.State.PRESENT).toList();
            }
        }

        /** The project cannot be cleaned now: {@code reason} says why. */
        record Refused(String reason) implements PreviewOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        record Cancelled() implements PreviewOutcome {
        }

        record Failed(String message) implements PreviewOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    /** How {@link #clean} ended. */
    public sealed interface CleanOutcome {
        /**
         * The listed folders were cleaned as far as they could be.
         *
         * @param freedBytes the size of the folders that are gone
         * @param cleaned    the folders that are gone
         * @param skipped    the folders left alone, with why
         * @param failures   what could not be deleted
         */
        record Done(long freedBytes, List<CacheFolder> cleaned, List<CacheFolder> skipped, List<String> failures)
                implements CleanOutcome {
            public Done {
                cleaned = List.copyOf(cleaned);
                skipped = List.copyOf(skipped);
                failures = List.copyOf(failures);
            }
        }

        /** Nothing was deleted: {@code reason} says why. */
        record Refused(String reason) implements CleanOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        /** The user stopped it between two folders; {@code cleaned} were already gone. */
        record Cancelled(long freedBytes, List<CacheFolder> cleaned) implements CleanOutcome {
            public Cancelled {
                cleaned = List.copyOf(cleaned);
            }
        }

        record Failed(String message) implements CleanOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    /** The project's own scripts, relative to the working directory: no listed folder may hold or lie in them. */
    static final List<String> OWN_SCRIPTS = List.of("scripts/lib", "scripts/game", ProjectContent.SCRIPTS_PROJECT);

    private final EditorLockProbe locks;

    public ProjectCacheCleaner(EditorLockProbe locks) {
        this.locks = Objects.requireNonNull(locks, "locks");
    }

    /** What {@link #clean} would delete. Only reads. Blocks: a job's thread. Never throws. */
    public PreviewOutcome preview(CleanRequest request, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        try {
            ProjectContent content = ProjectContent.of(request.projectFile(), request.workingDirectory());
            Optional<String> unavailable = unavailableReason(content);
            if (unavailable.isPresent()) {
                return new PreviewOutcome.Refused(unavailable.get());
            }
            Optional<String> opened = describeLock(locks.probe(content.folder()))
                    .or(() -> describeLock(locks.probeLauncher(content.folder())));
            if (opened.isPresent()) {
                return new PreviewOutcome.Refused(opened.get());
            }
            Path realFolder = content.folder().toRealPath();
            List<CacheFolder> folders = new ArrayList<>();
            for (String entry : request.cacheDirs()) {
                folders.add(inspect(content, realFolder, entry, monitor));
            }
            return new PreviewOutcome.Ready(folders);
        } catch (JobCancelledException e) {
            return new PreviewOutcome.Cancelled();
        } catch (IOException e) {
            return new PreviewOutcome.Failed("Could not look at the project: " + FileErrors.describe(e));
        } catch (RuntimeException e) {
            return new PreviewOutcome.Failed("The check stopped unexpectedly: " + e);
        }
    }

    /**
     * Deletes every listed folder that is {@link CacheFolder.State#PRESENT} now, holding the project for the whole job.
     * Cancelling stops it between two folders. Blocks: a job's thread (the hold is taken and let go on it). Never
     * throws.
     */
    public CleanOutcome clean(CleanRequest request, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        ProjectContent content = ProjectContent.of(request.projectFile(), request.workingDirectory());
        Optional<String> unavailable = unavailableReason(content);
        if (unavailable.isPresent()) {
            return new CleanOutcome.Refused(unavailable.get());
        }
        EditorLockProbe.HoldResult result = locks.hold(content.folder());
        if (result instanceof EditorLockProbe.HoldResult.NotHeld notHeld) {
            return new CleanOutcome.Refused(describeLock(notHeld.state()).orElse("The project could not be locked."));
        }
        try (EditorLockProbe.Hold ignored = ((EditorLockProbe.HoldResult.Holding) result).hold()) {
            // The folders were found with the working directory as first read; one changed since cannot be trusted.
            String workingDirectory;
            try {
                workingDirectory = ProjectFileReader.read(request.projectFile()).workingDirectory();
            } catch (InvalidProjectFileException e) {
                return new CleanOutcome.Refused("The project file cannot be read as a project now: "
                        + e.getMessage() + ". Nothing was deleted.");
            }
            if (!Objects.equals(workingDirectory, request.workingDirectory())) {
                return new CleanOutcome.Refused("The project file's working directory changed since its caches were "
                        + "looked at (now \"" + workingDirectory + "\"). Nothing was deleted; try again.");
            }
            Path realFolder = content.folder().toRealPath();
            long freed = 0;
            List<CacheFolder> cleaned = new ArrayList<>();
            List<CacheFolder> skipped = new ArrayList<>();
            List<String> failures = new ArrayList<>();
            for (String entry : request.cacheDirs()) {
                if (monitor.cancelRequested()) {
                    return new CleanOutcome.Cancelled(freed, cleaned);
                }
                CacheFolder folder = inspect(content, realFolder, entry, JobMonitor.NONE);
                switch (folder.state()) {
                    case MISSING -> {
                        // Nothing to clean.
                    }
                    case SKIPPED -> skipped.add(folder);
                    case PRESENT -> {
                        List<IOException> problems = new ArrayList<>();
                        FileTrees.deleteTree(folder.path(), problems);
                        if (Files.notExists(folder.path(), LinkOption.NOFOLLOW_LINKS)) {
                            freed += folder.bytes();
                            cleaned.add(folder);
                        } else {
                            failures.add(folder.relative() + " could not be deleted completely"
                                    + (problems.isEmpty() ? "" : ": " + FileErrors.describe(problems.getFirst()))
                                    + (problems.size() > 1 ? " (and " + (problems.size() - 1) + " more)" : "")
                                    + ". A program may be using it, or a file in it is read-only.");
                        }
                    }
                }
                monitor.progress(JobMonitor.Phase.COMMITTING, cleaned.size() + skipped.size() + failures.size(),
                        request.cacheDirs().size());
            }
            return new CleanOutcome.Done(freed, cleaned, skipped, failures);
        } catch (IOException e) {
            return new CleanOutcome.Failed("The cleaning stopped: " + FileErrors.describe(e));
        } catch (RuntimeException e) {
            return new CleanOutcome.Failed("The cleaning stopped unexpectedly: " + e);
        }
    }

    /**
     * One listed folder, checked from the project folder down without following a link. Reads only.
     *
     * @throws JobCancelledException when the monitor asks to stop while the folder's size is counted
     */
    static CacheFolder inspect(ProjectContent content, Path realFolder, String entry, JobMonitor monitor)
            throws JobCancelledException {
        Path folder = content.folder();
        if (!RelativePathRules.isValid(entry) || RelativePathRules.isEditorState(entry)) {
            return skipped(entry, entry, folder, "\"" + entry + "\" is not a folder name the launcher accepts");
        }
        String relative = content.relativeToFolder(entry);
        Path path = folder.resolve(relative);
        if (RelativePathRules.isEditorState(relative)) {
            return skipped(entry, relative, path, "it lies in the editor's own " + RelativePathRules.EDITOR_STATE_DIR
                    + " folder");
        }
        for (String own : OWN_SCRIPTS) {
            String ownRelative = content.relativeToFolder(own);
            if (overlaps(relative, ownRelative)) {
                return skipped(entry, relative, path, "it overlaps the project's own scripts (" + ownRelative + ")");
            }
        }

        // Down from the project folder, one name at a time, never through a link.
        String[] names = relative.split("/");
        Path current = folder;
        for (int i = 0; i < names.length; i++) {
            current = current.resolve(names[i]);
            boolean last = i == names.length - 1;
            BasicFileAttributes attributes;
            try {
                attributes = Files.readAttributes(current, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
            } catch (NoSuchFileException e) {
                return new CacheFolder(entry, relative, path, CacheFolder.State.MISSING, 0, 0, "");
            } catch (IOException e) {
                return skipped(entry, relative, path, "it could not be read: " + FileErrors.describe(e));
            }
            boolean link = attributes.isSymbolicLink()
                    || (attributes.isDirectory() && attributes.isOther() && !FileTrees.isOwnFolder(current));
            if (link) {
                return skipped(entry, relative, path, last ? "it is a link; what it points to is left alone"
                        : "it is reached through a link (" + current + ")");
            }
            if (!attributes.isDirectory()) {
                return last
                        ? skipped(entry, relative, path, "it is a file, not a folder")
                        : new CacheFolder(entry, relative, path, CacheFolder.State.MISSING, 0, 0, "");
            }
        }

        try {
            Path real = path.toRealPath();
            Path expected = realFolder.resolve(relative).normalize();
            if (!real.toString().equalsIgnoreCase(expected.toString())) {
                return skipped(entry, relative, path, "it leads outside the project (" + real + ")");
            }
            FolderTree tree = FolderTree.scan(path, ignored -> false, monitor);
            return new CacheFolder(entry, relative, path, CacheFolder.State.PRESENT, tree.totalBytes(),
                    tree.files().size(), "");
        } catch (JobCancelledException e) {
            throw e;
        } catch (IOException e) {
            return skipped(entry, relative, path, "it could not be read: " + FileErrors.describe(e));
        }
    }

    private static CacheFolder skipped(String entry, String relative, Path path, String reason) {
        return new CacheFolder(entry, relative, path, CacheFolder.State.SKIPPED, 0, 0, reason);
    }

    /** Whether one '/'-relative path is the other or lies in it, ignoring case. */
    static boolean overlaps(String a, String b) {
        return sameOrInside(a, b) || sameOrInside(b, a);
    }

    private static boolean sameOrInside(String path, String base) {
        return path.equalsIgnoreCase(base)
                || (path.length() > base.length() && path.charAt(base.length()) == '/'
                && path.regionMatches(true, 0, base, 0, base.length()));
    }

    /** Why a project's caches cannot be found; empty when its working directory lies in its folder. */
    private static Optional<String> unavailableReason(ProjectContent content) {
        return switch (content.scope()) {
            case INSIDE -> Optional.empty();
            case OUTSIDE -> Optional.of("Its working directory (" + content.workingDirectory() + ") lies outside the "
                    + "project folder, so the launcher does not clean it.");
            case NO_PARENT -> Optional.of("The project file is at the top of a drive, so the launcher does not "
                    + "clean it.");
            case INVALID -> Optional.of("Its working directory is missing or is not a valid path, so the launcher "
                    + "cannot tell where its caches are.");
        };
    }

    /** Why an editor's or a launcher's lock stops the cleaning; empty when neither holds the project. */
    static Optional<String> describeLock(EditorLockState state) {
        return switch (state) {
            case EditorLockState.Held held -> Optional.of("The project is open in the editor"
                    + (held.pid().isPresent() ? " (process " + held.pid().getAsLong() + ")" : "")
                    + ". Close the editor, then clean its caches.");
            case EditorLockState.Unknown unknown -> Optional.of(EditorLockProbe.HELD_BY_LAUNCHER.equals(unknown.reason())
                    ? "Another VertexForge launcher is changing this project right now. Try again once it is done."
                    : "Could not check whether the editor has the project open (" + unknown.reason() + "), so "
                    + "nothing was deleted.");
            case EditorLockState.Free ignored -> Optional.empty();
            case EditorLockState.Skipped ignored -> Optional.empty();
        };
    }
}
