package com.vertexforge.launcher.engine;

import java.util.List;
import java.util.Locale;
import java.util.Optional;
import java.util.UUID;

/**
 * Decides which engine opens a project (VK-1681). Pure: it only reads the {@link EngineOptions} it is given, so it
 * runs on the FX thread and in plain unit tests.
 * <ol>
 *   <li>A working {@code VERTEXFORGE_EDITOR_PATH} override opens every project, and is never bound.</li>
 *   <li>A bound project opens with its engine when that is OK. A Missing engine (it may come back) asks, without
 *       offering to rebind by default; a Broken or unknown one asks, rebinding by default.</li>
 *   <li>An unbound project is bound to the engine it first opens with: the chosen default when it is OK (a chosen
 *       default that does not work asks instead of silently picking another), else the running launcher's bundled
 *       engine when it is OK, else the only engine that works. Otherwise the user is asked.</li>
 * </ol>
 */
public final class EngineResolver {

    static final String NO_ENGINE = "No working engine is registered. Add an engine (its Editor.exe or "
            + EngineManifest.FILE_NAME + "), or set " + EditorOverride.ENV_VAR + ".";

    private EngineResolver() {
        // Utility class - prevent instantiation
    }

    /** @param boundEngineId the project's stored {@code engineId}, or {@code null} for an unbound project */
    public static LaunchPlan resolve(EngineOptions options, String boundEngineId) {
        Optional<EngineOption> override = options.activeOverride();
        if (override.isPresent()) {
            return new LaunchPlan.Ready(override.get(), Optional.empty());
        }

        List<EngineOption> usable = options.usable();
        if (boundEngineId != null) {
            Optional<EngineOption> bound = options.find(boundEngineId);
            if (bound.isEmpty()) {
                return choose("The engine this project was opened with is no longer registered.", usable, true);
            }
            EngineOption engine = bound.get();
            return switch (engine.health().kind()) {
                case OK -> new LaunchPlan.Ready(engine, Optional.empty());
                case MISSING -> choose("The engine this project uses (" + describe(engine) + ") cannot be reached"
                        + " right now: " + engine.health().reason() + ". Open it with another engine, or cancel and"
                        + " try again later.", usable, false);
                case BROKEN -> choose("The engine this project uses (" + describe(engine) + ") is broken: "
                        + engine.health().reason() + ".", usable, true);
            };
        }

        Optional<UUID> defaultId = options.registry().defaultEngineId();
        Optional<EngineOption> chosenDefault = defaultId.flatMap(options::find);
        if (chosenDefault.isPresent()) {
            EngineOption engine = chosenDefault.get();
            if (engine.isOk()) {
                return new LaunchPlan.Ready(engine, engine.id());
            }
            // As for a bound engine: a default that may come back is not replaced for good by default.
            return choose("The default engine (" + describe(engine) + ") is "
                    + engine.health().label().toLowerCase(Locale.ROOT) + ": " + engine.health().reason() + ".",
                    usable, engine.health().kind() != EngineHealth.Kind.MISSING);
        }

        Optional<EngineOption> bundled = options.bundled().filter(EngineOption::isOk);
        if (bundled.isPresent()) {
            return new LaunchPlan.Ready(bundled.get(), bundled.get().id());
        }
        if (usable.size() == 1) {
            return new LaunchPlan.Ready(usable.getFirst(), usable.getFirst().id());
        }
        return choose(usable.isEmpty() ? "" : "Choose the engine to open this project with.", usable, true);
    }

    /** With no engine to choose from, the problem is always that there is none. */
    private static LaunchPlan choose(String problem, List<EngineOption> usable, boolean rememberByDefault) {
        String text = usable.isEmpty() ? (problem.isEmpty() ? NO_ENGINE : problem + " " + NO_ENGINE) : problem;
        return new LaunchPlan.Choose(text, usable, rememberByDefault);
    }

    private static String describe(EngineOption engine) {
        return engine.versionText() + ", " + engine.pathText();
    }
}
