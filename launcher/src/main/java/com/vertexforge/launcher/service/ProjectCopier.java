package com.vertexforge.launcher.service;

import com.vertexforge.launcher.project.InvalidProjectFileException;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.project.ProjectFileEdit;
import com.vertexforge.launcher.project.ProjectFileReader;
import com.vertexforge.launcher.project.ProjectFiles;
import com.vertexforge.launcher.project.ScriptsProjectEdit;
import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.FileTrees;
import com.vertexforge.launcher.util.TransientIo;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.DirectoryNotEmptyException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Supplier;

/**
 * Copies a project into a new folder beside it: VK-1690's "Open a copy", so that an engine upgrade changes the copy and
 * never the original, and VK-1695's "Duplicate".
 * <p>
 * The copy holds the project folder without the editor's state folder and the compiled scripts
 * ({@link ProjectContent#exclusions()}). Its {@code .vfproj} gets the copy's name as {@code projectName}, with the paths
 * that pointed into the original re-pointed into the copy ({@link ProjectFileEdit#forCopy}); it keeps its file name, or
 * for a duplicate becomes {@code <name>.vfproj} ({@link CopyRequest.ProjectFileName}). Its scripts project
 * ({@code <workingDirectory>/scripts/scripts.mtproj}) gets the copy's name too ({@link ScriptsProjectEdit}, VK-1695);
 * one that cannot be edited is copied as it is, with a note. As a project creation does (VK-1679), the copier checks
 * everything first, builds the copy in a sibling staging folder ({@code .<name>.creating-<uuid>}) and renames it into
 * place in one step that never replaces anything. A failure or a cancel before that rename deletes the staging folder
 * and nothing else; the original is only ever read.
 */
public final class ProjectCopier {

    /**
     * What a copy needs besides the files: a cluster's slack for every file and folder (4 KiB, NTFS's default), and
     * 1 MiB for the folder entries themselves.
     */
    static final long PER_ENTRY_MARGIN_BYTES = 4096;
    static final long MARGIN_BYTES = 1024 * 1024;

    private static final int CHUNK_BYTES = 64 * 1024;
    private static final int COMMIT_ATTEMPTS = ProjectCreator.COMMIT_ATTEMPTS;
    private static final Duration COMMIT_FIRST_DELAY = ProjectCreator.COMMIT_FIRST_DELAY;

    /**
     * What to copy.
     *
     * @param projectFile      the original's {@code .vfproj}
     * @param workingDirectory its {@code workingDirectory} member as written, or {@code null}
     * @param newName          the copy's folder name and {@code projectName}, exactly as typed
     * @param projectFileName  what the copy's {@code .vfproj} is called
     */
    public record CopyRequest(Path projectFile, String workingDirectory, String newName,
                              ProjectFileName projectFileName) {

        /** What the copy's {@code .vfproj} is called. */
        public enum ProjectFileName {
            /** The original's file name (VK-1690 "Open a copy"). */
            KEEP,
            /** {@code <newName>.vfproj}, as a project creation names it (VK-1695 "Duplicate"). */
            NEW_NAME
        }

        public CopyRequest {
            Objects.requireNonNull(projectFile, "projectFile");
            Objects.requireNonNull(projectFileName, "projectFileName");
        }

        /** A copy whose {@code .vfproj} keeps its file name ({@link ProjectFileName#KEEP}). */
        public CopyRequest(Path projectFile, String workingDirectory, String newName) {
            this(projectFile, workingDirectory, newName, ProjectFileName.KEEP);
        }
    }

    /** A file of the original whose copy gets other bytes, under {@code targetRelative} in the copy. */
    private record Replacement(String targetRelative, byte[] bytes) {
    }

    /** How a copy ended. */
    public sealed interface CopyOutcome {
        /**
         * The copy is in place.
         *
         * @param projectFile the copy's {@code .vfproj}
         * @param folder      the copy's folder
         * @param notes       what the user should know: links left out, a free-space check that could not run
         */
        record Done(Path projectFile, Path folder, List<String> notes) implements CopyOutcome {
            public Done {
                Objects.requireNonNull(projectFile, "projectFile");
                Objects.requireNonNull(folder, "folder");
                notes = List.copyOf(notes);
            }
        }

        /** Nothing was written, because the copy cannot be made as asked: {@code reason} says why. */
        record Refused(String reason) implements CopyOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        /** The user cancelled; nothing is left behind. */
        record Cancelled() implements CopyOutcome {
        }

