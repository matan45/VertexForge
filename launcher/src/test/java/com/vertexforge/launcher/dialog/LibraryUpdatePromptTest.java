package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.scriptlib.ImportCheck;
import com.vertexforge.launcher.scriptlib.LegacyLibraryHashes;
import com.vertexforge.launcher.scriptlib.LibraryBaseline;
import com.vertexforge.launcher.scriptlib.LibraryPlan;
import com.vertexforge.launcher.scriptlib.LibrarySnapshot;
import com.vertexforge.launcher.service.ScriptLibraryUpdater;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** What the script-library update shows before it changes anything (VK-1691), built without the toolkit. */
class LibraryUpdatePromptTest {

    private static final Path LIBRARY = Path.of("C:\\projects\\Game\\assets\\scripts\\lib");
    private static final Path BACKUPS = Path.of("C:\\projects\\Game\\.vertexforge\\backups");
    private static final String SAME = "1".repeat(64);
    private static final String BASE = "2".repeat(64);
    private static final String ENGINE = "3".repeat(64);
    private static final String MINE = "4".repeat(64);

    private static LibraryBaseline.Recorded recorded(Map<String, String> files) throws Exception {
        return new LibraryBaseline.Recorded("1.0.0", LibrarySnapshot.of(files));
    }

    private static ScriptLibraryUpdater.Preview preview(Map<String, String> project, List<String> projectFiles,
                                                        List<String> projectFolders, Map<String, String> engine,
                                                        LibraryBaseline baseline, boolean hadLibrary,
                                                        List<ImportCheck.BrokenImport> broken, List<String> notes)
            throws Exception {
        LibraryPlan.Inputs inputs = new LibraryPlan.Inputs(LibrarySnapshot.of(project), projectFiles, projectFolders,
                LibrarySnapshot.of(engine), List.of("engine"), baseline, "1.1.0");
        ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(Path.of("Game.vfproj"), "assets",
                Path.of("engine-lib"), "1.1.0");
        return new ScriptLibraryUpdater.Preview(request, LIBRARY, hadLibrary, baseline, LibraryPlan.of(inputs, false),
                broken, "fp", BACKUPS, notes);
    }

    /** One file of every verdict, plus a rename. */
    private static ScriptLibraryUpdater.Preview everything() throws Exception {
        Map<String, String> project = new LinkedHashMap<>();
        project.put("engine/noop.mt", SAME);
        project.put("engine/Other.mt", SAME);
        project.put("engine/update.mt", BASE);
        project.put("engine/delete.mt", BASE);
        project.put("engine/conflict.mt", MINE);
        project.put("engine/keep.mt", MINE);
        project.put("engine/removed.mt", MINE);
        project.put("engine/user.mt", MINE);
        Map<String, String> engine = new LinkedHashMap<>();
        engine.put("engine/noop.mt", SAME);
        engine.put("engine/OTHER.mt", SAME);
        engine.put("engine/add.mt", ENGINE);
        engine.put("engine/restore.mt", BASE);
        engine.put("engine/update.mt", ENGINE);
        engine.put("engine/conflict.mt", ENGINE);
        engine.put("engine/keep.mt", BASE);
        Map<String, String> base = new LinkedHashMap<>();
        for (String path : List.of("engine/noop.mt", "engine/restore.mt", "engine/update.mt", "engine/delete.mt",
                "engine/conflict.mt", "engine/keep.mt", "engine/removed.mt")) {
            base.put(path, BASE);
        }
        return preview(project, List.copyOf(project.keySet()), List.of("engine"), engine, recorded(base), true,
                List.of(), List.of());
    }

    private static LibraryUpdatePrompt.Group group(LibraryUpdatePrompt prompt, String labelStart) {
        return prompt.groups().stream().filter(group -> group.label().startsWith(labelStart)).findFirst()
                .orElseThrow(() -> new AssertionError("no group " + labelStart + " in " + prompt.groups()));
    }

    @Test
    void everyFileIsListedUnderWhatHappensToItWithTheCountInTheLabel() throws Exception {
        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "1.1.0 Development", everything(), null);

