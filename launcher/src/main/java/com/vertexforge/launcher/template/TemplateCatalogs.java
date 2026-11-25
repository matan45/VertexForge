package com.vertexforge.launcher.template;

import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.util.FileErrors;
import com.vertexforge.launcher.util.SingleFlightChecks;

import java.io.IOException;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

/**
 * Reads engines' templates in the background, each read with a timeout (VK-1689), so that an engine on an unreachable
 * network share never freezes the Create Project dialog. At most one read per engine runs at a time.
 */
public final class TemplateCatalogs implements AutoCloseable {

    public static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(10);

    /**
     * A read's answer: the catalog, or why there is none.
     *
     * @param catalog what the engine offers; {@code null} exactly when {@code problem} is set
     * @param problem why the engine's templates could not be read
     */
    public record Answer(TemplateCatalog catalog, String problem) {
        public Answer {
            if ((catalog == null) == (problem == null)) {
                throw new IllegalArgumentException("an answer has a catalog or a problem");
            }
        }

        public static Answer of(TemplateCatalog catalog) {
            return new Answer(Objects.requireNonNull(catalog, "catalog"), null);
        }

        public static Answer failed(String problem) {
            return new Answer(null, Objects.requireNonNull(problem, "problem"));
        }
    }

    private final SingleFlightChecks<EngineContent, Answer> reads;

    public TemplateCatalogs(TemplateScanner scanner, Duration timeout) {
        Objects.requireNonNull(scanner, "scanner");
        this.reads = new SingleFlightChecks<>("vf-templates-", timeout,
                engine -> {
                    try {
                        return Answer.of(scanner.scan(engine));
                    } catch (IOException e) {
                        return Answer.failed("The templates of " + engine.label() + " could not be read: "
                                + FileErrors.describe(e));
                    }
                },
                Answer.failed("The engine's templates could not be read."),
                e -> Answer.failed("The engine's templates could not be read: " + e),
                Answer.failed("The engine's templates did not answer within " + SingleFlightChecks.describe(timeout)
                        + ": they may be in an unreachable network folder."),
                Answer.failed("The launcher is closing."));
    }

    /** What {@code engine} offers. Returns at once; never completes exceptionally. */
    public CompletableFuture<Answer> list(EngineContent engine) {
        return reads.check(Objects.requireNonNull(engine, "engine"));
    }

    @Override
    public void close() {
        reads.close();
    }
}
