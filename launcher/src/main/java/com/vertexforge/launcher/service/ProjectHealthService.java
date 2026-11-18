package com.vertexforge.launcher.service;

import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.EditorProjectRules;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.SchemaVersion;
import com.vertexforge.launcher.util.SingleFlightChecks;

import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.function.Function;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

/**
 * The health of every listed project (VK-1686): the project file (VK-1685's reader and the editor's own rules), the
 * engine it would open with (VK-1681) and the editor's lock on its folder (VK-1682), as one status per row.
 * <p>
 * It has two halves, because the file and the engines are checked separately and answer in any order:
 * <ul>
 *   <li>{@link #check} looks at the file in the background (VK-1678): probes run on a cached pool of daemon platform
 *       threads ({@code vf-status-N}), at most one per path at a time, and every caller gets its own copy of the
 *       answer with its own timeout, after which the file is {@code Unreadable} and a late answer is ignored. So a
 *       project on an unreachable network share never freezes the window. Once the file was read, the folder's
 *       editor lock is probed the same way ({@code vf-lock-N}, one probe per folder at a time) with a timeout of its
 *       own: a lock that does not answer in time is {@code Unknown}, which never blocks Open.</li>
 *   <li>{@link #evaluate} is pure: it turns the file's answer and the engines as last checked into the row's status,
 *       on the FX thread, whenever either of them changes.</li>
 * </ul>
 */
public final class ProjectHealthService implements AutoCloseable {

    public static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(3);

    /** Added to a Missing row's tooltip. */
    static final String LOCATE_HINT = "If it was moved or renamed, right-click it and choose Locate…";

    private static final Pattern DISPLAY_VERSION = Pattern.compile("VertexForge \\d+\\.\\d+\\.\\d+");
    private static final Pattern BARE_VERSION = Pattern.compile("\\d+\\.\\d+\\.\\d+");

    /** Which editor of this launcher has the project's folder open; the folder's lock is only probed for NONE. */
    public enum FolderEditor {
        NONE,
        /** This very project: the row shows the editor's phase, and a second Open says it already runs. */
        THIS_PROJECT,
        /** Another project file of the same folder, so this one is locked too. */
        OTHER_PROJECT
    }

    private final SingleFlightChecks<Path, ProjectFileCheck> files;
    private final SingleFlightChecks<Path, EditorLockState> locks;

    /**
     * @param probe     reads a project file
     * @param lockProbe tells whether an editor holds a project folder ({@code EditorLockProbe::probe})
     * @param timeout   how long the file's read may take, and then how long its folder's lock check may take
     */
    public ProjectHealthService(PathProbe probe, Function<Path, EditorLockState> lockProbe, Duration timeout) {
        Objects.requireNonNull(probe, "probe");
        Objects.requireNonNull(lockProbe, "lockProbe");
        Objects.requireNonNull(timeout, "timeout");
        if (timeout.isNegative() || timeout.isZero()) {
            throw new IllegalArgumentException("timeout must be positive");
        }
        String waited = SingleFlightChecks.describe(timeout);
        this.files = new SingleFlightChecks<>(
                "vf-status-",
                timeout,
                probe::probe,
                new ProjectFileCheck.Unreadable("Could not check the project file"),
                e -> new ProjectFileCheck.Unreadable("Could not check the project file: " + e),
                new ProjectFileCheck.Unreadable("No response after " + waited),
                new ProjectFileCheck.Unreadable("The launcher is shutting down"));
        this.locks = new SingleFlightChecks<>(
                "vf-lock-",
                timeout,
                lockProbe,
                new EditorLockState.Unknown("the check gave no answer"),
                e -> new EditorLockState.Unknown(e.toString()),
                new EditorLockState.Unknown("no response after " + waited),
                new EditorLockState.Unknown("the launcher is shutting down"));
    }

    /**
     * What the entry's project file holds; an entry without a usable path is {@code Unreadable} at once.
     *
     * @param probeLock whether to look at the folder's editor lock (see {@link FolderEditor})
     */
    public CompletableFuture<ProjectFileCheck> check(RecentEntry entry, boolean probeLock) {
        Objects.requireNonNull(entry, "entry");
        return check(entry.path(), probeLock);
    }