        /**
         * Something went wrong; nothing is left behind unless {@code message} names a temporary folder the copier
         * could not delete.
         */
        record Failed(String message) implements CopyOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    private final Supplier<UUID> ids;
    private final DirectoryMover mover;
    private final SpaceProbe space;
    private final Duration commitFirstDelay;
    private final Duration commitMaxDelay;

    public ProjectCopier(SpaceProbe space) {
        this(UUID::randomUUID, DirectoryMover.SYSTEM, space, COMMIT_FIRST_DELAY);
    }

    /** For tests: {@code mover} makes the final rename, retried after {@code retryDelay}, doubling up to tenfold. */
    ProjectCopier(Supplier<UUID> ids, DirectoryMover mover, SpaceProbe space, Duration retryDelay) {
        this.ids = Objects.requireNonNull(ids, "ids");
        this.mover = Objects.requireNonNull(mover, "mover");
        this.space = Objects.requireNonNull(space, "space");
        this.commitFirstDelay = Objects.requireNonNull(retryDelay, "retryDelay");
        this.commitMaxDelay = retryDelay.multipliedBy(10);
    }

    /**
     * Copies the project, reporting to {@code monitor} and stopping when it asks to (until the final rename). Blocks
     * on the file system, so it only runs on a background thread. Never throws.
     */
    public CopyOutcome copy(CopyRequest request, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        try {
            return run(request, monitor);
        } catch (RuntimeException e) {
            return new CopyOutcome.Failed("The copy stopped unexpectedly: " + e);
        }
    }

