package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettings.AfterLaunch;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The Settings dialog without its controls (VK-1694). */
class SettingsFormTest {

    private static final Path ROOT = Path.of("C:/engines").toAbsolutePath();
    private static final UUID A = UUID.fromString("00000000-0000-0000-0000-00000000000a");
    private static final UUID B = UUID.fromString("00000000-0000-0000-0000-00000000000b");
    private static final UUID C = UUID.fromString("00000000-0000-0000-0000-00000000000c");
    private static final EngineManifest MANIFEST = manifest();

    private static EngineManifest manifest() {
        try {
            return EngineManifest.parse("""
                    {"manifestVersion": 1, "engineVersion": "1.0.0", "configuration": "Development",
                     "editorExe": "Editor.exe", "workingDirectory": "."}
                    """);
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    private static EngineOption engine(UUID id, boolean ok, boolean isDefault) {
        Path exe = ROOT.resolve(id == null ? "unsaved" : id.toString()).resolve("Editor.exe");
        EngineHealth health = ok ? EngineHealth.ok(MANIFEST, ROOT) : EngineHealth.missing("gone");
        return new EngineOption(Optional.ofNullable(id), exe, exe.toString(), health, id == null, isDefault, false);
    }

    private static EngineOptions engines(StoreStatus status, EngineOption... engines) {
        Optional<UUID> defaultId = Arrays.stream(engines).filter(EngineOption::isDefault)
                .map(EngineOption::id).flatMap(Optional::stream).findFirst();
        EngineRegistryView registry = new EngineRegistryView(List.of(), defaultId, status, Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.empty(), Arrays.asList(engines));
    }

    private static SettingsForm form(LauncherSettings current) {
        return new SettingsForm(current, true, false, 3, 0, null);
    }

    @Test
    void theFormStartsFromTheCurrentSettings() {
        Path location = ROOT.resolve("Projects");
        LauncherSettings current = new LauncherSettings(12, AfterLaunch.CLOSE, false, Optional.of(location), 7);

        SettingsForm form = form(current);

        assertEquals(location.toString(), form.locationTextProperty().get());
        assertEquals("12", form.limitTextProperty().get());
        assertEquals("7", form.retentionTextProperty().get());
        assertEquals(AfterLaunch.CLOSE, form.afterLaunchProperty().get());
        assertFalse(form.showEditorConsoleProperty().get());
        assertTrue(form.saveEnabledProperty().get());
        assertEquals(current, form.result().orElseThrow().settings());
    }

    @Test
    void everyChoiceEndsUpInTheResult() {
        SettingsForm form = form(LauncherSettings.DEFAULTS);
        Path location = ROOT.resolve("New Projects");

        form.locationTextProperty().set("  " + location + "  ");
        form.limitTextProperty().set(" 30 ");
        form.retentionTextProperty().set("5");
        form.afterLaunchProperty().set(AfterLaunch.STAY);
        form.showEditorConsoleProperty().set(false);

        assertEquals(new LauncherSettings(30, AfterLaunch.STAY, false, Optional.of(location), 5),
                form.result().orElseThrow().settings());
    }

    @ParameterizedTest
    @ValueSource(strings = {"relative\\folder", "C:no-separator", "\\no-drive", "bad\u0000name"})
    void aLocationThatIsNotAnAbsolutePathBlocksSave(String text) {
        SettingsForm form = form(LauncherSettings.DEFAULTS);

        form.locationTextProperty().set(text);

        assertFalse(form.locationProblemProperty().get().isEmpty());
        assertFalse(form.saveEnabledProperty().get());
        assertEquals(Optional.empty(), form.result());
    }

    @Test
    void aBlankLocationMeansNone() {
        SettingsForm form = form(new LauncherSettings(50, AfterLaunch.MINIMIZE, true,
                Optional.of(ROOT.resolve("Old")), 20));

        form.locationTextProperty().set("   ");

        assertEquals("", form.locationProblemProperty().get());
        assertEquals(Optional.empty(), form.result().orElseThrow().settings().defaultProjectLocation());
    }

    @ParameterizedTest
    @ValueSource(strings = {"0", "501", "-1", "2.5", "ten", "", "9999999999"})
    void aCapOutsideItsRangeBlocksSave(String text) {
        SettingsForm form = form(LauncherSettings.DEFAULTS);

        form.limitTextProperty().set(text);

        assertTrue(form.limitProblemProperty().get().contains("1 to 500"), form.limitProblemProperty().get());
        assertFalse(form.saveEnabledProperty().get());
    }

    @ParameterizedTest
    @ValueSource(strings = {"0", "201", "x", ""})
    void aRetentionOutsideItsRangeBlocksSave(String text) {
        SettingsForm form = form(LauncherSettings.DEFAULTS);

        form.retentionTextProperty().set(text);

        assertTrue(form.retentionProblemProperty().get().contains("1 to 200"),
                form.retentionProblemProperty().get());
        assertFalse(form.saveEnabledProperty().get());
    }

    @Test
    void theRangesIncludeTheirEnds() {
        SettingsForm form = form(LauncherSettings.DEFAULTS);

        form.limitTextProperty().set("500");
        form.retentionTextProperty().set("200");
        assertTrue(form.saveEnabledProperty().get());

        form.limitTextProperty().set("1");
        form.retentionTextProperty().set("1");
        assertTrue(form.saveEnabledProperty().get());
    }

    @Test
    void theRemovedCountSkipsPinnedProjects() {
        assertEquals(5, SettingsForm.projectsRemoved(10, 3, 5));
        assertEquals(7, SettingsForm.projectsRemoved(10, 3, 2));
        assertEquals(0, SettingsForm.projectsRemoved(10, 10, 2), "pinned projects are never removed");
        assertEquals(0, SettingsForm.projectsRemoved(4, 0, 50));
        assertEquals(0, SettingsForm.projectsRemoved(0, 0, 1));
    }

    @Test
    void loweringTheCapSaysHowManyGoAndRemovesThemOnSave() {
        SettingsForm form = new SettingsForm(LauncherSettings.DEFAULTS, true, false, 10, 3, null);
        assertEquals("", form.trimNoticeProperty().get());

        form.limitTextProperty().set("5");

        assertTrue(form.trimNoticeProperty().get().contains("5 oldest unpinned projects"),
                form.trimNoticeProperty().get());
        assertEquals(5, form.result().orElseThrow().projectsToRemove());

        form.limitTextProperty().set("9");
        assertTrue(form.trimNoticeProperty().get().contains("oldest unpinned project "),
                form.trimNoticeProperty().get());
    }

    @Test
    void raisingTheCapRemovesNothing() {
        SettingsForm form = new SettingsForm(new LauncherSettings(10), true, false, 10, 0, null);

        form.limitTextProperty().set("40");

        assertEquals("", form.trimNoticeProperty().get());
        assertEquals(0, form.result().orElseThrow().projectsToRemove());
    }

    @Test
    void aReadOnlyListSaysTheTrimWaits() {
        SettingsForm form = new SettingsForm(LauncherSettings.DEFAULTS, true, true, 10, 0, null);

        form.limitTextProperty().set("5");

        assertTrue(form.trimNoticeProperty().get().contains("read-only"), form.trimNoticeProperty().get());
        assertEquals(0, form.result().orElseThrow().projectsToRemove());
        assertEquals(5, form.result().orElseThrow().settings().recentProjectsLimit(), "the cap is still saved");
    }

    @Test
    void anUnwritableSettingsFileDisablesSave() {
        SettingsForm form = new SettingsForm(LauncherSettings.DEFAULTS, false, false, 0, 0, null);

        assertFalse(form.canSave());
        assertFalse(form.saveEnabledProperty().get());
        assertEquals(Optional.empty(), form.result());
    }

    @Test
    void theEngineChoicesAreNoneAndTheWorkingRegisteredEnginesPlusABrokenDefault() {
        EngineOptions options = engines(StoreStatus.OK, engine(A, true, false), engine(B, false, false),
                engine(C, false, true), engine(null, true, false));

        SettingsForm form = new SettingsForm(LauncherSettings.DEFAULTS, true, false, 0, 0, options);

        List<Optional<UUID>> ids = form.engineItems().stream().map(SettingsForm.EngineItem::engineId).toList();
        assertEquals(List.of(Optional.empty(), Optional.of(A), Optional.of(C)), ids);
        assertTrue(form.engineItems().get(2).label().contains("not working"), form.engineItems().get(2).label());
        assertEquals(Optional.of(C), form.defaultEngineProperty().get().engineId(), "the current default is chosen");
        assertTrue(form.engineChoiceEnabled());
    }

    @Test
    void theEngineChoiceIsDisabledWhileUnknownOrReadOnly() {
        SettingsForm unknown = new SettingsForm(LauncherSettings.DEFAULTS, true, false, 0, 0, null);
        SettingsForm readOnly = new SettingsForm(LauncherSettings.DEFAULTS, true, false, 0, 0,
                engines(StoreStatus.NEWER_READ_ONLY, engine(A, true, true)));

        assertFalse(unknown.engineChoiceEnabled());
        assertEquals(List.of(SettingsForm.NO_DEFAULT_ENGINE), unknown.engineItems().stream()
                .map(SettingsForm.EngineItem::label).toList());
        assertFalse(readOnly.engineChoiceEnabled());
        readOnly.defaultEngineProperty().set(readOnly.engineItems().getFirst());
        assertEquals(Optional.empty(), readOnly.result().orElseThrow().defaultEngine(),
                "nothing is written to a read-only engines file");
    }

    @Test
    void anUnchangedDefaultEngineIsNotWritten() {
        SettingsForm form = new SettingsForm(LauncherSettings.DEFAULTS, true, false, 0, 0,
                engines(StoreStatus.OK, engine(A, true, true), engine(B, true, false)));

        assertEquals(Optional.empty(), form.result().orElseThrow().defaultEngine());
    }

    @Test
    void choosingAnotherEngineOrNoneChangesTheDefault() {
        SettingsForm form = new SettingsForm(LauncherSettings.DEFAULTS, true, false, 0, 0,
                engines(StoreStatus.OK, engine(A, true, true), engine(B, true, false)));

        form.defaultEngineProperty().set(form.engineItems().get(2));
        assertEquals(Optional.of(new SettingsForm.DefaultEngineChange(Optional.of(B))),
                form.result().orElseThrow().defaultEngine());

        form.defaultEngineProperty().set(form.engineItems().getFirst());
        assertEquals(Optional.of(new SettingsForm.DefaultEngineChange(Optional.empty())),
                form.result().orElseThrow().defaultEngine(), "None clears the default");
    }

    @Test
    void withoutADefaultNoneIsChosen() {
        SettingsForm form = new SettingsForm(LauncherSettings.DEFAULTS, true, false, 0, 0,
                engines(StoreStatus.OK, engine(A, true, false)));

        assertEquals(Optional.empty(), form.defaultEngineProperty().get().engineId());
        assertEquals(Optional.empty(), form.result().orElseThrow().defaultEngine());
    }
}
