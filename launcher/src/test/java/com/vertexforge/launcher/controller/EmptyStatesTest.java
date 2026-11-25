package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.controller.EmptyStates.EmptyState;
import com.vertexforge.launcher.controller.EmptyStates.EngineProblem;
import com.vertexforge.launcher.engine.EditorOverride;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The first-run and empty screens (VK-1694). */
class EmptyStatesTest {

    private static final Path ROOT = Path.of("C:/engines").toAbsolutePath();

    private static EngineOption engine(boolean ok, boolean override) {
        Path exe = ROOT.resolve(override ? "override" : "registered").resolve("Editor.exe");
        EngineHealth health;
        try {
            health = ok ? EngineHealth.ok(EngineManifest.parse("""
                    {"manifestVersion": 1, "engineVersion": "1.0.0", "configuration": "Development",
                     "editorExe": "Editor.exe", "workingDirectory": "."}
                    """), ROOT) : EngineHealth.missing("gone");
        } catch (Exception e) {
            throw new AssertionError(e);
        }
        return new EngineOption(override ? Optional.empty() : Optional.of(UUID.randomUUID()), exe, exe.toString(),
                health, false, false, override);
    }

    private static EngineOptions engines(EngineOption override, EngineOption... registered) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.empty(), StoreStatus.OK,
                Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.ofNullable(override), List.of(registered));
    }

    @Test
    void nothingIsShownBeforeTheListIsRead() {
        EmptyState state = EmptyStates.of(false, true, 0, engines(null));

        assertFalse(state.showEmptyScreen(), "the table says Loading… instead");
        assertFalse(state.engineBanner());
    }

    @Test
    void theEmptyScreenWaitsForTheEnginesFirstAnswer() {
        assertFalse(EmptyStates.of(true, false, 0, null).showEmptyScreen(),
                "it would otherwise change as soon as the engines answer");

        // A failed check is an answer too: the screen shows, saying nothing about the engines.
        EmptyState failed = EmptyStates.of(true, true, 0, null);
        assertTrue(failed.showEmptyScreen());
        assertEquals(EngineProblem.UNKNOWN, failed.problem());
        assertFalse(failed.problem().noEngine());
        assertEquals("", EmptyStates.message(failed.problem()));
    }

    @Test
    void anEmptyListShowsTheEmptyScreen() {
        EmptyState state = EmptyStates.of(true, true, 0, engines(null, engine(true, false)));

        assertTrue(state.showEmptyScreen());
        assertEquals(EngineProblem.NONE, state.problem());
        assertFalse(state.engineBanner());
    }

    /** The story's AC: a first run with no engine shows the empty screen with "Add Engine…". */
    @Test
    void aFirstRunWithoutAnEngineOffersToAddOne() {
        EmptyState state = EmptyStates.of(true, true, 0, engines(null));

        assertTrue(state.showEmptyScreen());
        assertEquals(EngineProblem.NOT_REGISTERED, state.problem());
        assertTrue(state.problem().noEngine());
        assertTrue(EmptyStates.message(state.problem()).contains("No engine is registered"));
    }

    @Test
    void aWorkingOverrideCountsAsAnEngine() {
        assertEquals(EngineProblem.NONE, EmptyStates.problem(engines(engine(true, true))));
    }

    @Test
    void aBrokenOverrideWithNothingRegisteredSaysSo() {
        EngineProblem problem = EmptyStates.problem(engines(engine(false, true)));

        assertEquals(EngineProblem.OVERRIDE_BROKEN, problem);
        assertTrue(problem.noEngine());
        assertTrue(EmptyStates.message(problem).contains(EditorOverride.ENV_VAR), EmptyStates.message(problem));
    }

    @Test
    void registeredEnginesThatDoNotWorkAreSaidSo() {
        EmptyState state = EmptyStates.of(true, true, 0, engines(engine(false, true), engine(false, false)));

        assertEquals(EngineProblem.NONE_WORKING, state.problem());
        assertTrue(EmptyStates.message(state.problem()).contains("None of the registered engines"));
    }

    @Test
    void projectsWithoutAnEngineGetABannerInstead() {
        EmptyState state = EmptyStates.of(true, true, 3, engines(null));

        assertFalse(state.showEmptyScreen());
        assertTrue(state.engineBanner());
        assertFalse(EmptyStates.of(true, true, 3, engines(null, engine(true, false))).engineBanner());
        assertFalse(EmptyStates.of(true, false, 3, null).engineBanner(), "not before the engines' first check");
    }
}