    private CopyOutcome run(CopyRequest request, JobMonitor monitor) {
        // 1-3: what the copy covers, its name, and where it goes. Nothing is written before every check passed.
        ProjectContent content = ProjectContent.of(request.projectFile(), request.workingDirectory());
        Optional<String> unavailable = content.unavailableReason();
        if (unavailable.isPresent()) {
            return new CopyOutcome.Refused(unavailable.get());
        }
        String name = request.newName();
        Optional<ProjectNameRules.Problem> nameProblem = ProjectNameRules.checkName(name);
        if (nameProblem.isPresent()) {
            return new CopyOutcome.Refused(nameProblem.get().message());
        }
        Path folder = content.folder();
        Path parent = folder.getParent();
        Path target;
        try {
            target = parent.resolve(name);
        } catch (InvalidPathException e) {
            return new CopyOutcome.Refused("The name can't be used as a folder name: " + e.getReason());
        }

        // 4: the name must be free (an empty folder too: the copy never takes over anything), and the copy must not
        // land inside the original, even when a link makes the two meet.
        try {
            BasicFileAttributes existing = Files.readAttributes(target, BasicFileAttributes.class,
                    LinkOption.NOFOLLOW_LINKS);
            return new CopyOutcome.Refused((existing.isDirectory() ? "A folder" : "A file") + " named \"" + name
                    + "\" already exists in " + parent + ".");
        } catch (NoSuchFileException e) {
            // Free.
        } catch (IOException e) {
            return new CopyOutcome.Failed("Could not check " + target + ": " + describe(e));
        }
        Path realFolder;
        try {
            realFolder = folder.toRealPath();
            if (parent.toRealPath().startsWith(realFolder)) {
                return new CopyOutcome.Refused("The folder beside the project, " + parent + ", is inside the project "
                        + "folder itself (" + realFolder + "), so a copy put there would be part of the original.");
            }
        } catch (IOException e) {
            return new CopyOutcome.Failed("Could not read the project folder " + folder + ": " + describe(e));
        }

        // 5: the files.
        FolderTree tree;
        try {
            tree = FolderTree.scan(folder, content.excluded(), monitor);
        } catch (JobCancelledException e) {
            return new CopyOutcome.Cancelled();
        } catch (IOException e) {
            return new CopyOutcome.Failed("Could not read the project's files: " + describe(e));
        }
        String projectFileName = content.projectFile().getFileName().toString();
        FolderTree.FileEntry projectEntry = null;
        for (FolderTree.FileEntry file : tree.files()) {
            if (file.relative().equalsIgnoreCase(projectFileName)) {
                projectEntry = file;
            }
        }
        if (projectEntry == null) {
            return new CopyOutcome.Refused("The project file " + content.projectFile() + " is not a file in the "
                    + "project folder (a link is never copied), so the copy would have none.");
        }
        // A duplicate's project file is named after it; nothing else at the top of the folder may have that name.
        String copyFileName = request.projectFileName() == CopyRequest.ProjectFileName.NEW_NAME
                ? name + ProjectFiles.EXTENSION
                : projectEntry.relative();
        if (!copyFileName.equalsIgnoreCase(projectEntry.relative())) {
            Optional<String> clash = topLevelEntryNamed(tree, copyFileName);
            if (clash.isPresent()) {
                return new CopyOutcome.Refused("The project folder already holds \"" + clash.get() + "\", so the "
                        + "copy's project file could not be called \"" + copyFileName + "\". Choose another name.");
            }
        }

        // 6-7: the path budget and the room for the files.
        Optional<ProjectNameRules.Problem> lengthProblem = ProjectNameRules.checkPathLength(parent, name,
                Math.max(tree.deepestRelativeLength(), copyFileName.length()));
        if (lengthProblem.isPresent()) {
            return new CopyOutcome.Refused(lengthProblem.get().message());
        }
        List<String> notes = new ArrayList<>(tree.notes());
        long required = tree.totalBytes() + MARGIN_BYTES
                + PER_ENTRY_MARGIN_BYTES * (tree.files().size() + tree.folders().size());
        try {
            long usable = space.usableBytes(parent);
            if (usable < required) {
                return new CopyOutcome.Refused("Not enough free space for the copy in " + parent + ": it needs about "
                        + JobMonitor.formatBytes(required) + ", but only " + JobMonitor.formatBytes(usable)
                        + " is free.");
            }
        } catch (IOException e) {
            notes.add("Could not check the free space in " + parent + " (" + describe(e) + "); the copy went ahead.");
        }

        // 8: the copy's .vfproj, from the original read now, so that a file the launcher cannot read stops the copy
        // before anything is written.
        byte[] projectBytes;
        try (InputStream in = Files.newInputStream(projectEntry.path())) {
            // One byte more than allowed tells a file at the limit from a longer one, without trusting its size.
            projectBytes = ProjectFileEdit.forCopy(in.readNBytes(ProjectFileReader.MAX_BYTES + 1), name, folder,
                    target);
        } catch (InvalidProjectFileException e) {
            return new CopyOutcome.Refused("The project file can't be copied: " + e.getMessage() + ".");
        } catch (IOException e) {
            return new CopyOutcome.Failed("Could not read the project file " + projectEntry.path() + ": "
                    + describe(e));
        }
        // The copy's paths, as its editor will resolve them, must not reach the original by any spelling.
        Optional<String> reach;
        try {
            reach = leadsIntoOriginal(projectBytes, target, folder);
        } catch (InvalidProjectFileException e) {
            return new CopyOutcome.Refused("The project file can't be copied: " + e.getMessage() + ".");
        }
        if (reach.isPresent()) {
            return new CopyOutcome.Refused(reach.get());
        }
        Map<FolderTree.FileEntry, Replacement> replacements = new HashMap<>();
        replacements.put(projectEntry, new Replacement(copyFileName, projectBytes));

        // 8b: the scripts project gets the copy's name too (VK-1695), read before anything is written.
        String scriptsRelative = content.scriptsProjectRelative();
        for (FolderTree.FileEntry file : tree.files()) {
            if (!file.relative().equalsIgnoreCase(scriptsRelative)) {
                continue;
            }
            byte[] original;
            try (InputStream in = Files.newInputStream(file.path())) {
                // One byte more than the edit takes tells a file at the limit from a longer one.
                original = in.readNBytes(ScriptsProjectEdit.MAX_BYTES + 1);
            } catch (IOException e) {
                return new CopyOutcome.Failed("Could not read " + file.path() + ": " + describe(e));
            }
            switch (ScriptsProjectEdit.rename(original, name)) {
                case ScriptsProjectEdit.Result.Renamed renamed ->
                        replacements.put(file, new Replacement(file.relative(), renamed.bytes()));
                case ScriptsProjectEdit.Result.Unchanged unchanged -> notes.add(file.relative() + " was copied as it "
                        + "is, so it keeps the original's name: " + unchanged.reason() + ".");
            }
        }

        if (monitor.cancelRequested()) {
            return new CopyOutcome.Cancelled();
        }

        // 9: from here on this folder is ours, and it is the only thing a failure may delete.
        Path staging = parent.resolve("." + name + ProjectCreator.STAGING_INFIX + ids.get());
        try {
            Files.createDirectory(staging);
        } catch (IOException e) {
            return new CopyOutcome.Failed("Could not create a folder in " + parent + ": " + describe(e));
        }
        try {
            copyInto(staging, tree, replacements, monitor);
            monitor.checkCancelled();
        } catch (JobCancelledException e) {
            return abandon(parent, name, staging, new CopyOutcome.Cancelled());
        } catch (IOException | RuntimeException e) {
            return abandon(parent, name, staging, new CopyOutcome.Failed("Could not copy the project: "
                    + describe(e)));
        } catch (Error e) {
            // Out of memory and the like: still take the staging folder away, then let FileJob report the Error.
            abandon(parent, name, staging, new CopyOutcome.Failed("The copy stopped unexpectedly: " + e));
            throw e;
        }

        // 10: the commit. A cancel is no longer possible.
        return commit(parent, name, staging, target, target.resolve(copyFileName), notes);
    }

