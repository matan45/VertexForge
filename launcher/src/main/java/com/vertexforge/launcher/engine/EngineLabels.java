package com.vertexforge.launcher.engine;

import java.util.Optional;

/** The text of the project table's Engine column (VK-1681). Never blank. */
public final class EngineLabels {

    /** An unbound project: it opens with the default engine and is bound to it then. */
    public static final String DEFAULT = "Default";
    /** The engines have not been checked yet. */
    public static final String CHECKING = "Checking…";
    /** The project is bound to an engine that is no longer registered. */
    public static final String UNKNOWN = "Unknown engine";

    private EngineLabels() {
        // Utility class - prevent instantiation
    }

    /**
     * @param engineId the project's stored {@code engineId}, or {@code null}
     * @param options  the engines as last checked, or {@code null} before the first check answered
     */
    public static String forProject(String engineId, EngineOptions options) {
        if (engineId == null) {
            return DEFAULT;
        }
        if (options == null) {
            return CHECKING;
        }
        Optional<EngineOption> engine = options.find(engineId);
        if (engine.isEmpty()) {
            return UNKNOWN;
        }
        EngineHealth health = engine.get().health();
        if (health.isOk()) {
            return engine.get().versionText();
        }
        return health.manifest() == null
                ? health.label()
                : health.manifest().describe() + " (" + health.label() + ")";
    }
}