    /**
     * As {@link #check(RecentEntry, boolean)}. The lock is probed only once the file was read; its answer replaces the
     * {@link EditorLockState#SKIPPED} the read gives. Never completes exceptionally.
     */
    public CompletableFuture<ProjectFileCheck> check(Path path, boolean probeLock) {
        if (path == null) {
            return CompletableFuture.completedFuture(
                    new ProjectFileCheck.Unreadable("The stored path is not a valid absolute path"));
        }
        CompletableFuture<ProjectFileCheck> file = files.check(path);
        Path folder = path.toAbsolutePath().getParent();
        if (!probeLock || folder == null) {
            return file;
        }
        return file.thenCompose(check -> check instanceof ProjectFileCheck.Readable readable
                ? locks.check(folder).thenApply(lock -> new ProjectFileCheck.Readable(readable.file(), lock))
                : CompletableFuture.completedFuture(check));
    }

    /** How many files and folders are being probed right now (for tests). */
    int inFlightCount() {
        return files.inFlightCount() + locks.inFlightCount();
    }

    @Override
    public void close() {
        files.close();
        locks.close();
    }

    /**
     * The row's status. The first that applies wins; as in the editor, the file comes before its lock:
     * <ol>
     *   <li>the file is Missing, Unreadable, or not a project file the launcher can read (Invalid);</li>
     *   <li>its schema version is newer than every engine Open could start reads: Needs newer engine (the editor would
     *       exit with code 4). Unlike the editor, which reports a member it refuses first (exit code 3), this comes
     *       before the member rules: a newer schema may define other members, and a newer engine is the remedy either
     *       way;</li>
     *   <li>a member the editor refuses, or a schema version it cannot read: Invalid (it would exit with code 3). An
     *       OLDER schema major is not judged here: the editor refuses it too, and VK-1690 offers to upgrade it;</li>
     *   <li>another editor has the folder open: Already open;</li>
     *   <li>OK.</li>
     * </ol>
     *
     * @param check         the file's answer
     * @param engines       the engines as last checked, or {@code null} before the first check answered (no verdict
     *                      on the version then)
     * @param boundEngineId the entry's {@code engineId}, or {@code null} when it is not bound
     */
    public static ProjectStatus evaluate(ProjectFileCheck check, EngineOptions engines, String boundEngineId,
                                         FolderEditor folderEditor) {
        Objects.requireNonNull(check, "check");
        Objects.requireNonNull(folderEditor, "folderEditor");
        return switch (check) {
            case ProjectFileCheck.Missing missing -> ProjectStatus.missing(missing.reason() + ". " + LOCATE_HINT);
            case ProjectFileCheck.Unreadable unreadable -> ProjectStatus.unreadable(unreadable.reason());
            case ProjectFileCheck.Invalid invalid -> ProjectStatus.invalid(invalid.reason());
            case ProjectFileCheck.Readable readable -> evaluateReadable(readable, engines, boundEngineId, folderEditor);
        };
    }

    private static ProjectStatus evaluateReadable(ProjectFileCheck.Readable readable, EngineOptions engines,
                                                  String boundEngineId, FolderEditor folderEditor) {
        ProjectFile file = readable.file();
        Optional<String> openElsewhere = openElsewhere(readable.lock(), folderEditor);

        Optional<SchemaVersion> schema = file.schema();
        if (schema.isPresent()) {
            Optional<ProjectStatus> newer = needsNewerEngine(file, schema.get(), engines, boundEngineId);
            if (newer.isPresent()) {
                ProjectStatus status = newer.get();
                return openElsewhere.map(open -> ProjectStatus.needsNewerEngine(status.label(),
                        status.reason() + " It is also " + open + ".")).orElse(status);
            }
        }
        if (file.structureProblem() != null) {
            return ProjectStatus.invalid(file.structureProblem());
        }
        if (schema.isEmpty()) {
            return ProjectStatus.invalid(EditorProjectRules.schemaFormatProblem(file.schemaVersion()));
        }
        if (openElsewhere.isPresent()) {
            return ProjectStatus.alreadyOpen("The project is " + openElsewhere.get()
                    + ". Open is available again once that editor has closed it.");
        }
        if (folderEditor == FolderEditor.NONE && readable.lock() instanceof EditorLockState.Unknown unknown) {
            return ProjectStatus.ok("The project file can be opened. Could not check whether another editor has it"
                    + " open: " + unknown.reason() + ".");
        }
        return ProjectStatus.ok();
    }

