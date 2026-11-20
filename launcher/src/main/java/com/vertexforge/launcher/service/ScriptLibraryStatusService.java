package com.vertexforge.launcher.service;

import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.project.ProjectContent;
import com.vertexforge.launcher.scriptlib.RecoveryPlanner;
import com.vertexforge.launcher.template.LibraryManifest;
import com.vertexforge.launcher.util.SingleFlightChecks;
import com.vertexforge.launcher.util.VersionNumber;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Whether a listed project's script library is behind its engine's (VK-1691): the "Library outdated" tag of the
 * project list. Cheap enough for every row: the engine's library is hashed once (then only its names, sizes and times
 * are looked at again), and a project costs one read of its small {@code .vflib.json}. It never hashes a project's
 * files, so the user's own edits and deletions do not show; the update's preview shows those.
 */
public final class ScriptLibraryStatusService implements AutoCloseable {

    /** How long a row's check may take, as a file check may. */
    public static final Duration DEFAULT_TIMEOUT = ProjectHealthService.DEFAULT_TIMEOUT;

    /** What the tag says. */
    public enum LibraryState {
        /** The library records exactly the engine's library: no tag. */
        UP_TO_DATE(""),
        /** The library records another version of the engine's library. */
        OUTDATED("Library outdated"),
        /**
         * The library predates library tracking (no {@code .vflib.json}): every library of that time differs from
         * today's, and the update at least records a baseline.
         */
        LEGACY("Library outdated"),
        /** The project has no script library folder. */
        MISSING("Library missing"),
        /** An update of the library was interrupted. */
        INTERRUPTED("Library update interrupted"),
        /** Not known (no usable engine, an unreadable file, a timeout, a working directory outside the project). */
        UNKNOWN("");

        private final String tag;

        LibraryState(String tag) {
            this.tag = tag;
        }

        /** The tag's text; empty for none. */
        public String tag() {
            return tag;
        }

        /** The tag's tooltip; empty for none. */
        public String tooltip() {
            return switch (this) {
                case OUTDATED -> "The engine's script library changed since this project's was installed. "
                        + "Right-click the project, then Update Script Library….";
                case LEGACY -> "This project's script library was installed before libraries were tracked. "
                        + "Right-click the project, then Update Script Library….";
                case MISSING -> "The project has no script library. Right-click the project, then Update Script "
                        + "Library….";
                case INTERRUPTED -> "An update of this project's script library did not finish. Right-click the "
                        + "project, then Update Script Library… to finish it.";
                case UP_TO_DATE, UNKNOWN -> "";
            };
        }
    }

    /**
     * An engine's script library, as {@code engine.json} names it.
     *
     * @param folder  the library folder
     * @param version the engine's {@code MAJOR.MINOR.PATCH}
     */
    public record EngineLibrary(Path folder, String version) {
        public EngineLibrary {
            Objects.requireNonNull(folder, "folder");
            Objects.requireNonNull(version, "version");
        }

        /** The script library of a usable engine; empty when it is not usable or names none. */
        public static Optional<EngineLibrary> of(EngineOption engine) {
            if (engine == null || !engine.isOk() || engine.health().manifest() == null
                    || engine.executable() == null || engine.executable().getParent() == null) {
                return Optional.empty();
            }
            return engine.health().manifest().scriptLibDirIn(engine.executable().getParent())
                    .map(folder -> new EngineLibrary(folder, engine.health().manifest().engineVersion()));
        }
    }

    /** One row's question. */
    private record Key(Path projectFile, String workingDirectory, Path engineLibrary, String engineVersion) {
    }

    /** An engine library's hashes, with what they were taken from. */
    private record EngineIndex(List<Signature> signature, SortedMap<String, String> hashes) {
    }

    /** A file as listed: enough to tell that it changed without reading it. */
    private record Signature(String relative, long size, long modifiedMillis) {
    }

    private final SingleFlightChecks<Key, LibraryState> checks;
    /** Whether a scripts folder holds an interrupted update's journal, by scripts folder. */
    private final SingleFlightChecks<Path, Boolean> journals;
    private final Map<Path, EngineIndex> engineIndexes = new ConcurrentHashMap<>();

    public ScriptLibraryStatusService(Duration timeout) {
        this.checks = new SingleFlightChecks<>("vf-lib-", timeout, this::evaluate, LibraryState.UNKNOWN,
                e -> LibraryState.UNKNOWN, LibraryState.UNKNOWN, LibraryState.UNKNOWN);
        this.journals = new SingleFlightChecks<>("vf-lib-journal-", timeout,
                scripts -> Files.exists(ScriptLibraryUpdater.journalFile(scripts), LinkOption.NOFOLLOW_LINKS),
                false, e -> false, false, false);
    }

    /**
     * Whether an update of the project's script library was interrupted (its journal is still there), for Open to
     * finish it first; {@code false} when that cannot be told (in time). Never completes exceptionally.
     */
    public CompletableFuture<Boolean> interrupted(Path projectFile, String workingDirectory) {
        Objects.requireNonNull(projectFile, "projectFile");
        ProjectContent content = ProjectContent.of(projectFile, workingDirectory);
        if (ScriptLibraryUpdater.unavailableReason(content).isPresent()) {
            return CompletableFuture.completedFuture(false);
        }
        return journals.check(ScriptLibraryUpdater.scriptsFolder(content));
    }

