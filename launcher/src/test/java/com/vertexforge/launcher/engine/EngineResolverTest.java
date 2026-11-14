package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Which engine opens a project (VK-1681). Pure: no files. */
class EngineResolverTest {

    private static final Path ROOT = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath();
    private static final UUID A = UUID.fromString("5b0c9d1e-7f0a-4d3b-9a51-2f6f1c0e8a42");
    private static final UUID B = UUID.fromString("c3a8e1f2-0b6d-4e5a-8f7c-1d2e3f4a5b6c");

    private static EngineHealth ok() {
        return EngineHealth.ok(null, ROOT);
    }

    private static EngineOption engine(UUID id, EngineHealth health) {
        return engine(Optional.ofNullable(id), health, false, false);
    }

    private static EngineOption engine(Optional<UUID> id, EngineHealth health, boolean bundled, boolean isDefault) {
        Path exe = ROOT.resolve(id.map(UUID::toString).orElse("unsaved")).resolve("Editor.exe");
        return new EngineOption(id, exe, exe.toString(), health, bundled, isDefault, false);
    }

    private static EngineOption override(EngineHealth health) {
        Path exe = ROOT.resolve("override/Editor.exe");
        return new EngineOption(Optional.empty(), exe, exe.toString(), health, false, false, true);
    }