        assertEquals(List.of("Updated (1)", "Added (1)", "Restored (1)", "Removed (1)",
                "Changed by you and by the engine (1)", "Changed by you (kept) (1)",
                "Removed by the engine, changed by you (kept) (1)", "Your own files (kept) (1)", "Renamed"),
                prompt.groups().stream().map(LibraryUpdatePrompt.Group::label).toList());
        assertEquals(List.of("engine/update.mt"), group(prompt, "Updated").files());
        assertEquals(List.of("engine/add.mt"), group(prompt, "Added").files());
        assertEquals(List.of("engine/restore.mt"), group(prompt, "Restored").files());
        assertEquals(List.of("engine/delete.mt"), group(prompt, "Removed (").files());
        assertEquals(List.of("engine/conflict.mt"), group(prompt, "Changed by you and").files());
        assertEquals(List.of("engine/keep.mt"), group(prompt, "Changed by you (kept)").files());
        assertEquals(List.of("engine/removed.mt"), group(prompt, "Removed by the engine").files());
        assertEquals(List.of("engine/user.mt"), group(prompt, "Your own").files());
        assertEquals(List.of("engine/Other.mt"), group(prompt, "Renamed").files(), "by the old spelling");
        assertEquals(1, prompt.conflicts());
        assertTrue(group(prompt, "Changed by you and").explanation().contains(LibraryUpdatePrompt.KEEP_MINE));
    }

    @Test
    void theTitleHeaderAndIntroNameTheProjectTheEngineAndWhatIsAlreadyDone() throws Exception {
        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "1.1.0 Development", everything(), null);

        assertEquals(LibraryUpdatePrompt.TITLE, prompt.title());
        assertEquals("Update the script library of \"Game\" to 1.1.0 Development?", prompt.header());
        assertEquals("2 files are already up to date.", prompt.intro());
    }

    @Test
    void onlyGroupsWithFilesAreShownAndOneFileIsSingular() throws Exception {
        ScriptLibraryUpdater.Preview preview = preview(Map.of("engine/Log.mt", BASE, "engine/Keep.mt", SAME),
                List.of("engine/Log.mt", "engine/Keep.mt"), List.of("engine"),
                Map.of("engine/Log.mt", ENGINE, "engine/Keep.mt", SAME),
                recorded(Map.of("engine/Log.mt", BASE, "engine/Keep.mt", SAME)), true, List.of(), List.of());

        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "engine", preview, null);

        assertEquals(List.of("Updated (1)"), prompt.groups().stream().map(LibraryUpdatePrompt.Group::label).toList());
        assertEquals("1 file is already up to date.", prompt.intro());
        assertEquals(0, prompt.conflicts());
    }

    @Test
    void whyTheQuestionIsAskedAgainFollowsTheIntro() throws Exception {
        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "engine", everything(),
                "The library changed since the last look.");

        assertEquals("2 files are already up to date. The library changed since the last look.", prompt.intro());
    }

    @Test
    void aBrokenImportIsAWarningWithItsSuggestion() throws Exception {
        ScriptLibraryUpdater.Preview preview = preview(Map.of("engine/Log.mt", SAME), List.of("engine/Log.mt"),
                List.of("engine"), Map.of("engine/Log.mt", SAME), recorded(Map.of("engine/Log.mt", SAME)), true,
                List.of(new ImportCheck.BrokenImport("game/Main.mt", "engine/Entity.mt", "engine/ecs/Entity.mt"),
                        new ImportCheck.BrokenImport("lib/mine/Tool.mt", "../engine/Gone.mt", null)),
                List.of());

        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "engine", preview, null);

        assertEquals(List.of("game/Main.mt imports \"engine/Entity.mt\", which will no longer be found (the library "
                        + "now has engine/ecs/Entity.mt).",
                "lib/mine/Tool.mt imports \"../engine/Gone.mt\", which will no longer be found."), prompt.warnings());
    }

    @Test
    void theNotesNameTheBackupAndWhatIsNotTouched() throws Exception {
        ScriptLibraryUpdater.Preview preview = preview(Map.of("engine/Log.mt", BASE), List.of("engine/Log.mt"),
                List.of("engine"), Map.of("engine/Log.mt", ENGINE), recorded(Map.of("engine/Log.mt", BASE)), true,
                List.of(), List.of("A leftover folder."));

        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "engine", preview, null);

        assertEquals(List.of("The library is backed up into " + BACKUPS + " first.",
                "Only " + LIBRARY + " changes; the game's scripts are not touched.", "A leftover folder."),
                prompt.notes());
        assertTrue(prompt.warnings().isEmpty());
    }

    @Test
    void aLegacyBaselineExplainsItselfFirst() throws Exception {
        LegacyLibraryHashes known = LegacyLibraryHashes.parse("{\"formatVersion\": 1, \"hashAlgorithm\": \"SHA-256\", "
                + "\"lineEndings\": \"lf\", \"through\": \"" + "0".repeat(40) + "\", \"files\": {}}");
        LibraryBaseline.Legacy legacy = new LibraryBaseline.Legacy(known, "The project was made before tracking.");
        ScriptLibraryUpdater.Preview preview = preview(Map.of("engine/Log.mt", SAME), List.of("engine/Log.mt"),
                List.of("engine"), Map.of("engine/Log.mt", SAME), legacy, true, List.of(), List.of());

        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "engine", preview, null);

        assertEquals("The project was made before tracking.", prompt.notes().getFirst());
        assertTrue(prompt.groups().isEmpty(), "nothing but the record changes");
    }

    @Test
    void aProjectWithoutALibrarySaysSoAndHasNoBackup() throws Exception {
        LegacyLibraryHashes known = LegacyLibraryHashes.parse("{\"formatVersion\": 1, \"hashAlgorithm\": \"SHA-256\", "
                + "\"lineEndings\": \"lf\", \"through\": \"" + "0".repeat(40) + "\", \"files\": {}}");
        ScriptLibraryUpdater.Preview preview = preview(Map.of(), List.of(), List.of(), Map.of("engine/Log.mt", SAME),
                new LibraryBaseline.Legacy(known, "No library yet."), false, List.of(), List.of());

        LibraryUpdatePrompt prompt = LibraryUpdatePrompt.of("Game", "engine", preview, null);

        assertEquals("The project has no script library yet: the engine's is added.", prompt.intro());
        assertEquals(List.of("Added (1)"), prompt.groups().stream().map(LibraryUpdatePrompt.Group::label).toList());
        assertFalse(prompt.notes().stream().anyMatch(note -> note.contains("backed up")), prompt.notes().toString());
    }
}
