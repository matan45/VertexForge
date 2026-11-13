package com.vertexforge.launcher.service;

import com.vertexforge.launcher.util.ProjectValidator;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.function.Supplier;

/**
 * Checks, in the background, whether a project can be created where the Create dialog points (VK-1679), so that an
 * unreachable network folder never freezes the dialog. It applies the same checks as {@link ProjectCreator}, which
 * still runs them again itself before writing anything.
 * <ul>
 *   <li>Checks run on a cached pool of daemon threads; a call on a dead share cannot be interrupted, so its thread
 *       stays busy until Windows gives up.</li>
 *   <li>There is at most one check per folder at a time, and the project's own path is only looked at once its
 *       folder has answered. Typing a name while the folder is stuck therefore never piles up stuck threads.</li>
 *   <li>There is no timeout: a slow folder that does answer still gets its answer. The dialog says when one is taking
 *       long.</li>
 * </ul>
 */
public final class LocationChecker implements AutoCloseable {

    /**
     * What the dialog shows: a reason Create is not possible, or the warnings to show while it is.
     *
     * @param error    why no project can be created there, or empty
     * @param warnings things to know about the place; only set when {@code error} is empty
     */
    public record LocationCheck(Optional<String> error, List<String> warnings) {
        public LocationCheck {
            Objects.requireNonNull(error, "error");
            warnings = List.copyOf(warnings);
        }

        public static LocationCheck ok(List<String> warnings) {
            return new LocationCheck(Optional.empty(), warnings);
        }

        public static LocationCheck refused(String error) {
            return new LocationCheck(Optional.of(error), List.of());
        }

        public boolean ok() {
            return error.isEmpty();
        }
    }

    /** Looks at the folder itself: exists, is a folder, may be written. Blocks. */
    @FunctionalInterface
    interface FolderProbe {
        Optional<String> probe(Path location);
    }

    /** Looks at the project's own path in a folder that answered. Blocks. */
    @FunctionalInterface
    interface TargetProbe {
        LocationCheck probe(Path location, String name);
    }

    /** The project's path; the name compares exactly, so its spelling in the messages is the one typed. */
    private record TargetKey(Path location, String name) {
    }

    private final FolderProbe folderProbe;
    private final TargetProbe targetProbe;
    private final ExecutorService pool;
    private final ConcurrentHashMap<Path, CompletableFuture<Optional<String>>> folders = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<TargetKey, CompletableFuture<LocationCheck>> targets = new ConcurrentHashMap<>();

    public LocationChecker(LocationWarnings warnings, ProjectTemplate.Source templates) {
        this(ProjectCreator::inspectLocation, defaultTargetProbe(warnings, templates));
    }

    LocationChecker(FolderProbe folderProbe, TargetProbe targetProbe) {
        this.folderProbe = Objects.requireNonNull(folderProbe, "folderProbe");
        this.targetProbe = Objects.requireNonNull(targetProbe, "targetProbe");
        AtomicInteger count = new AtomicInteger();
        this.pool = Executors.newCachedThreadPool(runnable -> {
            Thread thread = new Thread(runnable, "vf-location-" + count.incrementAndGet());
            thread.setDaemon(true);
            return thread;
        });
    }

    /**
     * Whether a project called {@code name} can be created in {@code location}. Returns at once; the answer arrives on
     * one of the checker's threads (or at once, when the name or the location is wrong as text).
     */
    public CompletableFuture<LocationCheck> check(Path location, String name) {
        Objects.requireNonNull(location, "location");
        Objects.requireNonNull(name, "name");
        Optional<ProjectNameRules.Problem> nameProblem = ProjectNameRules.checkName(name);
        if (nameProblem.isPresent()) {
            return CompletableFuture.completedFuture(LocationCheck.refused(nameProblem.get().message()));
        }
        if (!location.isAbsolute()) {
            return CompletableFuture.completedFuture(LocationCheck.refused(ProjectCreator.LOCATION_NOT_ABSOLUTE));
        }

        Path folder = location.normalize();
        return shared(folders, folder, () -> folderProbe.probe(folder),
                error -> Optional.of("Could not check the folder: " + ProjectCreator.describe(error)))
                .thenCompose(problem -> problem.isPresent()
                        ? CompletableFuture.completedFuture(LocationCheck.refused(problem.get()))
                        : shared(targets, new TargetKey(folder, name), () -> targetProbe.probe(folder, name),
                        error -> LocationCheck.refused("Could not check the location: "
                                + ProjectCreator.describe(error))));
    }

    /** How many folders and project paths are being checked right now (for tests). */
    int inFlightCount() {
        return folders.size() + targets.size();
    }

    @Override
    public void close() {
        pool.shutdownNow();
    }

    /**
     * The running check for {@code key}, or a new one. Every caller gets its own copy, so none can complete the shared
     * one. A finished check leaves the map BEFORE it completes (see {@code ProjectStatusChecker.finish}).
     */
    private <K, V> CompletableFuture<V> shared(ConcurrentHashMap<K, CompletableFuture<V>> inFlight, K key,
                                                Supplier<V> work, Function<Throwable, V> onError) {
        CompletableFuture<V> mine = new CompletableFuture<>();
        CompletableFuture<V> running = inFlight.putIfAbsent(key, mine);
        if (running != null) {
            return running.copy();
        }
        try {
            pool.execute(() -> {
                V value = null;
                try {
                    value = work.get();
                } catch (RuntimeException e) {
                    value = onError.apply(e);
                } finally {
                    // Even an Error must not leave the key taken: every later check would wait on it forever.
                    if (value == null) {
                        value = onError.apply(new IllegalStateException("the check did not finish"));
                    }
                    inFlight.remove(key, mine);
                    mine.complete(value);
                }
            });
        } catch (RejectedExecutionException e) {
            inFlight.remove(key, mine);
            mine.complete(onError.apply(new IllegalStateException("the launcher is closing")));
        }
        return mine.copy();
    }

    private static TargetProbe defaultTargetProbe(LocationWarnings warnings, ProjectTemplate.Source templates) {
        Objects.requireNonNull(warnings, "warnings");
        Objects.requireNonNull(templates, "templates");
        return (location, name) -> {
            ProjectTemplate template;
            try {
                template = templates.load();
            } catch (IOException e) {
                return LocationCheck.refused(ProjectCreator.describe(e));
            }
            Optional<ProjectNameRules.Problem> tooLong = ProjectNameRules.checkPathLength(location, name, template);
            if (tooLong.isPresent()) {
                return LocationCheck.refused(tooLong.get().message());
            }
            ProjectCreator.TargetInspection target = ProjectCreator.inspectTarget(location, name);
            if (target.refused()) {
                return LocationCheck.refused(target.problem());
            }
            Path projectFile = location.resolve(name).resolve(name + ProjectValidator.PROJECT_EXTENSION);
            return LocationCheck.ok(warnings.warningsFor(location, projectFile));
        };
    }
}