    /**
     * The name of a file or folder at the top of {@code tree} that is {@code name}, ignoring case, as Windows compares
     * names; empty when there is none.
     */
    private static Optional<String> topLevelEntryNamed(FolderTree tree, String name) {
        for (FolderTree.FileEntry file : tree.files()) {
            if (file.relative().indexOf('/') < 0 && file.relative().equalsIgnoreCase(name)) {
                return Optional.of(file.relative());
            }
        }
        for (String folder : tree.folders()) {
            if (folder.indexOf('/') < 0 && folder.equalsIgnoreCase(name)) {
                return Optional.of(folder);
            }
        }
        return Optional.empty();
    }

    /**
     * Writes every folder and file of {@code tree} into {@code staging}, each file new ({@code CREATE_NEW}) with its
     * modification time; a file in {@code replacements} gets its replacement's bytes instead of its content, under
     * the replacement's path. Never {@code Files.copy}: it keeps a source's read-only flag, which would stop a failed
     * copy from deleting its staging folder.
     */
    private static void copyInto(Path staging, FolderTree tree, Map<FolderTree.FileEntry, Replacement> replacements,
                                 JobMonitor monitor) throws IOException {
        for (String folder : tree.folders()) {
            monitor.checkCancelled();
            Files.createDirectory(staging.resolve(folder));
        }

        long total = tree.totalBytes();
        long done = 0;
        byte[] buffer = new byte[CHUNK_BYTES];
        for (FolderTree.FileEntry file : tree.files()) {
            monitor.checkCancelled();
            Path copy = staging.resolve(file.relative());
            Replacement replacement = replacements.get(file);
            if (replacement != null) {
                Files.write(staging.resolve(replacement.targetRelative()), replacement.bytes(),
                        StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE);
                done += file.size();
                total = Math.max(total, done);
                monitor.progress(JobMonitor.Phase.COPYING, done, total);
                continue;
            }
            try (InputStream in = Files.newInputStream(file.path());
                 OutputStream out = Files.newOutputStream(copy, StandardOpenOption.CREATE_NEW,
                         StandardOpenOption.WRITE)) {
                for (int read = in.read(buffer); read >= 0; read = in.read(buffer)) {
                    if (read == 0) {
                        continue;
                    }
                    out.write(buffer, 0, read);
                    done += read;
                    // A file that grew since the scan must not make the bar run past its end.
                    total = Math.max(total, done);
                    monitor.progress(JobMonitor.Phase.COPYING, done, total);
                    monitor.checkCancelled();
                }
            }
            Files.setLastModifiedTime(copy, file.lastModified());
        }
    }

    private CopyOutcome commit(Path parent, String name, Path staging, Path target, Path projectFile,
                               List<String> notes) {
        try {
            TransientIo.retry(() -> mover.move(staging, target), COMMIT_ATTEMPTS, commitFirstDelay, commitMaxDelay,
                    "moving the copy into place");
        } catch (FileAlreadyExistsException | DirectoryNotEmptyException e) {
            return abandon(parent, name, staging, new CopyOutcome.Failed("Something else created \"" + name
                    + "\" in " + parent + " while the copy was being made. Nothing was changed."));
        } catch (IOException | RuntimeException e) {
            return abandon(parent, name, staging, new CopyOutcome.Failed("Could not move the copy into place: "
                    + describe(e) + ". Another program (often an antivirus scan) may be using it. Try again."));
        }

        // Files.move returns quietly when source and target are the same file; make sure the rename really happened.
        if (!Files.isDirectory(target, LinkOption.NOFOLLOW_LINKS) || Files.exists(staging, LinkOption.NOFOLLOW_LINKS)) {
            return abandon(parent, name, staging, new CopyOutcome.Failed("Could not move the copy into place: "
                    + staging + " is still there."));
        }
        return new CopyOutcome.Done(projectFile, target, notes);
    }