    private static EngineOptions options(UUID defaultId, EngineOption override, EngineOption... engines) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.ofNullable(defaultId), StoreStatus.OK,
                Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.ofNullable(override), Arrays.asList(engines));
    }

    private static LaunchPlan.Ready ready(LaunchPlan plan) {
        return assertInstanceOf(LaunchPlan.Ready.class, plan, plan::toString);
    }

    private static LaunchPlan.Choose choose(LaunchPlan plan) {
        return assertInstanceOf(LaunchPlan.Choose.class, plan, plan::toString);
    }

    @Test
    void aWorkingOverrideWinsAndIsNeverBound() {
        EngineOption override = override(ok());
        EngineOptions options = options(A, override, engine(A, ok()));

        for (String bound : new String[]{null, A.toString(), "gone"}) {
            LaunchPlan.Ready ready = ready(EngineResolver.resolve(options, bound));
            assertEquals(override, ready.engine());
            assertEquals(Optional.empty(), ready.bindTo());
        }
    }

    @Test
    void anOverrideThatDoesNotWorkIsIgnored() {
        EngineOptions options = options(null, override(EngineHealth.broken("no such file")), engine(A, ok()));

        assertEquals(Optional.of(A), ready(EngineResolver.resolve(options, null)).engine().id());
    }

    @Test
    void aBoundProjectOpensWithItsEngineAndIsNotRebound() {
        EngineOptions options = options(B, null, engine(A, ok()), engine(B, ok()));

        LaunchPlan.Ready ready = ready(EngineResolver.resolve(options, A.toString()));

        assertEquals(Optional.of(A), ready.engine().id());
        assertEquals(Optional.empty(), ready.bindTo());
    }

    @Test
    void aBrokenBoundEngineAsksAndOffersToRebindByDefault() {
        EngineOptions options = options(null, null, engine(A, EngineHealth.broken("Editor.exe was not found")),
                engine(B, ok()));

        LaunchPlan.Choose choose = choose(EngineResolver.resolve(options, A.toString()));

        assertTrue(choose.rememberByDefault());
        assertTrue(choose.problem().contains("broken") && choose.problem().contains("Editor.exe was not found"),
                choose.problem());
        assertEquals(List.of(Optional.of(B)), choose.candidates().stream().map(EngineOption::id).toList());
    }

    @Test
    void anUnknownBoundEngineAsksAndOffersToRebind() {
        EngineOptions options = options(null, null, engine(B, ok()));

        for (String bound : new String[]{UUID.randomUUID().toString(), "not-a-uuid"}) {
            LaunchPlan.Choose choose = choose(EngineResolver.resolve(options, bound));
            assertTrue(choose.rememberByDefault());
            assertTrue(choose.problem().contains("no longer registered"), choose.problem());
        }
    }

    @Test
    void aMissingBoundEngineAsksWithoutRebindingByDefault() {
        EngineOptions options = options(null, null, engine(A, EngineHealth.missing("Drive Z:\\ is not available")),
                engine(B, ok()));

        LaunchPlan.Choose choose = choose(EngineResolver.resolve(options, A.toString()));

        assertFalse(choose.rememberByDefault(), "a missing engine may come back");
        assertTrue(choose.problem().contains("cannot be reached"), choose.problem());
    }

    @Test
    void anUnboundProjectBindsToTheChosenDefault() {
        EngineOptions options = options(B, null, engine(A, ok()), engine(B, ok()));

        LaunchPlan.Ready ready = ready(EngineResolver.resolve(options, null));

        assertEquals(Optional.of(B), ready.engine().id());
        assertEquals(Optional.of(B), ready.bindTo());
    }

    @Test
    void aChosenDefaultThatDoesNotWorkAsksInsteadOfPickingAnother() {
        EngineOption bundled = engine(Optional.of(A), ok(), true, false);
        EngineOptions options = options(B, null, bundled, engine(B, EngineHealth.broken("gone")));

        LaunchPlan.Choose choose = choose(EngineResolver.resolve(options, null));

        assertTrue(choose.problem().contains("default engine"), choose.problem());
        assertEquals(List.of(bundled), choose.candidates());
    }

    /** Review finding: as for a bound engine, a default that may come back is not replaced for good by default. */
    @Test
    void aMissingDefaultAsksWithoutRebindingByDefault() {
        EngineOptions options = options(B, null, engine(A, ok()),
                engine(B, EngineHealth.missing("No response after 3 s")));

        LaunchPlan.Choose choose = choose(EngineResolver.resolve(options, null));

        assertFalse(choose.rememberByDefault());
        assertTrue(choose.problem().contains("default engine") && choose.problem().contains("missing"),
                choose.problem());
    }

    @Test
    void withoutADefaultTheBundledEngineIsUsed() {
        EngineOption bundled = engine(Optional.of(B), ok(), true, false);
        EngineOptions options = options(null, null, engine(A, ok()), bundled);

        LaunchPlan.Ready ready = ready(EngineResolver.resolve(options, null));

        assertEquals(bundled, ready.engine());
        assertEquals(Optional.of(B), ready.bindTo());
    }

    @Test
    void anUnsavedBundledEngineOpensButIsNotBound() {
        EngineOption bundled = engine(Optional.empty(), ok(), true, false);

        LaunchPlan.Ready ready = ready(EngineResolver.resolve(options(null, null, bundled), null));

        assertEquals(bundled, ready.engine());
        assertEquals(Optional.empty(), ready.bindTo());
    }

    @Test
    void theOnlyWorkingEngineIsUsedWhenThereIsNoDefaultOrBundledOne() {
        EngineOptions options = options(null, null, engine(A, EngineHealth.broken("gone")), engine(B, ok()));

        LaunchPlan.Ready ready = ready(EngineResolver.resolve(options, null));

        assertEquals(Optional.of(B), ready.bindTo());
    }

    @Test
    void severalEnginesAndNoDefaultAsk() {
        LaunchPlan.Choose choose = choose(EngineResolver.resolve(options(null, null, engine(A, ok()),
                engine(B, ok())), null));

        assertEquals(2, choose.candidates().size());
        assertTrue(choose.rememberByDefault());
    }

    @Test
    void noWorkingEngineAsksForOneToBeAdded() {
        LaunchPlan.Choose none = choose(EngineResolver.resolve(options(null, null), null));
        LaunchPlan.Choose broken = choose(EngineResolver.resolve(
                options(null, null, engine(A, EngineHealth.broken("gone"))), A.toString()));

        assertTrue(none.candidates().isEmpty());
        assertEquals(EngineResolver.NO_ENGINE, none.problem());
        assertTrue(broken.problem().endsWith(EngineResolver.NO_ENGINE), broken.problem());
    }

    @Test
    void anUnknownDefaultIsIgnored() {
        EngineOptions options = options(UUID.randomUUID(), null, engine(A, ok()));

        assertEquals(Optional.of(A), ready(EngineResolver.resolve(options, null)).engine().id());
    }

    // -----------------------------------------------------------------------------------------------------------
    // EngineLabels
    // -----------------------------------------------------------------------------------------------------------

    @Test
    void theEngineColumnIsNeverBlank() throws InvalidManifestException {
        EngineManifest manifest = EngineManifest.parse(EngineManifestTest.DEV_MANIFEST);
        EngineOptions options = options(null, null,
                engine(A, EngineHealth.ok(manifest, ROOT)),
                engine(B, EngineHealth.broken("gone", manifest)));
        UUID unversioned = UUID.randomUUID();
        EngineOptions plain = options(null, null, engine(unversioned, ok()),
                engine(A, EngineHealth.missing("Drive Z:\\ is not available")));

        assertEquals(EngineLabels.DEFAULT, EngineLabels.forProject(null, options));
        assertEquals(EngineLabels.DEFAULT, EngineLabels.forProject(null, null));
        assertEquals(EngineLabels.CHECKING, EngineLabels.forProject(A.toString(), null));
        assertEquals("1.0.0 Development", EngineLabels.forProject(A.toString(), options));
        assertEquals("1.0.0 Development (Broken)", EngineLabels.forProject(B.toString(), options));
        assertEquals(EngineLabels.UNKNOWN, EngineLabels.forProject(UUID.randomUUID().toString(), options));
        assertEquals(EngineOption.UNKNOWN_VERSION, EngineLabels.forProject(unversioned.toString(), plain));
        assertEquals("Missing", EngineLabels.forProject(A.toString(), plain));
    }

    @Test
    void anEnginesLabelNamesWhatMakesItSpecial() {
        assertEquals(EngineOption.UNKNOWN_VERSION, engine(A, ok()).label());
        assertEquals(EngineOption.UNKNOWN_VERSION + " (bundled, default)",
                engine(Optional.of(A), ok(), true, true).label());
        assertEquals(EngineOption.UNKNOWN_VERSION + " (" + EditorOverride.ENV_VAR + ")", override(ok()).label());
    }
}
