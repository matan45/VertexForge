package com.vertexforge.launcher.service;

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
import com.vertexforge.launcher.project.VersionCompatibility;
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
 * engines it could open with (VK-1681, judged by VK-1690's {@link VersionCompatibility}) and the editor's lock on its
 * folder (VK-1682), as one status per row.
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
     *   <li>no engine Open could offer can open it ({@link VersionCompatibility#openable} is empty) and one of them
     *       finds it {@link VersionCompatibility.Newer newer}: Needs newer engine, naming the version it needs
     *       (VK-1690, replacing VK-1686's schema-only rule). Unlike the editor, which reports a member it refuses
     *       first (exit code 3), this comes before the member rules: a newer project may define other members, and a
     *       newer engine is the remedy either way;</li>
     *   <li>a member the editor refuses, or a schema version it cannot read: Invalid (it would exit with code 3);</li>
     *   <li>no engine can open it and none finds it newer, so each refuses its older schema major: Invalid, in the
     *       editor's words (exit code 3; no migration exists). After the member rules, as in the editor;</li>
     *   <li>another editor has the folder open: Already open;</li>
     *   <li>OK. When the engine Open starts by itself ({@link EngineResolver}'s Ready engine) cannot open it but
     *       another can, the tooltip says so: Open then offers one that can.</li>
     * </ol>
     * The engines Open could offer are {@link VersionCompatibility#targets}: every registered engine that works and a
     * working override. There is no verdict on the versions without them, or before the engines were checked. An
     * engine that does not say what it reads ({@link VersionCompatibility.Unknown}) counts as able to open the project.
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
        // Without a schema version the editor can read, the file is Invalid below whatever the engines say.
        VersionsVerdict versions = schema.isPresent()
                ? versionsVerdict(file, engines, boundEngineId)
                : VersionsVerdict.NONE;
        if (versions.needsNewer() != null) {
            ProjectStatus status = versions.needsNewer();
            return openElsewhere.map(open -> ProjectStatus.needsNewerEngine(status.label(),
                    status.reason() + " It is also " + open + ".")).orElse(status);
        }
        if (file.structureProblem() != null) {
            return ProjectStatus.invalid(file.structureProblem());
        }
        if (schema.isEmpty()) {
            return ProjectStatus.invalid(EditorProjectRules.schemaFormatProblem(file.schemaVersion()));
        }
        if (versions.unsupported() != null) {
            return ProjectStatus.invalid(versions.unsupported());
        }
        if (openElsewhere.isPresent()) {
            return ProjectStatus.alreadyOpen("The project is " + openElsewhere.get()
                    + ". Open is available again once that editor has closed it.");
        }
        String lockNote = folderEditor == FolderEditor.NONE
                && readable.lock() instanceof EditorLockState.Unknown unknown
                ? "Could not check whether another editor has it open: " + unknown.reason() + "."
                : null;
        if (versions.note() != null) {
            return ProjectStatus.ok(lockNote == null ? versions.note() : versions.note() + " " + lockNote);
        }
        if (lockNote != null) {
            return ProjectStatus.ok("The project file can be opened. " + lockNote);
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
     * What the engines say about a project's versions (VK-1690): at most one of the three is set.
     *
     * @param needsNewer  no engine can open it and one finds it newer: the row's status
     * @param unsupported no engine can open it and each refuses it: the editor's message for an Invalid row
     * @param note        another engine can open it, but not the one Open starts by itself: the OK row's tooltip
     */
    private record VersionsVerdict(ProjectStatus needsNewer, String unsupported, String note) {
        static final VersionsVerdict NONE = new VersionsVerdict(null, null, null);
    }

    /**
     * The D2 row rule. No verdict without engines, or with none Open could offer. When none of them can open the
     * project, it needs a newer engine if any of them finds it newer, else each refuses its older schema major. When
     * some can, only the engine {@link EngineResolver} would start by itself is looked at again: if it cannot, the row
     * says so, and Open offers one that can.
     */
    private static VersionsVerdict versionsVerdict(ProjectFile file, EngineOptions engines, String boundEngineId) {
        if (engines == null) {
            return VersionsVerdict.NONE;
        }
        List<EngineOption> targets = VersionCompatibility.targets(engines);
        if (targets.isEmpty()) {
            return VersionsVerdict.NONE;
        }
        // VersionCompatibility.openable, with each verdict kept: openable is empty exactly when every target blocks.
        List<VersionCompatibility.Verdict> verdicts = targets.stream()
                .map(engine -> VersionCompatibility.check(file, engine))
                .toList();
        if (verdicts.stream().allMatch(VersionCompatibility.Verdict::blocksOpen)) {
            List<VersionCompatibility.Newer> newer = verdicts.stream()
                    .filter(VersionCompatibility.Newer.class::isInstance)
                    .map(VersionCompatibility.Newer.class::cast)
                    .toList();
            if (newer.isEmpty()) {
                // Every verdict is Unsupported: the editor's message, for the first engine Open could offer.
                return new VersionsVerdict(null, verdicts.getFirst().reason(), null);
            }
            // The label names the version the verdicts require, never one no newer than the engines' own (a project
            // whose asset format is newer, saved by the same version, "needs a newer VertexForge").
            String required = required(newer);
            return new VersionsVerdict(ProjectStatus.needsNewerEngine(label(required),
                    needsNewerReason(file, targets, required)), null, null);
        }
        if (EngineResolver.resolve(engines, boundEngineId) instanceof LaunchPlan.Ready ready
                && VersionCompatibility.check(file, ready.engine()).blocksOpen()) {
            return new VersionsVerdict(null, null, opensWithAnotherNote(file, ready.engine()));
        }
        return VersionsVerdict.NONE;
    }

    /**
     * The version a project no engine can open needs: the one that saved it, as every
     * {@link VersionCompatibility.Newer} that names a version names it ({@code "VertexForge " + Pv});
     * {@link VersionCompatibility#ANY_NEWER} when none does.
     */
    private static String required(List<VersionCompatibility.Newer> newer) {
        return newer.stream()
                .map(VersionCompatibility.Newer::required)
                .filter(required -> !required.equals(VersionCompatibility.ANY_NEWER))
                .findFirst()
                .orElse(VersionCompatibility.ANY_NEWER);
    }

    /**
     * "Saved by VertexForge 1.1.0 (project format 1.0, asset format 1.1.0). The only engine this launcher knows
     * (1.0.0 Development) cannot open it: it reads project format 1.0 and asset format 1.0.0. Add VertexForge 1.1.0 or
     * later with Engines…."
     *
     * @param targets the engines Open could offer, none of which can open the project
     */
    static String needsNewerReason(ProjectFile file, List<EngineOption> targets, String required) {
        StringBuilder reason = new StringBuilder(VersionCompatibility.describe(file));
        if (targets.size() == 1) {
            EngineOption engine = targets.getFirst();
            reason.append(" The only engine this launcher knows (").append(engine.versionText())
                    .append(") cannot open it: it ").append(VersionCompatibility.reads(engine)).append('.');
        } else {
            reason.append(" None of the engines this launcher knows can open it: ")
                    .append(targets.stream()
                            .map(engine -> engine.versionText() + " " + VersionCompatibility.reads(engine))
                            .collect(Collectors.joining("; ")))
                    .append('.');
        }
        reason.append(required.equals(VersionCompatibility.ANY_NEWER)
                ? " Add " + VersionCompatibility.ANY_NEWER + " with Engines…."
                : " Add " + required + " or later with Engines….");
        return reason.toString();
    }

    /**
     * "Saved by VertexForge 1.1.0. The engine it opens with (1.0.0 Development) cannot open it, so Open offers one that
     * can."
     */
    static String opensWithAnotherNote(ProjectFile file, EngineOption engine) {
        return "Saved by " + VersionCompatibility.savedBy(file) + ". The engine it opens with (" + engine.versionText()
                + ") cannot open it, so Open offers one that can.";
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
