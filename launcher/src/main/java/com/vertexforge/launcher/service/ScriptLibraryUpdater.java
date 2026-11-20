package com.vertexforge.launcher.service;

import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.scriptlib.FileDecision;
import com.vertexforge.launcher.scriptlib.ImportCheck;
import com.vertexforge.launcher.scriptlib.LegacyLibraryHashes;
import com.vertexforge.launcher.scriptlib.LibraryBaseline;
import com.vertexforge.launcher.scriptlib.LibraryPlan;
import com.vertexforge.launcher.scriptlib.LibrarySnapshot;
import com.vertexforge.launcher.scriptlib.RecoveryPlanner;
import com.vertexforge.launcher.scriptlib.UpdateJournal;
import com.vertexforge.launcher.template.LibraryManifest;
import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.FileTrees;
import com.vertexforge.launcher.util.TransientIo;
import com.vertexforge.launcher.util.VersionNumber;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.FileTime;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Clock;
import java.time.Duration;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import java.util.function.Supplier;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

/**
 * Updates the script library of an existing project to an engine's (VK-1691), keeping the user's own work.
 * <p>
 * The project's library is {@code <workingDirectory>/scripts/lib}. Each file is compared three ways, with the project's
 * {@code .vflib.json} as the baseline, or every version the launcher knows ({@link LegacyLibraryHashes}) for a project
 * made before libraries were tracked; {@link com.vertexforge.launcher.scriptlib.LibraryClassifier} has the rules and
 * {@link LibraryPlan} what the new library holds. Nothing outside the library changes, and {@code scripts/game} is only
 * read (for the import warnings).
 * <p>
 * An update, under the editor's lock (so no editor can open the project meanwhile):
 * <ol>
 *   <li>plans again, and stops when the plan is not the one the user confirmed, or when it would change nothing;</li>
 *   <li>backs the library up into {@code <project>/.vertexforge/backups/scripts-lib-<version>-<UTC time>Z.zip}
 *       (complete and verified before it has its name);</li>
 *   <li>writes the {@linkplain UpdateJournal journal}, then builds the new library in {@code lib.new-<id>}, its
 *       {@code .vflib.json} last, and reads it back;</li>
 *   <li>renames {@code lib} to {@code lib.old-<id>} and {@code lib.new-<id>} to {@code lib} (from here on it cannot
 *       be cancelled), deletes {@code lib.old-<id>}, then the journal.</li>
 * </ol>
 * Any failure on the way undoes what the update did, through the same {@link RecoveryPlanner} that finishes an update
 * the launcher did not live to finish. The backup zip is never deleted.
 */
public final class ScriptLibraryUpdater {

    public static final String SCRIPTS_FOLDER = "scripts";
    public static final String LIBRARY_FOLDER = "lib";
    public static final String GAME_FOLDER = "game";
    public static final String SCRIPTS_PROJECT = "scripts.mtproj";
    /** Where the backups go, inside the editor's state folder. */
    public static final String BACKUPS_FOLDER = "backups";
    public static final String BACKUP_PREFIX = "scripts-lib-";
    /** The version part of a backup's name when the library had no {@code .vflib.json}. */
    public static final String LEGACY_VERSION = "legacy";

    /** A library file larger than this is refused: a script library holds small text files. */
    static final long MAX_FILE_BYTES = 64L * 1024 * 1024;
    /** The largest {@code .vflib.json}, journal or {@code .mtproj} read. */
    static final int MAX_SMALL_FILE_BYTES = 4 * 1024 * 1024;
    static final int MAX_RECOVERY_STEPS = 8;

    private static final DateTimeFormatter TIMESTAMP =
            DateTimeFormatter.ofPattern("uuuuMMdd-HHmmss").withZone(ZoneOffset.UTC);
    private static final Pattern SEARCH_PATH = Pattern.compile("<SearchPath>\\s*([^<]*?)\\s*</SearchPath>");

    /**
     * What to update.
     *
     * @param projectFile      the project's {@code .vfproj}
     * @param workingDirectory its {@code workingDirectory} member as written, or {@code null}
     * @param engineLibrary    the engine's script library folder ({@code engine.json}'s {@code scriptLibDir})
     * @param engineVersion    the engine's {@code MAJOR.MINOR.PATCH}
     */
    public record Request(Path projectFile, String workingDirectory, Path engineLibrary, String engineVersion) {
        public Request {
            Objects.requireNonNull(projectFile, "projectFile");
            Objects.requireNonNull(engineLibrary, "engineLibrary");
            Objects.requireNonNull(engineVersion, "engineVersion");
        }
    }

    /**
     * What an update would do, for the user to confirm.
     *
     * @param request       what it was made for
     * @param library       the project's library folder
     * @param hadLibrary    whether that folder exists
     * @param baseline      what the files were compared against
     * @param plan          the plan, with the engine's copy for every file in conflict
     * @param brokenImports the user's imports the update would break
     * @param fingerprint   what the update checks it still plans the same
     * @param backupFolder  where the backup zip will go
     * @param notes         what else the user should know
     */
    public record Preview(Request request, Path library, boolean hadLibrary, LibraryBaseline baseline,
                          LibraryPlan plan, List<ImportCheck.BrokenImport> brokenImports, String fingerprint,
                          Path backupFolder, List<String> notes) {
        public Preview {
            Objects.requireNonNull(request, "request");
            Objects.requireNonNull(library, "library");
            Objects.requireNonNull(baseline, "baseline");
            Objects.requireNonNull(plan, "plan");
            brokenImports = List.copyOf(brokenImports);
            Objects.requireNonNull(fingerprint, "fingerprint");
            Objects.requireNonNull(backupFolder, "backupFolder");
            notes = List.copyOf(notes);
        }

        /** How many files both the user and the engine changed. */
        public int conflicts() {
            return plan.decisions(FileDecision.Verdict.CONFLICT).size();
        }
    }

    /** How a preview ended. */
    public sealed interface PreviewOutcome {
        record Ready(Preview preview) implements PreviewOutcome {
            public Ready {
                Objects.requireNonNull(preview, "preview");
            }
        }

        /** An earlier update was interrupted: it must be finished (or undone) first, by {@link #recover}. */
        record Interrupted(String detail) implements PreviewOutcome {
            public Interrupted {
                Objects.requireNonNull(detail, "detail");
            }
        }

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

    /** How an update ended. */
    public sealed interface UpdateOutcome {
        /**
         * The new library is in place.
         *
         * @param backup the backup zip, or {@code null} when the project had no library to back up
         * @param plan   what was done
         * @param notes  what the user should know
         */
        record Done(Path backup, LibraryPlan plan, List<String> notes) implements UpdateOutcome {
            public Done {
                Objects.requireNonNull(plan, "plan");
                notes = List.copyOf(notes);
            }
        }

        /** The library already is what an update would make; nothing was written. */
        record UpToDate() implements UpdateOutcome {
        }

        /** The project or the engine changed since the preview; nothing was changed. Here is a new preview. */
        record Changed(Preview preview) implements UpdateOutcome {
            public Changed {
                Objects.requireNonNull(preview, "preview");
            }
        }

        /** Nothing was written: {@code reason} says why the update cannot run. */
        record Refused(String reason) implements UpdateOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        /** The user cancelled; the library is as it was (a backup already made stays). */
        record Cancelled() implements UpdateOutcome {
        }

        /** Something went wrong; the library is as it was. */
        record Failed(String message) implements UpdateOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }

        /** The update stopped half-way and could not be undone; {@link #recover} finishes it. */
        record Interrupted(String message) implements UpdateOutcome {
            public Interrupted {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    /** How finishing an interrupted update ended. */
    public sealed interface RecoveryOutcome {
        /**
         * The library is in order again: {@code summary} says how.
         *
         * @param completedUpdate whether the library is now the one the update built (the update was finished);
         *                        {@code false} when it is the old one again, or was only cleaned up
         */
        record Recovered(String summary, boolean completedUpdate) implements RecoveryOutcome {
            public Recovered {
                Objects.requireNonNull(summary, "summary");
            }
        }

        /** No update was interrupted. */
        record NothingToDo() implements RecoveryOutcome {
        }

        /** Nothing was touched: the user must look ({@code reason}). */
        record NeedsAttention(String reason) implements RecoveryOutcome {
            public NeedsAttention {
                Objects.requireNonNull(reason, "reason");
            }
        }

        record Refused(String reason) implements RecoveryOutcome {
            public Refused {
                Objects.requireNonNull(reason, "reason");
            }
        }

        record Failed(String message) implements RecoveryOutcome {
            public Failed {
                Objects.requireNonNull(message, "message");
            }
        }
    }

    /** A point of an update where tests inject a failure. */
    @FunctionalInterface
    interface Checkpoint {
        Checkpoint NONE = point -> {
        };

        void reached(String point) throws IOException;
    }

    private final Clock clock;
    private final SpaceProbe space;
    private final EditorLockProbe locks;
    private final DirectoryMover mover;
    private final Supplier<UUID> ids;
    private final Supplier<LegacyLibraryHashes> legacy;
    private final Duration retryFirstDelay;
    private final Duration retryMaxDelay;
    private final Checkpoint checkpoint;

    public ScriptLibraryUpdater(Clock clock, SpaceProbe space, EditorLockProbe locks) {
        this(clock, space, locks, DirectoryMover.SYSTEM, UUID::randomUUID, LegacyLibraryHashes::bundled,
                ProjectCreator.COMMIT_FIRST_DELAY, Checkpoint.NONE);
    }

    /** For tests: every seam. Renames are retried after {@code retryDelay}, doubling up to tenfold. */
    ScriptLibraryUpdater(Clock clock, SpaceProbe space, EditorLockProbe locks, DirectoryMover mover,
                         Supplier<UUID> ids, Supplier<LegacyLibraryHashes> legacy, Duration retryDelay,
                         Checkpoint checkpoint) {
        this.clock = Objects.requireNonNull(clock, "clock");
        this.space = Objects.requireNonNull(space, "space");
        this.locks = Objects.requireNonNull(locks, "locks");
        this.mover = Objects.requireNonNull(mover, "mover");
        this.ids = Objects.requireNonNull(ids, "ids");
        this.legacy = Objects.requireNonNull(legacy, "legacy");
        this.retryFirstDelay = Objects.requireNonNull(retryDelay, "retryDelay");
        this.retryMaxDelay = retryDelay.multipliedBy(10);
        this.checkpoint = Objects.requireNonNull(checkpoint, "checkpoint");
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Where things are

    /** The scripts folder of a project whose working directory is resolved: {@code <wd>/scripts}. */
    public static Path scriptsFolder(ProjectContent content) {
        return content.workingDirectory().resolve(SCRIPTS_FOLDER);
    }

    /** The journal of an interrupted update, beside the library. */
    public static Path journalFile(Path scriptsFolder) {
        return scriptsFolder.resolve(UpdateJournal.FILE_NAME);
    }

    /** The backups folder of a project folder. */
    public static Path backupFolder(Path projectFolder) {
        return projectFolder.resolve(ProjectContent.STATE_FOLDER).resolve(BACKUPS_FOLDER);
    }

    /** Why the update cannot cover this project; empty when it can. */
    public static Optional<String> unavailableReason(ProjectContent content) {
        return switch (content.scope()) {
            case INSIDE -> Optional.empty();
            case OUTSIDE -> Optional.of("Its working directory (" + content.workingDirectory() + ") lies outside the "
                    + "project folder, so the launcher cannot update its script library safely.");
            case NO_PARENT -> Optional.of("The project folder is a drive root; move the project into a folder to "
                    + "update its script library.");
            case INVALID -> Optional.of("Its working directory is missing or is not a valid path, so the launcher "
                    + "cannot tell where its script library is.");
        };
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Preview

    /**
     * Works out what an update would do. Only reads; refuses while an editor has the project open. Blocks on the file
     * system, so it only runs on a background thread. Never throws.
     */
    public PreviewOutcome preview(Request request, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        try {
            ProjectContent content = ProjectContent.of(request.projectFile(), request.workingDirectory());
            Optional<String> unavailable = unavailableReason(content);
            if (unavailable.isPresent()) {
                return new PreviewOutcome.Refused(unavailable.get());
            }
            Optional<String> locked = lockRefusal(locks.probe(content.folder()));
            if (locked.isPresent()) {
                return new PreviewOutcome.Refused(locked.get());
            }
            if (Files.exists(journalFile(scriptsFolder(content)), LinkOption.NOFOLLOW_LINKS)) {
                return new PreviewOutcome.Interrupted("An earlier update of this project's script library was "
                        + "interrupted. It must be finished first.");
            }
            Preview preview = previewOf(scan(request, content, monitor));
            Optional<String> clash = clashRefusal(preview.plan(), preview.library());
            if (clash.isPresent()) {
                return new PreviewOutcome.Refused(clash.get());
            }
            return new PreviewOutcome.Ready(preview);
        } catch (ScanStop stop) {
            return stop.outcome;
        } catch (JobCancelledException e) {
            return new PreviewOutcome.Cancelled();
        } catch (IOException e) {
            return new PreviewOutcome.Failed("Could not read the script libraries: " + describe(e));
        } catch (RuntimeException e) {
            return new PreviewOutcome.Failed("The preview stopped unexpectedly: " + e);
        }
    }

    /** Everything a plan is made from, as read from the disk. */
    private record Scan(ProjectContent content, Path scripts, Path library, String libraryName, boolean hadLibrary,
                        FolderTree projectTree, FolderTree engineTree, Request request, LibraryPlan.Inputs inputs,
                        String fingerprint, List<String> notes) {
    }

    /** A scan that stops with an outcome for the user. */
    private static final class ScanStop extends Exception {
        private final PreviewOutcome outcome;

        ScanStop(PreviewOutcome outcome) {
            super(null, null, false, false);
            this.outcome = outcome;
        }

        static ScanStop refused(String reason) {
            return new ScanStop(new PreviewOutcome.Refused(reason));
        }
    }

    private Scan scan(Request request, ProjectContent content, JobMonitor monitor) throws IOException, ScanStop {
        List<String> notes = new ArrayList<>();
        if (VersionNumber.parse(request.engineVersion()).isEmpty()) {
            throw ScanStop.refused("The engine's version (" + request.engineVersion() + ") is not a "
                    + "MAJOR.MINOR.PATCH version.");
        }
        Path scripts = scriptsFolder(content);
        if (!Files.isDirectory(scripts)) {
            throw ScanStop.refused("The project has no scripts folder (" + scripts + ").");
        }
        String libraryName = libraryName(scripts);
        Path library = scripts.resolve(libraryName);
        RecoveryPlanner.Kind libraryKind = kindOf(library);
        if (libraryKind == RecoveryPlanner.Kind.OTHER) {
            throw ScanStop.refused(library + " is not a plain folder (a link or a file), so the launcher cannot "
                    + "update it.");
        }
        boolean hadLibrary = libraryKind == RecoveryPlanner.Kind.FOLDER;
        for (String stray : strayUpdateFolders(scripts, libraryName)) {
            notes.add(scripts.resolve(stray) + " looks like the leftover of an interrupted update; it is not "
                    + "touched. Delete it when you no longer need it.");
        }

        // The engine's library.
        if (kindOf(request.engineLibrary()) != RecoveryPlanner.Kind.FOLDER) {
            throw ScanStop.refused("The engine's script library " + request.engineLibrary() + " is missing.");
        }
        FolderTree engineTree = FolderTree.scan(request.engineLibrary(), relative -> false, monitor);
        notes.addAll(engineTree.notes().stream().map(note -> "In the engine's library: " + note).toList());
        LibrarySnapshot engine = snapshot(engineTree, "the engine's script library");
        if (engine.isEmpty()) {
            throw ScanStop.refused("The engine's script library " + request.engineLibrary() + " has no files.");
        }

        // The project's library and its baseline.
        FolderTree projectTree = null;
        LibrarySnapshot project = LibrarySnapshot.EMPTY;
        LibraryBaseline baseline;
        if (hadLibrary) {
            projectTree = FolderTree.scan(library, relative -> false, monitor);
            if (!projectTree.notes().isEmpty()) {
                throw ScanStop.refused("The script library holds something an update would lose: "
                        + String.join(" ", projectTree.notes()) + " Move it out of " + library + ", then update.");
            }
            requireDistinctNames(projectTree, library);
            project = snapshot(projectTree, "the project's script library");
            baseline = baseline(projectTree, request);
        } else {
            baseline = new LibraryBaseline.Legacy(legacy.get(), "The project has no script library; the engine's "
                    + "is added.");
        }

        LibraryPlan.Inputs inputs = new LibraryPlan.Inputs(project,
                projectTree == null ? List.of() : projectTree.files().stream().map(FolderTree.FileEntry::relative)
                        .toList(),
                projectTree == null ? List.of() : projectTree.folders(), engine,
                engineTree.folders(), baseline, request.engineVersion());
        return new Scan(content, scripts, library, libraryName, hadLibrary, projectTree, engineTree, request, inputs,
                LibraryPlan.fingerprint(inputs), notes);
    }

    private LibraryBaseline baseline(FolderTree projectTree, Request request) throws IOException, ScanStop {
        Optional<FolderTree.FileEntry> manifestEntry = projectTree.files().stream()
                .filter(file -> file.relative().equalsIgnoreCase(LibraryManifest.FILE_NAME))
                .findFirst();
        if (manifestEntry.isEmpty()) {
            return new LibraryBaseline.Legacy(legacy.get(), "The project was made before script libraries were "
                    + "tracked, so each file is compared with every library version the launcher knows.");
        }
        byte[] bytes = readCapped(manifestEntry.get().path(), MAX_SMALL_FILE_BYTES);
        LibraryManifest.Contents contents;
        try {
            contents = LibraryManifest.parse(bytes);
        } catch (LibraryManifest.InvalidLibraryManifestException e) {
            if (e.newerFormat()) {
                throw ScanStop.refused("The library's " + LibraryManifest.FILE_NAME + " was written by a newer "
                        + "launcher (" + e.getMessage() + "). Update the library with that launcher.");
            }
            return new LibraryBaseline.Legacy(legacy.get(), "The library's " + LibraryManifest.FILE_NAME
                    + " cannot be read (" + e.getMessage() + "), so each file is compared with every library "
                    + "version the launcher knows.");
        }
        LibraryBaseline.Recorded recorded;
        try {
            recorded = LibraryBaseline.Recorded.of(contents);
        } catch (LibrarySnapshot.DuplicatePathException e) {
            // parse already refuses two paths with one key; kept for safety.
            return new LibraryBaseline.Legacy(legacy.get(), "The library's " + LibraryManifest.FILE_NAME
                    + " cannot be read (" + e.getMessage() + ").");
        }
        VersionNumber recordedVersion = VersionNumber.parse(recorded.engineVersion()).orElseThrow();
        VersionNumber engineVersion = VersionNumber.parse(request.engineVersion()).orElseThrow();
        if (recordedVersion.compareTo(engineVersion) > 0) {
            throw ScanStop.refused("The library comes from engine " + recorded.engineVersion() + ", which is newer "
                    + "than this engine (" + request.engineVersion() + "). Update it with that engine.");
        }
        return recorded;
    }

    private Preview previewOf(Scan scan) throws IOException {
        LibraryPlan plan = LibraryPlan.of(scan.inputs(), false);
        List<String> notes = new ArrayList<>(scan.notes());
        List<ImportCheck.BrokenImport> broken = brokenImports(scan, plan, notes);
        return new Preview(scan.request(), scan.library(), scan.hadLibrary(), scan.inputs().baseline(), plan, broken,
                scan.fingerprint(), backupFolder(scan.content().folder()), notes);
    }

    /** The user's imports the plan breaks: in {@code scripts/game}, and in the library files the user keeps. */
    private static List<ImportCheck.BrokenImport> brokenImports(Scan scan, LibraryPlan plan, List<String> notes)
            throws IOException {
        Path mtproj = scan.scripts().resolve(SCRIPTS_PROJECT);
        if (!Files.isRegularFile(mtproj)) {
            notes.add("The scripts project " + mtproj + " is missing, so imports were not checked.");
            return List.of();
        }
        List<String> searchPaths = new ArrayList<>();
        Matcher matcher = SEARCH_PATH.matcher(new String(readCapped(mtproj, MAX_SMALL_FILE_BYTES),
                StandardCharsets.UTF_8));
        while (matcher.find()) {
            if (!matcher.group(1).isEmpty()) {
                searchPaths.add(matcher.group(1));
            }
        }

        List<ImportCheck.Script> scripts = new ArrayList<>();
        Path game = scan.scripts().resolve(GAME_FOLDER);
        if (Files.isDirectory(game)) {
            FolderTree gameTree = FolderTree.scan(game, relative -> false, JobMonitor.NONE);
            for (FolderTree.FileEntry file : gameTree.files()) {
                if (isScript(file.relative()) && file.size() <= MAX_FILE_BYTES) {
                    scripts.add(new ImportCheck.Script(file.path(), GAME_FOLDER + "/" + file.relative(),
                            new String(Files.readAllBytes(file.path()), StandardCharsets.UTF_8)));
                }
            }
        }
        for (FileDecision decision : plan.decisions()) {
            boolean keptOfTheUser = switch (decision.verdict()) {
                case KEEP_EDIT, KEEP_REMOVED_EDITED, USER_FILE -> true;
                default -> false;
            };
            if (keptOfTheUser && isScript(decision.projectPath())) {
                Path file = scan.library().resolve(decision.projectPath());
                scripts.add(new ImportCheck.Script(file, scan.libraryName() + "/" + decision.projectPath(),
                        new String(readCapped(file, MAX_FILE_BYTES), StandardCharsets.UTF_8)));
            }
        }

        Set<String> before = new HashSet<>();
        if (scan.projectTree() != null) {
            scan.projectTree().files().forEach(file -> before.add(LibraryManifest.pathKey(file.relative())));
        }
        List<String> after = new ArrayList<>(plan.files().stream().map(LibraryPlan.TargetFile::path).toList());
        after.add(LibraryManifest.FILE_NAME);
        ImportCheck.Libraries libraries = new ImportCheck.Libraries(scan.scripts().toAbsolutePath().normalize(),
                scan.library().toAbsolutePath().normalize(), searchPaths, before, after,
                path -> Files.exists(path));
        return ImportCheck.check(scripts, libraries);
    }

    private static boolean isScript(String relative) {
        return relative.toLowerCase(Locale.ROOT).endsWith(".mt");
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Update

    /**
     * Updates the library as previewed. Holds the editor's lock throughout; stops with {@link UpdateOutcome.Changed}
     * when the plan is no longer the one with {@code fingerprint}. Blocks on the file system, so it only runs on a
     * background thread. A cancel is possible until the folders are swapped. Never throws, except an {@link Error},
     * after it has undone what it did.
     *
     * @param keepMine whether files in conflict keep the user's copy
     */
    public UpdateOutcome update(Request request, String fingerprint, boolean keepMine, JobMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(fingerprint, "fingerprint");
        Objects.requireNonNull(monitor, "monitor");
        Path journal = null;
        try {
            ProjectContent content = ProjectContent.of(request.projectFile(), request.workingDirectory());
            Optional<String> unavailable = unavailableReason(content);
            if (unavailable.isPresent()) {
                return new UpdateOutcome.Refused(unavailable.get());
            }
            journal = journalFile(scriptsFolder(content));
            EditorLockProbe.HoldResult held = locks.hold(content.folder());
            if (held instanceof EditorLockProbe.HoldResult.NotHeld notHeld) {
                return new UpdateOutcome.Refused(lockRefusal(notHeld.state()).orElse("The project's lock could not "
                        + "be taken."));
            }
            try (EditorLockProbe.Hold ignored = ((EditorLockProbe.HoldResult.Holding) held).hold()) {
                return updateHeld(request, content, fingerprint, keepMine, monitor);
            }
        } catch (RuntimeException e) {
            // With its journal still there, the update may have stopped half-way: say so, rather than "as it was".
            if (journal != null && Files.exists(journal, LinkOption.NOFOLLOW_LINKS)) {
                return new UpdateOutcome.Interrupted("The update stopped unexpectedly (" + e + "). The launcher "
                        + "finishes it the next time you update or open the project.");
            }
            return new UpdateOutcome.Failed("The update stopped unexpectedly: " + e);
        }
    }

    private UpdateOutcome updateHeld(Request request, ProjectContent content, String fingerprint, boolean keepMine,
                                     JobMonitor monitor) {
        Path scripts = scriptsFolder(content);
        List<String> recoveryNotes = new ArrayList<>();
        if (Files.exists(journalFile(scripts), LinkOption.NOFOLLOW_LINKS)) {
            RecoveryOutcome recovered = recoverHeld(content, scripts);
            switch (recovered) {
                case RecoveryOutcome.Recovered done -> recoveryNotes.add(done.summary());
                case RecoveryOutcome.NothingToDo ignored -> {
                }
                case RecoveryOutcome.NeedsAttention attention -> {
                    return new UpdateOutcome.Interrupted(attention.reason());
                }
                case RecoveryOutcome.Refused refused -> {
                    return new UpdateOutcome.Refused(refused.reason());
                }
                case RecoveryOutcome.Failed failed -> {
                    return new UpdateOutcome.Interrupted(failed.message());
                }
            }
        }

        Scan scan;
        try {
            scan = scan(request, content, monitor);
            if (!scan.fingerprint().equals(fingerprint)) {
                return changedOrRefused(previewOf(scan));
            }
        } catch (ScanStop stop) {
            return switch (stop.outcome) {
                case PreviewOutcome.Refused refused -> new UpdateOutcome.Refused(refused.reason());
                case PreviewOutcome.Failed failed -> new UpdateOutcome.Failed(failed.message());
                case PreviewOutcome.Cancelled ignored -> new UpdateOutcome.Cancelled();
                case PreviewOutcome.Interrupted interrupted -> new UpdateOutcome.Interrupted(interrupted.detail());
                case PreviewOutcome.Ready ready -> changedOrRefused(ready.preview());
            };
        } catch (JobCancelledException e) {
            return new UpdateOutcome.Cancelled();
        } catch (IOException e) {
            return new UpdateOutcome.Failed("Could not read the script libraries: " + describe(e));
        }

        LibraryPlan plan = LibraryPlan.of(scan.inputs(), keepMine);
        Optional<String> clash = clashRefusal(plan, scan.library());
        if (clash.isPresent()) {
            return new UpdateOutcome.Refused(clash.get());
        }
        if (plan.upToDate()) {
            return new UpdateOutcome.UpToDate();
        }
        List<String> notes = new ArrayList<>(recoveryNotes);
        notes.addAll(scan.notes());
        return apply(scan, plan, notes, monitor);
    }

    private UpdateOutcome apply(Scan scan, LibraryPlan plan, List<String> notes, JobMonitor monitor) {
        Path folder = scan.content().folder();
        String suffix = ids.get().toString().replace("-", "").substring(0, 8).toLowerCase(Locale.ROOT);
        String stagingName = UpdateJournal.stagingName(scan.libraryName(), suffix);
        String previousName = UpdateJournal.previousName(scan.libraryName(), suffix);
        Path staging = scan.scripts().resolve(stagingName);
        Path previous = scan.scripts().resolve(previousName);
        byte[] manifest = plan.manifest();

        // Refusals: nothing has been written yet.
        String deepest = plan.files().stream().map(LibraryPlan.TargetFile::path)
                .max((a, b) -> Integer.compare(a.length(), b.length())).orElse(LibraryManifest.FILE_NAME);
        String longest = staging.resolve(deepest).toString();
        if (longest.length() > ProjectNameRules.MAX_PATH_CHARS) {
            return new UpdateOutcome.Refused("The new library's longest path would be " + longest.length()
                    + " characters long, more than the " + ProjectNameRules.MAX_PATH_CHARS + " Windows allows. Move "
                    + "the project to a folder with a shorter path.");
        }
        Map<String, Long> sizes = sourceSizes(scan);
        long stagingBytes = manifest.length;
        for (LibraryPlan.TargetFile file : plan.files()) {
            stagingBytes += sizes.getOrDefault(sourceKey(file), 0L);
        }
        long zipBytes = scan.hadLibrary() ? FolderZipWriter.requiredSpace(scan.projectTree(), scan.libraryName()) : 0;
        long required = stagingBytes + zipBytes + FolderZipWriter.MIN_MARGIN_BYTES;
        try {
            long usable = space.usableBytes(scan.scripts());
            if (usable < required) {
                return new UpdateOutcome.Refused("Not enough free space in " + scan.scripts() + ": the update needs "
                        + "up to " + JobMonitor.formatBytes(required) + ", but only " + JobMonitor.formatBytes(usable)
                        + " is free.");
            }
        } catch (IOException e) {
            notes.add("Could not check the free space in " + scan.scripts() + " (" + describe(e)
                    + "); the update went ahead.");
        }

        // 1: the backup.
        Path zip = null;
        if (scan.hadLibrary()) {
            Path backups = backupFolder(folder);
            String stem = BACKUP_PREFIX + versionPart(scan.inputs().baseline()) + "-"
                    + TIMESTAMP.format(clock.instant()) + "Z";
            String longestZip = backups.resolve(VerifiedZipCommit.candidate(stem, VerifiedZipCommit.MAX_NAME_ATTEMPTS)
                    + VerifiedZipCommit.PARTIAL_EXTENSION).toString();
            if (longestZip.length() > ProjectNameRules.MAX_PATH_CHARS) {
                return new UpdateOutcome.Refused("The backup's path would be up to " + longestZip.length()
                        + " characters long, more than the " + ProjectNameRules.MAX_PATH_CHARS + " Windows allows.");
            }
            try {
                Files.createDirectories(backups);
            } catch (IOException e) {
                return new UpdateOutcome.Failed("Could not make the backup folder " + backups + ": " + describe(e));
            }
            VerifiedZipCommit.Outcome backup = VerifiedZipCommit.write(scan.projectTree(), scan.libraryName(),
                    backups, stem, monitor, mover, ProjectCreator.COMMIT_ATTEMPTS, retryFirstDelay, retryMaxDelay);
            switch (backup) {
                case VerifiedZipCommit.Outcome.Committed committed -> zip = committed.zip();
                case VerifiedZipCommit.Outcome.Cancelled ignored -> {
                    return new UpdateOutcome.Cancelled();
                }
                case VerifiedZipCommit.Outcome.Failed failed -> {
                    return new UpdateOutcome.Failed(failed.message());
                }
            }
        }
        String backupNote = zip == null ? "" : " The backup " + zip + " stays.";

        // 2: the journal. From its creation on, every failure undoes the update through the recovery.
        UpdateJournal journal = new UpdateJournal(UpdateJournal.FORMAT_VERSION, suffix, scan.libraryName(),
                stagingName, previousName, scan.hadLibrary(), sha256(manifest),
                zip == null ? null : folder.relativize(zip).toString().replace('\\', '/'),
                scan.request().engineVersion(), clock.instant().toString());
        try {
            checkpoint.reached("journal");
            writeNew(journalFile(scan.scripts()), journal.toJson());
        } catch (FileAlreadyExistsException e) {
            // Another update's journal appeared since the check (another launcher): it is not ours to undo.
            return new UpdateOutcome.Refused("Another update of this library started meanwhile ("
                    + journalFile(scan.scripts()) + " appeared). Try again once it is over." + backupNote);
        } catch (IOException e) {
            return afterFailure(scan, plan, zip, notes, "Could not start the update: " + describe(e) + ".",
                    backupNote);
        }

        // 3: the new library, read back; then the old one must still be what was planned.
        try {
            build(staging, scan, plan, manifest, sizes, monitor);
            checkpoint.reached("built");
            verify(staging, plan, manifest, sizes, monitor);
            checkpoint.reached("verified");
            if (!unchanged(scan)) {
                Undone undone = undo(scan);
                if (undone.left() != null) {
                    return new UpdateOutcome.Interrupted("The library changed while the update ran, and "
                            + undone.left());
                }
                try {
                    return changedOrRefused(previewOf(scan(scan.request(), scan.content(), JobMonitor.NONE)));
                } catch (ScanStop | IOException e) {
                    return new UpdateOutcome.Failed("The library changed while the update ran; nothing was changed."
                            + " Try again." + backupNote);
                }
            }
        } catch (JobCancelledException e) {
            Undone undone = undo(scan);
            if (undone.completed()) {
                return new UpdateOutcome.Done(zip, plan, notes);
            }
            return undone.left() == null ? new UpdateOutcome.Cancelled()
                    : new UpdateOutcome.Interrupted("The update was cancelled, but " + undone.left());
        } catch (IOException | RuntimeException e) {
            return afterFailure(scan, plan, zip, notes, "Could not build the new library: " + describe(e) + ".",
                    backupNote);
        } catch (Error e) {
            undo(scan);
            throw e;
        }

        // 4: the swap. A cancel is no longer possible.
        try {
            monitor.progress(JobMonitor.Phase.COMMITTING, 0, 2);
        } catch (IOException e) {
            // Progress only; the swap goes on.
        }
        if (scan.hadLibrary()) {
            try {
                checkpoint.reached("aside");
                move(scan.library(), previous, "moving the old library aside");
            } catch (IOException e) {
                // A rename that happened although it was reported as failed (a share whose answer got lost) goes on.
                if (!movedAnyway(scan.library(), previous)) {
                    return afterFailure(scan, plan, zip, notes, "Could not move the old library aside: "
                            + describe(e) + ". Close any program that uses a file in " + scan.library()
                            + " (a terminal, an editor, a file manager), then try again.", backupNote);
                }
            }
        }
        try {
            monitor.progress(JobMonitor.Phase.COMMITTING, 1, 2);
        } catch (IOException e) {
            // Progress only.
        }
        try {
            checkpoint.reached("swap");
            move(staging, scan.library(), "putting the new library in place");
        } catch (IOException e) {
            if (movedAnyway(staging, scan.library())) {
                // The new library is in place after all; the update goes on to its clean-up.
                notes.add("Putting the new library in place was reported as failed (" + describe(e) + "), but it is "
                        + "in place.");
            } else {
                return failedSwap(scan, plan, zip, notes, previous, e, backupNote);
            }
        }
        try {
            monitor.progress(JobMonitor.Phase.COMMITTING, 2, 2);
        } catch (IOException e) {
            // Progress only.
        }
        return cleanUp(scan, plan, zip, notes, previous);
    }

    /**
     * Whether a rename of {@code source} to {@code target} that reported a failure happened anyway: the source is
     * gone and the target is a folder.
     */
    private static boolean movedAnyway(Path source, Path target) {
        try {
            return kindOf(source) == RecoveryPlanner.Kind.MISSING && kindOf(target) == RecoveryPlanner.Kind.FOLDER;
        } catch (IOException e) {
            return false;
        }
    }

    /** The new library could not be put in place: the old one goes back, then the update is undone. */
    private UpdateOutcome failedSwap(Scan scan, LibraryPlan plan, Path zip, List<String> notes, Path previous,
                                     IOException e, String backupNote) {
        if (scan.hadLibrary()) {
            try {
                move(previous, scan.library(), "putting the old library back");
            } catch (IOException rollback) {
                if (!movedAnyway(previous, scan.library())) {
                    return new UpdateOutcome.Interrupted("The new library could not be put in place (" + describe(e)
                            + "), nor the old one back (" + describe(rollback) + "). The launcher finishes the update "
                            + "the next time you update or open the project." + backupNote);
                }
            }
        }
        return afterFailure(scan, plan, zip, notes, "Could not put the new library in place: " + describe(e) + ".",
                backupNote);
    }

    /** Step 5, once the new library is in place: the update is done whatever happens here. */
    private UpdateOutcome cleanUp(Scan scan, LibraryPlan plan, Path zip, List<String> notes, Path previous) {
        if (scan.hadLibrary()) {
            // The old library goes only while its backup holds every file of it: a file changed during the update
            // would otherwise be lost.
            String mismatch = backupMismatch(previous, zip, scan.libraryName());
            if (mismatch != null) {
                notes.add("The old library was kept in " + previous + ", because " + mismatch + ". Compare it with "
                        + "the backup, then delete it.");
            } else {
                List<IOException> failures = new ArrayList<>();
                FileTrees.deleteTreeClearingReadOnly(previous, failures);
                if (!failures.isEmpty()) {
                    notes.add("The old library is still in " + previous + " (" + describe(failures.getFirst())
                            + "); the launcher deletes it the next time you update or open the project.");
                    return new UpdateOutcome.Done(zip, plan, notes);
                }
            }
        }
        try {
            TransientIo.retry(() -> Files.deleteIfExists(journalFile(scan.scripts())), ProjectCreator.COMMIT_ATTEMPTS,
                    retryFirstDelay, retryMaxDelay, "deleting the update's journal");
        } catch (IOException e) {
            notes.add("Could not delete " + journalFile(scan.scripts()) + " (" + describe(e) + "); the launcher "
                    + "deletes it the next time you update or open the project.");
        }
        return new UpdateOutcome.Done(zip, plan, notes);
    }

    /** The size of every file a plan copies, by {@link #sourceKey}. */
    private static Map<String, Long> sourceSizes(Scan scan) {
        Map<String, Long> sizes = new HashMap<>();
        for (FolderTree.FileEntry file : scan.engineTree().files()) {
            sizes.put(LibraryPlan.Source.ENGINE + "/" + file.relative(), file.size());
        }
        if (scan.projectTree() != null) {
            for (FolderTree.FileEntry file : scan.projectTree().files()) {
                sizes.put(LibraryPlan.Source.PROJECT + "/" + file.relative(), file.size());
            }
        }
        return sizes;
    }

    private static String sourceKey(LibraryPlan.TargetFile file) {
        return file.source() + "/" + file.sourcePath();
    }

    /**
     * Builds the new library in {@code staging}: every folder, every file (read, checked against the plan, written new
     * with its source's modification time), and the {@code .vflib.json} last. Never {@code Files.copy}, which would
     * carry a read-only flag over.
     */
    private void build(Path staging, Scan scan, LibraryPlan plan, byte[] manifest, Map<String, Long> sizes,
                       JobMonitor monitor) throws IOException {
        Files.createDirectory(staging);
        for (String folder : plan.folders()) {
            monitor.checkCancelled();
            Files.createDirectory(staging.resolve(folder));
        }
        long total = manifest.length;
        for (LibraryPlan.TargetFile file : plan.files()) {
            total += sizes.getOrDefault(sourceKey(file), 0L);
        }
        long done = 0;
        for (LibraryPlan.TargetFile file : plan.files()) {
            monitor.checkCancelled();
            Path source = (file.source() == LibraryPlan.Source.ENGINE ? scan.request().engineLibrary()
                    : scan.library()).resolve(file.sourcePath());
            byte[] bytes = readCapped(source, MAX_FILE_BYTES);
            if (file.expectedHash() != null && !LibraryManifest.hash(bytes).equals(file.expectedHash())) {
                throw new IOException(source + " changed while the update ran");
            }
            FileTime modified = Files.getLastModifiedTime(source);
            Path target = staging.resolve(file.path());
            writeNew(target, bytes);
            Files.setLastModifiedTime(target, modified);
            done += bytes.length;
            total = Math.max(total, done);
            monitor.progress(JobMonitor.Phase.COPYING, done, total);
        }
        monitor.checkCancelled();
        writeNew(staging.resolve(LibraryManifest.FILE_NAME), manifest);
    }

    /** Reads the new library back: exactly the planned files and folders, each with the planned content. */
    private static void verify(Path staging, LibraryPlan plan, byte[] manifest, Map<String, Long> sizes,
                               JobMonitor monitor) throws IOException {
        FolderTree tree = FolderTree.scan(staging, relative -> false, monitor);
        Set<String> expected = new TreeSet<>();
        plan.files().forEach(file -> expected.add(file.path()));
        expected.add(LibraryManifest.FILE_NAME);
        Set<String> actual = new TreeSet<>();
        tree.files().forEach(file -> actual.add(file.relative()));
        if (!actual.equals(expected) || !new TreeSet<>(tree.folders()).equals(new TreeSet<>(plan.folders()))
                || !tree.notes().isEmpty()) {
            throw new IOException("the new library in " + staging + " does not hold exactly the planned files");
        }
        long total = tree.totalBytes();
        long done = 0;
        for (LibraryPlan.TargetFile file : plan.files()) {
            monitor.checkCancelled();
            Path written = staging.resolve(file.path());
            byte[] bytes = readCapped(written, MAX_FILE_BYTES);
            boolean right = file.expectedHash() != null
                    ? LibraryManifest.hash(bytes).equals(file.expectedHash())
                    : bytes.length == sizes.getOrDefault(sourceKey(file), -1L);
            if (!right) {
                throw new IOException(written + " reads back with different content");
            }
            done += bytes.length;
            total = Math.max(total, done);
            monitor.progress(JobMonitor.Phase.VERIFYING, done, total);
        }
        if (!java.util.Arrays.equals(readCapped(staging.resolve(LibraryManifest.FILE_NAME), MAX_SMALL_FILE_BYTES),
                manifest)) {
            throw new IOException("the new library's " + LibraryManifest.FILE_NAME + " reads back differently");
        }
    }

    /** Whether the project's library is still exactly as scanned (files, sizes, times and folders). */
    private static boolean unchanged(Scan scan) throws IOException {
        RecoveryPlanner.Kind kind = kindOf(scan.library());
        if (!scan.hadLibrary()) {
            return kind == RecoveryPlanner.Kind.MISSING;
        }
        if (kind != RecoveryPlanner.Kind.FOLDER) {
            return false;
        }
        FolderTree now = FolderTree.scan(scan.library(), relative -> false, JobMonitor.NONE);
        // A link added meanwhile would be neither copied nor backed up: it is a change too.
        return now.notes().isEmpty() && now.files().equals(scan.projectTree().files())
                && now.folders().equals(scan.projectTree().folders());
    }

    /**
     * What undoing a failed update left.
     *
     * @param completed whether the recovery found the swap done and FINISHED the update instead
     * @param left      what is left for the user, or {@code null} when the library is in order
     */
    private record Undone(boolean completed, String left) {
    }

    /** Undoes what the update wrote beside the library, through the recovery. */
    private Undone undo(Scan scan) {
        RecoveryOutcome outcome = recoverHeld(scan.content(), scan.scripts());
        return switch (outcome) {
            case RecoveryOutcome.Recovered recovered -> new Undone(recovered.completedUpdate(), null);
            case RecoveryOutcome.NothingToDo ignored -> new Undone(false, null);
            case RecoveryOutcome.NeedsAttention attention -> new Undone(false, attention.reason());
            case RecoveryOutcome.Refused refused -> new Undone(false, refused.reason());
            case RecoveryOutcome.Failed failed -> new Undone(false, failed.message());
        };
    }

    /**
     * The outcome of an update that failed with {@code message} after its journal was (or may have been) written:
     * undone through the recovery, which may also find that the update had in fact gone through.
     */
    private UpdateOutcome afterFailure(Scan scan, LibraryPlan plan, Path zip, List<String> notes, String message,
                                       String backupNote) {
        Undone undone = undo(scan);
        if (undone.completed()) {
            notes.add(message + " The update had gone through, and the launcher finished it.");
            return new UpdateOutcome.Done(zip, plan, notes);
        }
        return undone.left() == null ? new UpdateOutcome.Failed(message + " Nothing was changed." + backupNote)
                : new UpdateOutcome.Interrupted(message + " Then " + undone.left());
    }

    /**
     * Why {@code folder} is not exactly what the backup {@code zip} holds under {@code rootEntry}/ (every file with
     * the same size and CRC-32, and no other file); {@code null} when it is. Reads every file.
     */
    static String backupMismatch(Path folder, Path zip, String rootEntry) {
        if (zip == null) {
            return "it has no backup";
        }
        FolderTree tree;
        try {
            tree = FolderTree.scan(folder, relative -> false, JobMonitor.NONE);
        } catch (IOException e) {
            return "it could not be read (" + describe(e) + ")";
        }
        if (!tree.notes().isEmpty()) {
            return "it holds a link (" + String.join(" ", tree.notes()) + ")";
        }
        String prefix = rootEntry + "/";
        try (java.util.zip.ZipFile backup = new java.util.zip.ZipFile(zip.toFile(), StandardCharsets.UTF_8)) {
            long filesInBackup = backup.stream()
                    .filter(entry -> !entry.isDirectory() && entry.getName().startsWith(prefix))
                    .count();
            if (filesInBackup != tree.files().size()) {
                return "it holds " + tree.files().size() + " files and its backup " + filesInBackup;
            }
            for (FolderTree.FileEntry file : tree.files()) {
                java.util.zip.ZipEntry entry = backup.getEntry(prefix + file.relative());
                if (entry == null || entry.isDirectory()) {
                    return file.relative() + " is not in its backup";
                }
                java.util.zip.CRC32 crc = new java.util.zip.CRC32();
                byte[] bytes = Files.readAllBytes(file.path());
                crc.update(bytes);
                if (bytes.length != entry.getSize() || crc.getValue() != entry.getCrc()) {
                    return file.relative() + " differs from its backup";
                }
            }
            return null;
        } catch (IOException e) {
            return "its backup " + zip + " could not be read (" + describe(e) + ")";
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Recovery

    /**
     * Finishes or undoes an update that was interrupted, under the editor's lock. Blocks; never throws.
     */
    public RecoveryOutcome recover(Path projectFile, String workingDirectory) {
        Objects.requireNonNull(projectFile, "projectFile");
        try {
            ProjectContent content = ProjectContent.of(projectFile, workingDirectory);
            Optional<String> unavailable = unavailableReason(content);
            if (unavailable.isPresent()) {
                return new RecoveryOutcome.Refused(unavailable.get());
            }
            Path scripts = scriptsFolder(content);
            if (!Files.exists(journalFile(scripts), LinkOption.NOFOLLOW_LINKS)) {
                return new RecoveryOutcome.NothingToDo();
            }
            EditorLockProbe.HoldResult held = locks.hold(content.folder());
            if (held instanceof EditorLockProbe.HoldResult.NotHeld notHeld) {
                return new RecoveryOutcome.Refused(lockRefusal(notHeld.state()).orElse("The project's lock could "
                        + "not be taken."));
            }
            try (EditorLockProbe.Hold ignored = ((EditorLockProbe.HoldResult.Holding) held).hold()) {
                return recoverHeld(content, scripts);
            }
        } catch (RuntimeException e) {
            return new RecoveryOutcome.Failed("Finishing the interrupted update stopped unexpectedly: " + e);
        }
    }

    /** {@link #recover}, with the lock already held. Never throws a checked exception. */
    private RecoveryOutcome recoverHeld(ProjectContent content, Path scripts) {
        boolean finished = false;
        boolean undone = false;
        boolean deletedPrevious = false;
        boolean touched = false;
        try {
            for (int steps = 0; steps < MAX_RECOVERY_STEPS; steps++) {
                RecoveryPlanner.JournalState journal = readJournal(scripts);
                RecoveryPlanner.Disk disk = observe(content, scripts, journal);
                RecoveryPlanner.Step step = RecoveryPlanner.next(journal, disk);
                UpdateJournal valid = journal instanceof RecoveryPlanner.JournalState.Valid v ? v.journal() : null;
                switch (step) {
                    case RecoveryPlanner.Step.Done ignored -> {
                        if (!touched) {
                            return new RecoveryOutcome.NothingToDo();
                        }
                        // The library is the new one when the swap was finished here, or when only the old folder
                        // was left over (the swap itself had happened).
                        boolean completed = finished || (deletedPrevious && !undone);
                        return new RecoveryOutcome.Recovered(completed
                                ? "An interrupted update of the script library was finished."
                                : undone ? "An interrupted update of the script library was undone; the library is "
                                + "as it was before it."
                                : "An interrupted update of the script library was cleaned up.", completed);
                    }
                    case RecoveryPlanner.Step.NeedsAttention attention -> {
                        return new RecoveryOutcome.NeedsAttention(attention.reason() + " (" + scripts + ")");
                    }
                    case RecoveryPlanner.Step.DeleteStaging ignored -> {
                        deleteOwned(scripts.resolve(Objects.requireNonNull(valid).staging()));
                        undone = true;
                    }
                    case RecoveryPlanner.Step.FinishSwap ignored -> {
                        move(scripts.resolve(Objects.requireNonNull(valid).staging()), scripts.resolve(valid.library()),
                                "putting the new library in place");
                        finished = true;
                    }
                    case RecoveryPlanner.Step.RollBack ignored -> {
                        move(scripts.resolve(Objects.requireNonNull(valid).previous()),
                                scripts.resolve(valid.library()), "putting the old library back");
                        undone = true;
                    }
                    case RecoveryPlanner.Step.DeletePrevious ignored -> {
                        Path previous = scripts.resolve(Objects.requireNonNull(valid).previous());
                        // The old library goes only while its backup holds every file of it.
                        String mismatch = backupMismatch(previous, content.folder().resolve(valid.backup()),
                                valid.library());
                        if (mismatch != null) {
                            return new RecoveryOutcome.NeedsAttention(previous + " holds the library as it was "
                                    + "before the update, but " + mismatch + ". Compare it with the backup, then "
                                    + "delete it (" + scripts + ").");
                        }
                        deleteOwned(previous);
                        deletedPrevious = true;
                    }
                    case RecoveryPlanner.Step.DeleteJournal ignored -> TransientIo.retry(
                            () -> Files.deleteIfExists(journalFile(scripts)), ProjectCreator.COMMIT_ATTEMPTS,
                            retryFirstDelay, retryMaxDelay, "deleting the update's journal");
                }
                touched = true;
            }
            return new RecoveryOutcome.NeedsAttention("The interrupted update could not be finished in "
                    + MAX_RECOVERY_STEPS + " steps (" + scripts + ").");
        } catch (IOException e) {
            return new RecoveryOutcome.Failed("Could not finish the interrupted update of " + scripts + ": "
                    + describe(e) + ". Close any program that uses its files, then try again.");
        }
    }

    private static RecoveryPlanner.JournalState readJournal(Path scripts) throws IOException {
        Path file = journalFile(scripts);
        byte[] bytes;
        try {
            if (Files.size(file) > MAX_SMALL_FILE_BYTES) {
                return new RecoveryPlanner.JournalState.Invalid("it is too large", false);
            }
            bytes = Files.readAllBytes(file);
        } catch (NoSuchFileException e) {
            return new RecoveryPlanner.JournalState.Absent();
        }
        try {
            return new RecoveryPlanner.JournalState.Valid(UpdateJournal.parse(bytes));
        } catch (UpdateJournal.InvalidJournalException e) {
            return new RecoveryPlanner.JournalState.Invalid(e.getMessage(), e.newerFormat());
        }
    }

    private static RecoveryPlanner.Disk observe(ProjectContent content, Path scripts,
                                                RecoveryPlanner.JournalState journal) throws IOException {
        if (journal instanceof RecoveryPlanner.JournalState.Valid valid) {
            UpdateJournal j = valid.journal();
            Path staging = scripts.resolve(j.staging());
            RecoveryPlanner.Kind stagingKind = kindOf(staging);
            boolean complete = false;
            if (stagingKind == RecoveryPlanner.Kind.FOLDER) {
                Path manifest = staging.resolve(LibraryManifest.FILE_NAME);
                if (Files.isRegularFile(manifest, LinkOption.NOFOLLOW_LINKS)
                        && Files.size(manifest) <= MAX_SMALL_FILE_BYTES) {
                    complete = sha256(Files.readAllBytes(manifest)).equals(j.newManifestSha256());
                }
            }
            boolean backup = false;
            if (j.backup() != null) {
                try {
                    backup = Files.isRegularFile(content.folder().resolve(j.backup()));
                } catch (java.nio.file.InvalidPathException e) {
                    // A damaged name: there is no backup to rely on.
                }
            }
            return new RecoveryPlanner.Disk(kindOf(scripts.resolve(j.library())), stagingKind, complete,
                    kindOf(scripts.resolve(j.previous())), backup, false);
        }
        String libraryName = libraryName(scripts);
        return new RecoveryPlanner.Disk(kindOf(scripts.resolve(libraryName)), RecoveryPlanner.Kind.MISSING, false,
                RecoveryPlanner.Kind.MISSING, false, !strayUpdateFolders(scripts, libraryName).isEmpty());
    }

    /** Deletes a folder the update made (its staging or previous folder), read-only flags and all. */
    private static void deleteOwned(Path folder) throws IOException {
        List<IOException> failures = new ArrayList<>();
        FileTrees.deleteTreeClearingReadOnly(folder, failures);
        if (!failures.isEmpty()) {
            IOException failure = new IOException("could not delete " + folder + ": " + describe(failures.getFirst()));
            failures.forEach(failure::addSuppressed);
            throw failure;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Helpers

    /**
     * A new preview for the user to confirm again, or a refusal when that plan cannot be built anyway: the same check
     * {@link #preview} makes, so the user is never asked to confirm a plan the update would refuse.
     */
    private static UpdateOutcome changedOrRefused(Preview preview) {
        Optional<String> clash = clashRefusal(preview.plan(), preview.library());
        return clash.isPresent() ? new UpdateOutcome.Refused(clash.get()) : new UpdateOutcome.Changed(preview);
    }

    /** Why a plan cannot be built because a path is a file in one library and a folder in the other; empty when not. */
    static Optional<String> clashRefusal(LibraryPlan plan, Path library) {
        if (plan.clashes().isEmpty()) {
            return Optional.empty();
        }
        return Optional.of("In " + library + ", " + String.join(", ", plan.clashes()) + " would have to be both a "
                + "file and a folder (one library has a file where the other has a folder). Rename or move it, then "
                + "update.");
    }

    /** Why an editor's lock stops an update; empty when it does not. */
    static Optional<String> lockRefusal(EditorLockState state) {
        return switch (state) {
            case EditorLockState.Held held -> Optional.of("The project is open in the editor"
                    + (held.pid().isPresent() ? " (process " + held.pid().getAsLong() + ")" : "")
                    + ". Close the editor, then update the script library.");
            case EditorLockState.Unknown unknown -> Optional.of(EditorLockProbe.HELD_BY_LAUNCHER.equals(unknown.reason())
                    ? "Another VertexForge launcher is changing this project right now. Try again once it is done."
                    : "Could not check whether the editor has the project open (" + unknown.reason() + "), so the "
                    + "script library was not changed.");
            case EditorLockState.Free ignored -> Optional.empty();
            case EditorLockState.Skipped ignored -> Optional.empty();
        };
    }

    /** The library folder's name as the project spells it ({@code lib} when there is none). */
    private static String libraryName(Path scripts) throws IOException {
        try (Stream<Path> children = Files.list(scripts)) {
            return children.map(child -> child.getFileName().toString())
                    .filter(name -> name.equalsIgnoreCase(LIBRARY_FOLDER))
                    .findFirst()
                    .orElse(LIBRARY_FOLDER);
        } catch (NoSuchFileException e) {
            return LIBRARY_FOLDER;
        }
    }

    /** The names beside the library that look like an update's folders. */
    private static List<String> strayUpdateFolders(Path scripts, String libraryName) throws IOException {
        try (Stream<Path> children = Files.list(scripts)) {
            return children.map(child -> child.getFileName().toString())
                    .filter(name -> UpdateJournal.looksLikeUpdateFolder(libraryName, name))
                    .sorted()
                    .toList();
        } catch (NoSuchFileException e) {
            return List.of();
        }
    }

    static RecoveryPlanner.Kind kindOf(Path path) throws IOException {
        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        } catch (NoSuchFileException e) {
            return RecoveryPlanner.Kind.MISSING;
        }
        if (attributes.isDirectory() && (!attributes.isOther() || FileTrees.isOwnFolder(path))) {
            return RecoveryPlanner.Kind.FOLDER;
        }
        return RecoveryPlanner.Kind.OTHER;
    }

    /** Refuses two entries of a library that Windows would see as one (a case-sensitive folder can hold them). */
    private static void requireDistinctNames(FolderTree tree, Path library) throws ScanStop {
        Map<String, String> seen = new HashMap<>();
        List<String> names = new ArrayList<>(tree.folders());
        tree.files().forEach(file -> names.add(file.relative()));
        for (String name : names) {
            String earlier = seen.putIfAbsent(LibraryManifest.pathKey(name), name);
            if (earlier != null) {
                throw ScanStop.refused(library + " holds both \"" + earlier + "\" and \"" + name + "\", which differ "
                        + "only in case.");
            }
        }
    }

    /** The library files of {@code tree}, hashed. */
    private static LibrarySnapshot snapshot(FolderTree tree, String what) throws IOException, ScanStop {
        Map<String, String> hashes = new HashMap<>();
        for (FolderTree.FileEntry file : tree.files()) {
            if (LibraryManifest.isLibraryFile(file.relative())) {
                hashes.put(file.relative(), LibraryManifest.hash(readCapped(file.path(), MAX_FILE_BYTES)));
            }
        }
        try {
            return LibrarySnapshot.of(hashes);
        } catch (LibrarySnapshot.DuplicatePathException e) {
            throw ScanStop.refused("In " + what + ": " + e.getMessage() + ".");
        }
    }

    private static byte[] readCapped(Path file, long max) throws IOException {
        long size = Files.size(file);
        if (size > max) {
            throw new IOException(file + " is " + JobMonitor.formatBytes(size) + ", more than the "
                    + JobMonitor.formatBytes(max) + " a script library file may be");
        }
        return Files.readAllBytes(file);
    }

    /** Writes {@code bytes} into a new file ({@code CREATE_NEW}) and forces them to the disk. */
    private static void writeNew(Path file, byte[] bytes) throws IOException {
        try (FileChannel channel = FileChannel.open(file, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
            ByteBuffer buffer = ByteBuffer.wrap(bytes);
            while (buffer.hasRemaining()) {
                channel.write(buffer);
            }
            channel.force(true);
        }
    }

    /** Renames {@code source} to {@code target} (never replacing anything), retried while another program blocks it. */
    private void move(Path source, Path target, String what) throws IOException {
        TransientIo.retry(() -> mover.move(source, target), ProjectCreator.COMMIT_ATTEMPTS, retryFirstDelay,
                retryMaxDelay, what);
        if (!Files.exists(target, LinkOption.NOFOLLOW_LINKS) || Files.exists(source, LinkOption.NOFOLLOW_LINKS)) {
            throw new IOException(source + " is still there after " + what);
        }
    }

    private static String versionPart(LibraryBaseline baseline) {
        return baseline instanceof LibraryBaseline.Recorded recorded ? recorded.engineVersion() : LEGACY_VERSION;
    }

    static String sha256(byte[] bytes) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance(LibraryManifest.HASH_ALGORITHM).digest(bytes));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(LibraryManifest.HASH_ALGORITHM + " is not available", e);
        }
    }

    private static String describe(Throwable error) {
        return FileErrors.describe(error);
    }
}