    /**
     * Deletes the staging folder and returns {@code outcome}; when the folder cannot be deleted completely, a failure
     * that says so instead.
     */
    private static CopyOutcome abandon(Path parent, String name, Path staging, CopyOutcome outcome) {
        if (deleteStaging(parent, name, staging)) {
            return outcome;
        }
        String left = "You can delete the temporary folder " + staging + ".";
        return switch (outcome) {
            case CopyOutcome.Failed failed -> new CopyOutcome.Failed(failed.message() + " " + left);
            case CopyOutcome.Cancelled ignored -> new CopyOutcome.Failed("The copy was cancelled, but its temporary "
                    + "folder could not be deleted completely. " + left);
            default -> new CopyOutcome.Failed(left);
        };
    }

    /**
     * Deletes the staging folder, and only after checking that it IS one: it sits in {@code parent} and carries the
     * staging prefix. Links and junctions inside it are deleted themselves, never followed. Returns whether it is gone.
     */
    private static boolean deleteStaging(Path parent, String name, Path staging) {
        Path fileName = staging.getFileName();
        if (!parent.equals(staging.getParent()) || fileName == null
                || !fileName.toString().startsWith("." + name + ProjectCreator.STAGING_INFIX)) {
            return false;
        }
        List<IOException> failures = new ArrayList<>();
        FileTrees.deleteTree(staging, failures);
        return Files.notExists(staging, LinkOption.NOFOLLOW_LINKS);
    }

    /**
     * Why the copy's {@code .vfproj} would still let its editor reach the original folder, or empty when it would not.
     * {@link ProjectFileEdit#forCopy} re-points what it can tell by the path's text; this asks the file system, so a
     * hand-written spelling that names the same folder is caught too: a trailing dot ({@code Game.}), an admin share
     * ({@code \\localhost\C$\…}), a short name, a link or a {@code subst} drive. A member whose path the launcher cannot
     * read (a trailing space, a drive-relative path) is refused, as where it leads cannot be checked.
     *
     * @param copyProjectFile the copy's {@code .vfproj} bytes, as they will be written
     * @param copyFolder      where the copy will be (it does not exist yet)
     * @param originalFolder  the original project's folder
     */
    static Optional<String> leadsIntoOriginal(byte[] copyProjectFile, Path copyFolder, Path originalFolder)
            throws InvalidProjectFileException {
        for (ProjectFileEdit.MemberPath member : ProjectFileEdit.resolvedPaths(copyProjectFile, copyFolder)) {
            if (member.resolved() == null) {
                return Optional.of("The project file's " + member.member() + " (\"" + member.text() + "\") is not a"
                        + " path the launcher can check, so it cannot make sure the copy would not change the original"
                        + " project. Correct that path in the project file first.");
            }
            if (reaches(member.resolved(), originalFolder)) {
                return Optional.of("The project file's " + member.member() + " (\"" + member.text() + "\") would"
                        + " still lead the copy into the original project (" + member.resolved() + "), so the copy's"
                        + " editor would change the original. Correct that path in the project file first.");
            }
        }
        return Optional.empty();
    }

    /**
     * Whether {@code path}, or the deepest of its folders that exists, is {@code folder} or lies in it, as the file
     * system sees it ({@link Files#isSameFile}: the same volume and file id, whatever the spelling).
     */
    static boolean reaches(Path path, Path folder) {
        Path existing = path;
        while (existing != null && !Files.exists(existing)) {
            existing = existing.getParent();
        }
        for (Path ancestor = existing; ancestor != null; ancestor = ancestor.getParent()) {
            try {
                if (Files.isSameFile(ancestor, folder)) {
                    return true;
                }
            } catch (IOException | SecurityException e) {
                // A folder that cannot be opened cannot be shown to be the project's; keep looking above it.
            }
        }
        return false;
    }

    private static String describe(Throwable error) {
        return FileErrors.describe(error);
    }
}
