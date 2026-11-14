package com.vertexforge.launcher.service;

import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.util.SingleFlightChecks;

import java.nio.file.Path;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

/**
 * Checks project files in the background, each with its own timeout (VK-1678), so that a project on an unreachable
 * network share never freezes the window. The threading lives in {@link SingleFlightChecks}:
 * <ul>
 *   <li>Probes run on a cached pool of daemon platform threads ({@code vf-status-N}).</li>
 *   <li>There is at most one probe per path at a time, so stuck threads never pile up.</li>
 *   <li>Every caller gets its own copy of the result with its own timeout; after the timeout the status is
 *       {@code UNREADABLE} and a late answer is ignored.</li>
 * </ul>
 */
public final class ProjectStatusChecker implements AutoCloseable {

    public static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(3);

    private final SingleFlightChecks<Path, ProjectStatus> checks;

    public ProjectStatusChecker(PathProbe probe, Duration timeout) {
        Objects.requireNonNull(probe, "probe");
        Objects.requireNonNull(timeout, "timeout");
        if (timeout.isNegative() || timeout.isZero()) {
            throw new IllegalArgumentException("timeout must be positive");
        }
        this.checks = new SingleFlightChecks<>(
                "vf-status-",
                timeout,
                probe::probe,
                ProjectStatus.unreadable("Could not check the project file"),
                e -> ProjectStatus.unreadable("Could not check the project file: " + e),
                ProjectStatus.unreadable("No response after " + SingleFlightChecks.describe(timeout)),
                ProjectStatus.unreadable("The launcher is shutting down"));
    }

    /** The entry's status; an entry without a usable path is {@code UNREADABLE} at once. */
    public CompletableFuture<ProjectStatus> check(RecentEntry entry) {
        Objects.requireNonNull(entry, "entry");
        return check(entry.path());
    }

    public CompletableFuture<ProjectStatus> check(Path path) {
        if (path == null) {
            return CompletableFuture.completedFuture(
                    ProjectStatus.unreadable("The stored path is not a valid absolute path"));
        }
        return checks.check(path);
    }

    /** How many paths are being probed right now (for tests). */
    int inFlightCount() {
        return checks.inFlightCount();
    }

    @Override
    public void close() {
        checks.close();
    }
}
