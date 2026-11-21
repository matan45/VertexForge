package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineOptions;

import java.util.Objects;

/**
 * What the main window shows when there is nothing to list or nothing to open with (VK-1694). Pure, so the rules run in
 * unit tests.
 * <ul>
 *   <li>No projects: the empty screen, with Create Project and Add Existing… (and the drop hint).</li>
 *   <li>No engine that works: "Add Engine…" on that screen, or a banner with it above a list that has projects. A
 *       working {@code VERTEXFORGE_EDITOR_PATH} counts as an engine.</li>
 *   <li>Nothing is said before it is known: the empty screen waits for the list's first read and for the engines'
 *       first answer (a failed check counts as an answer, and then nothing is said about the engines), so a first
 *       run never shows a screen that is about to change.</li>
 * </ul>
 */
final class EmptyStates {

    /** Why no engine can open a project. */
    enum EngineProblem {
        /** An engine works. */
        NONE,
        /** No engine is registered and no override is set. */
        NOT_REGISTERED,
        /** Engines are registered but none of them works (and no working override is set). */
        NONE_WORKING,
        /** No engine is registered, and the override is set but does not work. */
        OVERRIDE_BROKEN,
        /** The engines are not known: not checked yet, or the check failed. */
        UNKNOWN;

        boolean noEngine() {
            return this == NOT_REGISTERED || this == NONE_WORKING || this == OVERRIDE_BROKEN;
        }
    }

    /**
     * @param showEmptyScreen whether the empty screen replaces the table
     * @param problem         why no engine can open a project, if one cannot
     * @param engineBanner    whether a banner says so (the list has projects, so the empty screen is not shown)
     */
    record EmptyState(boolean showEmptyScreen, EngineProblem problem, boolean engineBanner) {
        EmptyState {
            Objects.requireNonNull(problem, "problem");
        }
    }

    private EmptyStates() {
        // Utility class - prevent instantiation
    }

    /**
     * @param listRead       whether the list has been read (its first read answered)
     * @param enginesChecked whether the engines' first check has answered or failed
     * @param listed         how many projects the list holds
     * @param engines        the engines as last checked, or {@code null} when that is not known
     */
    static EmptyState of(boolean listRead, boolean enginesChecked, int listed, EngineOptions engines) {
        EngineProblem problem = problem(engines);
        boolean empty = listRead && enginesChecked && listed == 0;
        boolean banner = listRead && listed > 0 && problem.noEngine();
        return new EmptyState(empty, problem, banner);
    }

    static EngineProblem problem(EngineOptions engines) {
        if (engines == null) {
            return EngineProblem.UNKNOWN;
        }
        if (engines.anyUsable()) {
            return EngineProblem.NONE;
        }
        if (!engines.engines().isEmpty()) {
            return EngineProblem.NONE_WORKING;
        }
        return engines.override().isPresent() ? EngineProblem.OVERRIDE_BROKEN : EngineProblem.NOT_REGISTERED;
    }

    /** What the window says about {@code problem}; empty for an engine that works or one not known. */
    static String message(EngineProblem problem) {
        return switch (problem) {
            case NOT_REGISTERED -> "No engine is registered yet. Add the Editor.exe (or engine.json) of a VertexForge"
                    + " build to create and open projects.";
            case NONE_WORKING -> "None of the registered engines can start right now. Check them in Engines…, or add"
                    + " another one.";
            case OVERRIDE_BROKEN -> "The editor that " + EditorOverride.ENV_VAR + " names cannot start, and no engine"
                    + " is registered. Fix the variable, or add an engine.";
            case NONE, UNKNOWN -> "";
        };
    }
}