    /** The state of the project's library against {@code engine}'s; completes within the timeout, never exceptionally. */
    public CompletableFuture<LibraryState> check(Path projectFile, String workingDirectory, EngineLibrary engine) {
        Objects.requireNonNull(projectFile, "projectFile");
        Objects.requireNonNull(engine, "engine");
        return checks.check(new Key(projectFile.toAbsolutePath().normalize(), workingDirectory, engine.folder(),
                engine.version()));
    }

    @Override
    public void close() {
        checks.close();
        journals.close();
    }

    private LibraryState evaluate(Key key) {
        try {
            return evaluate(key.projectFile(), key.workingDirectory(), engineHashes(key.engineLibrary()),
                    key.engineVersion());
        } catch (IOException e) {
            return LibraryState.UNKNOWN;
        } catch (UncheckedIOException e) {
            return LibraryState.UNKNOWN;
        }
    }

    /**
     * The state of a project's library, given the engine's library hashes (exact path to hash), whatever version the
     * library records. Blocks on the file system.
     */
    static LibraryState evaluate(Path projectFile, String workingDirectory, SortedMap<String, String> engineHashes)
            throws IOException {
        return evaluate(projectFile, workingDirectory, engineHashes, null);
    }

    /**
     * The state of a project's library, given the engine's library hashes (exact path to hash) and version: a library
     * recorded by a newer engine than {@code engineVersion} has no tag, because the update refuses to take it back
     * ({@code null}: not compared). Blocks on the file system.
     */
    static LibraryState evaluate(Path projectFile, String workingDirectory, SortedMap<String, String> engineHashes,
                                 String engineVersion) throws IOException {
        ProjectContent content = ProjectContent.of(projectFile, workingDirectory);
        if (ScriptLibraryUpdater.unavailableReason(content).isPresent()) {
            return LibraryState.UNKNOWN;
        }
        Path scripts = ScriptLibraryUpdater.scriptsFolder(content);
        if (Files.exists(ScriptLibraryUpdater.journalFile(scripts), LinkOption.NOFOLLOW_LINKS)) {
            return LibraryState.INTERRUPTED;
        }
        Path library = scripts.resolve(ScriptLibraryUpdater.LIBRARY_FOLDER);
        RecoveryPlanner.Kind kind = ScriptLibraryUpdater.kindOf(library);
        if (kind == RecoveryPlanner.Kind.MISSING) {
            return Files.isDirectory(scripts) ? LibraryState.MISSING : LibraryState.UNKNOWN;
        }
        if (kind != RecoveryPlanner.Kind.FOLDER) {
            return LibraryState.UNKNOWN;
        }
        Path manifest = library.resolve(LibraryManifest.FILE_NAME);
        if (!Files.isRegularFile(manifest, LinkOption.NOFOLLOW_LINKS)) {
            return LibraryState.LEGACY;
        }
        if (Files.size(manifest) > ScriptLibraryUpdater.MAX_SMALL_FILE_BYTES) {
            return LibraryState.LEGACY;
        }
        try {
            LibraryManifest.Contents contents = LibraryManifest.parse(Files.readAllBytes(manifest));
            if (contents.files().equals(engineHashes)) {
                return LibraryState.UP_TO_DATE;
            }
            Optional<VersionNumber> recorded = VersionNumber.parse(contents.engineVersion());
            Optional<VersionNumber> engine = VersionNumber.parse(engineVersion);
            if (recorded.isPresent() && engine.isPresent() && recorded.get().compareTo(engine.get()) > 0) {
                // The update would refuse it ("newer than this engine"): no tag offers it.
                return LibraryState.UNKNOWN;
            }
            return LibraryState.OUTDATED;
        } catch (LibraryManifest.InvalidLibraryManifestException e) {
            // A newer launcher's record cannot be judged here; an unreadable one is treated as no record at all.
            return e.newerFormat() ? LibraryState.UNKNOWN : LibraryState.LEGACY;
        }
    }

    /** The engine library's hashes, from the cache while no file was added, removed, resized or touched. */
    private SortedMap<String, String> engineHashes(Path folder) throws IOException {
        List<Signature> signature = signature(folder);
        EngineIndex cached = engineIndexes.get(folder);
        if (cached != null && cached.signature().equals(signature)) {
            return cached.hashes();
        }
        SortedMap<String, String> hashes = new TreeMap<>();
        for (Signature file : signature) {
            hashes.put(file.relative(), LibraryManifest.hash(Files.readAllBytes(folder.resolve(file.relative()))));
        }
        engineIndexes.put(folder, new EngineIndex(signature, hashes));
        return hashes;
    }

    /**
     * Every library file below {@code folder}, sorted, as {@link FolderTree#scan} lists it, which is how the updater
     * and project creation take a library (links, and junctions to other folders, are left out).
     */
    private static List<Signature> signature(Path folder) throws IOException {
        return FolderTree.scan(folder, relative -> false, JobMonitor.NONE).files().stream()
                .filter(file -> LibraryManifest.isLibraryFile(file.relative()))
                .map(file -> new Signature(file.relative(), file.size(), file.lastModified().toMillis()))
                .toList();
    }
}