    /** "already open in another editor (pid 12)", or empty when no other editor has the folder open. */
    private static Optional<String> openElsewhere(EditorLockState lock, FolderEditor folderEditor) {
        return switch (folderEditor) {
            // The row shows the editor's own phase; the lock it holds is its own.
            case THIS_PROJECT -> Optional.empty();
            case OTHER_PROJECT -> Optional.of("already open in an editor this launcher started, on another project"
                    + " file of this folder");
            case NONE -> lock instanceof EditorLockState.Held held
                    ? Optional.of("already open in another editor"
                    + (held.pid().isPresent() ? " (pid " + held.pid().getAsLong() + ")" : ""))
                    : Optional.empty();
        };
    }

    /**
     * Needs newer engine when every engine Open could start reads an older schema major than the project's: the one
     * {@link EngineResolver} picks, or every candidate it would offer. No verdict without engines, with no engine to
     * offer, or when an engine's schema version is unknown (VK-1690 then warns on open).
     */
    private static Optional<ProjectStatus> needsNewerEngine(ProjectFile file, SchemaVersion schema,
                                                            EngineOptions engines, String boundEngineId) {
        if (engines == null) {
            return Optional.empty();
        }
        List<EngineOption> targets = switch (EngineResolver.resolve(engines, boundEngineId)) {
            case LaunchPlan.Ready ready -> List.of(ready.engine());
            case LaunchPlan.Choose choose -> choose.candidates();
        };
        if (targets.isEmpty()) {
            return Optional.empty();
        }
        for (EngineOption engine : targets) {
            Optional<SchemaVersion> reads = schemaRead(engine);
            if (reads.isEmpty() || reads.get().major() >= schema.major()) {
                return Optional.empty();
            }
        }

        String savedBy = file.engineVersion() != null && !file.engineVersion().isBlank()
                ? file.engineVersion().strip() : "a newer VertexForge";
        StringBuilder reason = new StringBuilder("Saved by ").append(savedBy)
                .append(" (project format ").append(schema).append(").");
        if (targets.size() == 1) {
            EngineOption engine = targets.getFirst();
            reason.append(" The engine it opens with (").append(engine.versionText()).append(") reads project format ")
                    .append(schemaRead(engine).orElseThrow().major()).append(".x.");
        } else {
            reason.append(" None of the engines it can open with (")
                    .append(targets.stream().map(EngineOption::versionText).collect(Collectors.joining(", ")))
                    .append(") reads project format ").append(schema.major()).append('.');
        }
        reason.append(" Use Change Engine… to pick an engine that reads format ").append(schema.major())
                .append(" (add one with Engines… if none does).");
        return Optional.of(ProjectStatus.needsNewerEngine(label(file.engineVersion()), reason.toString()));
    }

    /** The schema version an engine reads ({@code projectSchemaVersion} in its engine.json), when it is known. */
    private static Optional<SchemaVersion> schemaRead(EngineOption engine) {
        EngineManifest manifest = engine.health().manifest();
        return manifest == null ? Optional.empty() : SchemaVersion.parse(manifest.projectSchemaVersion());
    }

    /** "Needs VertexForge 2.1.0" from the project's {@code engineVersion}, else "Needs newer VertexForge". */
    static String label(String engineVersion) {
        String version = engineVersion == null ? "" : engineVersion.strip();
        if (DISPLAY_VERSION.matcher(version).matches()) {
            return "Needs " + version;
        }
        if (BARE_VERSION.matcher(version).matches()) {
            return "Needs VertexForge " + version;
        }
        return ProjectStatus.Kind.NEEDS_NEWER_ENGINE.label();
    }
}
