package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.util.SingleFlightChecks;

import java.nio.file.Path;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

/**
 * Checks engines in the background, each with its own timeout (VK-1681), exactly as the project checks do (see
 * {@link SingleFlightChecks}): an engine on an unreachable share never freezes the window, and a check that does not
 * answer in time reads as {@link EngineHealth.Kind#MISSING}.
 */
public final class EngineHealthChecker implements AutoCloseable {

    public static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(3);

    private final SingleFlightChecks<Path, EngineHealth> checks;

    public EngineHealthChecker(EngineProbe probe, Duration timeout) {
        Objects.requireNonNull(probe, "probe");
        Objects.requireNonNull(timeout, "timeout");
        this.checks = new SingleFlightChecks<>(
                "vf-engine-",
                timeout,
                probe::probe,
                EngineHealth.broken("Could not check the engine"),
                e -> EngineHealth.broken("Could not check the engine: " + e),
                EngineHealth.missing("No response after " + SingleFlightChecks.describe(timeout)
                        + ": the engine's drive or network share may be unreachable"),
                EngineHealth.missing("The launcher is shutting down"));
    }

    /** The editor's health; a {@code null} path (an entry without a valid path) is Broken at once. */
    public CompletableFuture<EngineHealth> check(Path executable) {
        if (executable == null) {
            return CompletableFuture.completedFuture(
                    EngineHealth.broken("The stored path is not a valid absolute path"));
        }
        return checks.check(executable);
    }

    /** How many editors are being checked right now (for tests). */
    int inFlightCount() {
        return checks.inFlightCount();
    }

    @Override
    public void close() {
        checks.close();
    }
}
