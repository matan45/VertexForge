package com.vertexforge.launcher.engine;

import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;

/** What Open does for a project, decided by {@link EngineResolver} (VK-1681). */
public sealed interface LaunchPlan {

    /**
     * Start this engine.
     *
     * @param engine the engine, always OK
     * @param bindTo the engine to bind the project to once the editor has started (an unbound project's first open);
     *               empty to leave the project's binding as it is
     */
    record Ready(EngineOption engine, Optional<UUID> bindTo) implements LaunchPlan {
        public Ready {
            Objects.requireNonNull(engine, "engine");
            if (!engine.isOk()) {
                throw new IllegalArgumentException("only an OK engine can be started");
            }
            bindTo = bindTo == null ? Optional.empty() : bindTo;
        }
    }

    /**
     * Ask the user which engine to use.
     *
     * @param problem           why the launcher has to ask, for the user
     * @param candidates        the engines that work (possibly none)
     * @param rememberByDefault whether "use this engine for this project from now on" starts ticked
     */
    record Choose(String problem, List<EngineOption> candidates, boolean rememberByDefault) implements LaunchPlan {
        public Choose {
            Objects.requireNonNull(problem, "problem");
            candidates = List.copyOf(candidates);
        }
    }
}
