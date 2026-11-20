package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.scriptlib.FileDecision.Verdict;
import com.vertexforge.launcher.scriptlib.LibraryPlan.Source;
import com.vertexforge.launcher.scriptlib.LibraryPlan.TargetFile;
import com.vertexforge.launcher.template.LibraryManifest;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Random;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.fail;

/** What {@link LibraryPlan} builds (VK-1691), and that building it once is enough. Pure: no disk. */
class LibraryPlanTest {

    private static final String ENGINE_VERSION = "2.0.0";

    static String h(String content) {
        return LibraryClassifierTest.hash(content);
    }

    /** One side's library: its library files (path to content), its other files and its empty folders. */
    static final class Side {
        final Map<String, String> library = new LinkedHashMap<>();
        final List<String> others = new ArrayList<>();
        final Set<String> emptyFolders = new TreeSet<>();

        Side file(String path, String content) {
            library.put(path, h(content));
            return this;
        }

        Side hashed(String path, String hash) {
            library.put(path, hash);
            return this;
        }

        Side other(String path) {
            others.add(path);
            return this;
        }

        Side folder(String path) {
            emptyFolders.add(path);
            return this;
        }

        LibrarySnapshot snapshot() {
            try {
                return LibrarySnapshot.of(library);
            } catch (LibrarySnapshot.DuplicatePathException e) {
                throw new IllegalStateException(e);
            }
        }

        List<String> files() {
            List<String> files = new ArrayList<>(library.keySet());
            files.addAll(others);
            return files;
        }

        List<String> folders() {
            Set<String> folders = new TreeSet<>();
            for (String file : files()) {
                addParents(file, folders);
            }
            for (String folder : emptyFolders) {
                folders.add(folder);
                addParents(folder, folders);
            }
            return List.copyOf(folders);
        }
    }

    static void addParents(String path, Set<String> folders) {
        for (int slash = path.indexOf('/'); slash > 0; slash = path.indexOf('/', slash + 1)) {
            folders.add(path.substring(0, slash));
        }
    }

    static LibraryBaseline.Recorded recorded(String version, Map<String, String> hashes) {
        try {
            return new LibraryBaseline.Recorded(version, LibrarySnapshot.of(hashes));
        } catch (LibrarySnapshot.DuplicatePathException e) {
            throw new IllegalStateException(e);
        }
    }

    /** The baseline as recorded for {@code side}: exactly its library files' hashes. */
    static LibraryBaseline.Recorded recordedAs(Side side) {
        return recorded("1.0.0", side.library);
    }

    static LibraryPlan.Inputs inputs(Side project, Side engine, LibraryBaseline baseline) {
        return new LibraryPlan.Inputs(project.snapshot(), project.files(), project.folders(), engine.snapshot(),
                engine.folders(), baseline, ENGINE_VERSION);
    }

    private static LibraryPlan plan(Side project, Side engine, LibraryBaseline baseline, boolean keepMine) {
        return LibraryPlan.of(inputs(project, engine, baseline), keepMine);
    }

    private static Map<String, TargetFile> byPath(LibraryPlan plan) {
        Map<String, TargetFile> files = new LinkedHashMap<>();
        plan.files().forEach(file -> files.put(file.path(), file));
        return files;
    }

    private static Verdict verdictOf(LibraryPlan plan, String key) {
        return plan.decisions().stream().filter(decision -> decision.key().equals(LibraryManifest.pathKey(key)))
                .findFirst().orElseThrow().verdict();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Sources
    // -----------------------------------------------------------------------------------------------------------------

    /** One file for every verdict of the recorded table. */
    private static final class EveryVerdict {
        final Side project = new Side()
                .file("noop.mt", "same")
                .file("update.mt", "base")
                .file("delete.mt", "base")
                .file("conflict.mt", "mine")
                .file("keepedit.mt", "mine")
                .file("keepremoved.mt", "mine")
                .file("user.mt", "mine");
        final Side engine = new Side()
                .file("noop.mt", "same")
                .file("add.mt", "new")
                .file("restore.mt", "base")
                .file("update.mt", "theirs")
                .file("conflict.mt", "theirs")
                .file("keepedit.mt", "base");
        final LibraryBaseline baseline = recorded("1.0.0", Map.of("noop.mt", h("same"), "restore.mt", h("base"),
                "update.mt", h("base"), "delete.mt", h("base"), "conflict.mt", h("base"), "keepedit.mt", h("base"),
                "keepremoved.mt", h("base")));
    }

    @Test
    void theFixtureHasEveryVerdict() {
        EveryVerdict fixture = new EveryVerdict();
        LibraryPlan plan = plan(fixture.project, fixture.engine, fixture.baseline, false);

        Map<String, Verdict> verdicts = new TreeMap<>();
        plan.decisions().forEach(decision -> verdicts.put(decision.displayPath(), decision.verdict()));
        assertEquals(Map.of("noop.mt", Verdict.NOOP, "add.mt", Verdict.ADD, "restore.mt", Verdict.RESTORE,
                "update.mt", Verdict.UPDATE, "delete.mt", Verdict.DELETE, "conflict.mt", Verdict.CONFLICT,
                "keepedit.mt", Verdict.KEEP_EDIT, "keepremoved.mt", Verdict.KEEP_REMOVED_EDITED, "user.mt",
                Verdict.USER_FILE), verdicts);
        for (Verdict verdict : Verdict.values()) {
            assertEquals(1, plan.counts().get(verdict), verdict.name());
            assertEquals(1, plan.decisions(verdict).size(), verdict.name());
        }
    }

    @Test
    void eachFileComesFromTheSideItsVerdictNames() {
        EveryVerdict fixture = new EveryVerdict();
        Map<String, TargetFile> files = byPath(plan(fixture.project, fixture.engine, fixture.baseline, false));

        assertEquals(new TargetFile("noop.mt", Source.PROJECT, "noop.mt", h("same")), files.get("noop.mt"));
        assertEquals(new TargetFile("add.mt", Source.ENGINE, "add.mt", h("new")), files.get("add.mt"));
        assertEquals(new TargetFile("restore.mt", Source.ENGINE, "restore.mt", h("base")), files.get("restore.mt"));
        assertEquals(new TargetFile("update.mt", Source.ENGINE, "update.mt", h("theirs")), files.get("update.mt"));
        assertFalse(files.containsKey("delete.mt"));
        assertEquals(new TargetFile("conflict.mt", Source.ENGINE, "conflict.mt", h("theirs")),
                files.get("conflict.mt"));
        assertEquals(new TargetFile("keepedit.mt", Source.PROJECT, "keepedit.mt", h("mine")), files.get("keepedit.mt"));
        assertEquals(new TargetFile("keepremoved.mt", Source.PROJECT, "keepremoved.mt", h("mine")),
                files.get("keepremoved.mt"));
        assertEquals(new TargetFile("user.mt", Source.PROJECT, "user.mt", h("mine")), files.get("user.mt"));
        assertEquals(8, files.size());
    }

    @Test
    void keepMineChangesOnlyTheConflicts() {
        EveryVerdict fixture = new EveryVerdict();
        Map<String, TargetFile> replaced = byPath(plan(fixture.project, fixture.engine, fixture.baseline, false));
        LibraryPlan kept = plan(fixture.project, fixture.engine, fixture.baseline, true);
        Map<String, TargetFile> files = byPath(kept);

        assertTrue(kept.keepMine());
        assertEquals(new TargetFile("conflict.mt", Source.PROJECT, "conflict.mt", h("mine")), files.get("conflict.mt"));
        files.remove("conflict.mt");
        replaced.remove("conflict.mt");
        assertEquals(replaced, files);
    }

    @Test
    void theSourceOfEveryVerdict() {
        for (boolean keepMine : new boolean[]{false, true}) {
            assertEquals(Optional.of(Source.PROJECT), LibraryPlan.sourceOf(Verdict.NOOP, keepMine));
            assertEquals(Optional.of(Source.PROJECT), LibraryPlan.sourceOf(Verdict.KEEP_EDIT, keepMine));
            assertEquals(Optional.of(Source.PROJECT), LibraryPlan.sourceOf(Verdict.KEEP_REMOVED_EDITED, keepMine));
            assertEquals(Optional.of(Source.PROJECT), LibraryPlan.sourceOf(Verdict.USER_FILE, keepMine));
            assertEquals(Optional.of(Source.ENGINE), LibraryPlan.sourceOf(Verdict.ADD, keepMine));
            assertEquals(Optional.of(Source.ENGINE), LibraryPlan.sourceOf(Verdict.RESTORE, keepMine));
            assertEquals(Optional.of(Source.ENGINE), LibraryPlan.sourceOf(Verdict.UPDATE, keepMine));
            assertEquals(Optional.empty(), LibraryPlan.sourceOf(Verdict.DELETE, keepMine));
        }
        assertEquals(Optional.of(Source.ENGINE), LibraryPlan.sourceOf(Verdict.CONFLICT, false));
        assertEquals(Optional.of(Source.PROJECT), LibraryPlan.sourceOf(Verdict.CONFLICT, true));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The new .vflib.json
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theNewRecordHoldsTheEnginesExactPathsHashesAndVersion() throws Exception {
        EveryVerdict fixture = new EveryVerdict();
        for (boolean keepMine : new boolean[]{false, true}) {
            LibraryPlan plan = plan(fixture.project, fixture.engine, fixture.baseline, keepMine);

            LibraryManifest.Contents contents = LibraryManifest.parse(plan.manifest());

            assertEquals(ENGINE_VERSION, contents.engineVersion());
            assertEquals(fixture.engine.snapshot().hashesByPath(), contents.files(),
                    "the engine's hashes, never the user's (keepMine " + keepMine + ")");
        }
    }

    @Test
    void theRecordUsesTheEnginesSpellingAndListsNoSidecar() throws Exception {
        Side project = new Side().file("ENGINE/log.mt", "a").other("ENGINE/log.mt.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recorded("1.0.0", Map.of()), false);

        assertEquals(Map.of("engine/Log.mt", h("a")), LibraryManifest.parse(plan.manifest()).files());
    }

    @Test
    void theManifestIsACopy() {
        EveryVerdict fixture = new EveryVerdict();
        LibraryPlan plan = plan(fixture.project, fixture.engine, fixture.baseline, false);

        plan.manifest()[0] = 0;

        assertEquals('{', plan.manifest()[0]);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Sidecars
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aSidecarStaysBesideAFileThatStays() {
        Side project = new Side().file("engine/Log.mt", "old").other("engine/Log.mt.vfmeta")
                .file("mine.mt", "x").other("mine.mt.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "new");

        LibraryPlan plan = plan(project, engine, recorded("1.0.0", Map.of("engine/Log.mt", h("old"))), false);

        Map<String, TargetFile> files = byPath(plan);
        assertEquals(Verdict.UPDATE, verdictOf(plan, "engine/Log.mt"));
        assertEquals(new TargetFile("engine/Log.mt.vfmeta", Source.PROJECT, "engine/Log.mt.vfmeta", null),
                files.get("engine/Log.mt.vfmeta"), "the GUID file is copied from the project, never hashed");
        assertEquals(new TargetFile("mine.mt.vfmeta", Source.PROJECT, "mine.mt.vfmeta", null),
                files.get("mine.mt.vfmeta"));
        assertEquals(List.of(), plan.droppedSidecars());
    }

    @Test
    void aSidecarGoesWithADeletedFile() {
        Side project = new Side().file("engine/Old.mt", "old").other("engine/Old.mt.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "x");

        LibraryPlan plan = plan(project, engine, recorded("1.0.0", Map.of("engine/Old.mt", h("old"))), false);

        assertEquals(Verdict.DELETE, verdictOf(plan, "engine/Old.mt"));
        assertEquals(List.of("engine/Old.mt.vfmeta"), plan.droppedSidecars());
        assertFalse(byPath(plan).containsKey("engine/Old.mt.vfmeta"));
        assertFalse(plan.upToDate());
    }

    @Test
    void aSidecarFollowsItsFileToTheEnginesSpelling() {
        Side project = new Side().file("Engine/log.mt", "a").other("Engine/log.mt.VFMETA");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        Map<String, TargetFile> files = byPath(plan);
        assertEquals(new TargetFile("engine/Log.mt", Source.PROJECT, "Engine/log.mt", h("a")),
                files.get("engine/Log.mt"));
        assertEquals(new TargetFile("engine/Log.mt.VFMETA", Source.PROJECT, "Engine/log.mt.VFMETA", null),
                files.get("engine/Log.mt.VFMETA"), "the file's new spelling, the sidecar's own extension");
        assertEquals(List.of("Engine/log.mt", "Engine/log.mt.VFMETA"), plan.renamed());
        assertFalse(plan.upToDate(), "a rename is a change");
    }

    @Test
    void aSidecarWhoseFileDiffersInCaseStillFollowsIt() {
        Side project = new Side().file("engine/Log.mt", "a").other("engine/LOG.MT.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertTrue(byPath(plan).containsKey("engine/Log.mt.vfmeta"));
        assertEquals(List.of("engine/LOG.MT.vfmeta"), plan.renamed());
    }

    @Test
    void aSidecarOfNoLibraryFileIsKept() {
        Side project = new Side().file("engine/Log.mt", "a").other("engine/Gone.mt.vfmeta").other("x.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        Map<String, TargetFile> files = byPath(plan);
        assertEquals(new TargetFile("engine/Gone.mt.vfmeta", Source.PROJECT, "engine/Gone.mt.vfmeta", null),
                files.get("engine/Gone.mt.vfmeta"));
        assertTrue(files.containsKey("x.vfmeta"));
        assertTrue(plan.upToDate(), "an orphan sidecar changes nothing");
    }

    @Test
    void anOrphanSidecarIsRespelledLikeAUserFile() {
        Side project = new Side().file("ENGINE/Log.mt", "a").other("ENGINE/Gone.mt.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertTrue(byPath(plan).containsKey("engine/Gone.mt.vfmeta"));
        assertTrue(plan.renamed().contains("ENGINE/Gone.mt.vfmeta"));
    }

    @Test
    void aSidecarOfARestoredFileStaysBesideIt() {
        Side project = new Side().other("engine/Log.mt.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(Verdict.RESTORE, verdictOf(plan, "engine/Log.mt"));
        assertTrue(byPath(plan).containsKey("engine/Log.mt.vfmeta"));
    }

    @Test
    void aSidecarOfAKeptRemovedFileIsKept() {
        Side project = new Side().file("engine/Old.mt", "mine").other("engine/Old.mt.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recorded("1.0.0", Map.of("engine/Old.mt", h("base"))), false);

        assertEquals(Verdict.KEEP_REMOVED_EDITED, verdictOf(plan, "engine/Old.mt"));
        assertTrue(byPath(plan).containsKey("engine/Old.mt.vfmeta"));
        assertEquals(List.of(), plan.droppedSidecars());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Folders
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aFolderHoldingAKeptFileIsKept() {
        Side project = new Side().file("mine/deep/Tool.mt", "x");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(List.of("engine", "mine", "mine/deep"), plan.folders());
    }

    @Test
    void everyEngineFolderIsMadeEvenAnEmptyOne() {
        Side project = new Side();
        Side engine = new Side().file("engine/oop/Behaviour.mt", "a").folder("assets/empty");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(List.of("assets", "assets/empty", "engine", "engine/oop"), plan.folders());
    }

    @Test
    void anEmptyFolderOfTheUsersIsKept() {
        Side project = new Side().file("engine/Log.mt", "a").folder("scratch/deeper").folder("engine/notes");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(List.of("engine", "engine/notes", "scratch", "scratch/deeper"), plan.folders());
        assertTrue(plan.upToDate());
    }

    @Test
    void aFolderOnlyTheDeletionsEmptiedIsDropped() {
        Side project = new Side().file("engine/Log.mt", "a").file("old/Gone.mt", "g").other("old/Gone.mt.vfmeta")
                .file("old/sub/AlsoGone.mt", "g");
        Side engine = new Side().file("engine/Log.mt", "a");
        Map<String, String> base = Map.of("engine/Log.mt", h("a"), "old/Gone.mt", h("g"), "old/sub/AlsoGone.mt",
                h("g"));

        LibraryPlan plan = plan(project, engine, recorded("1.0.0", base), false);

        assertEquals(List.of("engine"), plan.folders());
        assertFalse(plan.upToDate());
    }

    @Test
    void aFolderHoldingOnlyAnOrphanSidecarIsKept() {
        Side project = new Side().file("engine/Log.mt", "a").other("meta/x.vfmeta");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(List.of("engine", "meta"), plan.folders());
    }

    @Test
    void foldersComeParentFirst() {
        Side project = new Side().file("a-b/x.mt", "1").file("a/b/c/y.mt", "2").file("a.mt", "3");
        Side engine = new Side().file("a/z.mt", "4");

        List<String> folders = plan(project, engine, recordedAs(engine), false).folders();

        assertEquals(List.of("a", "a-b", "a/b", "a/b/c"), folders);
        for (String folder : folders) {
            int slash = folder.lastIndexOf('/');
            if (slash > 0) {
                assertTrue(folders.indexOf(folder.substring(0, slash)) < folders.indexOf(folder), folder);
            }
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Spelling
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aFileTheEngineHasTakesTheEnginesSpelling() {
        Side project = new Side().file("engine/log.mt", "a");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(Verdict.NOOP, verdictOf(plan, "engine/Log.mt"));
        assertEquals(List.of(new TargetFile("engine/Log.mt", Source.PROJECT, "engine/log.mt", h("a"))), plan.files());
        assertEquals(List.of("engine/log.mt"), plan.renamed());
        assertFalse(plan.upToDate(), "fixing the spelling is a change");
    }

    @Test
    void aUserFileInAnEngineFolderTakesTheEnginesFolderSpelling() {
        Side project = new Side().file("ENGINE/Log.mt", "a").file("ENGINE/Mine.mt", "m").folder("ENGINE/notes");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        Map<String, TargetFile> files = byPath(plan);
        assertEquals(new TargetFile("engine/Mine.mt", Source.PROJECT, "ENGINE/Mine.mt", h("m")),
                files.get("engine/Mine.mt"));
        assertEquals(List.of("ENGINE/Log.mt", "ENGINE/Mine.mt"), plan.renamed());
        assertEquals(List.of("engine", "engine/notes"), plan.folders(), "an empty user folder is respelled too");
    }

    @Test
    void aFolderOnlyTheProjectHasKeepsTheProjectsSpelling() {
        Side project = new Side().file("engine/Log.mt", "a").file("MyStuff/Sub/a.mt", "m").folder("MyStuff/Empty");
        Side engine = new Side().file("engine/Log.mt", "a");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertTrue(byPath(plan).containsKey("MyStuff/Sub/a.mt"));
        assertEquals(List.of("MyStuff", "MyStuff/Empty", "MyStuff/Sub", "engine"), plan.folders());
        assertEquals(List.of(), plan.renamed());
        assertTrue(plan.upToDate());
    }

    @Test
    void aFolderSpelledDifferentlyIsSpelledAsTheEngineDoesEvenWithoutAFileInIt() {
        // The engine's empty folder "docs" and the project's "DOCS" (also empty).
        Side project = new Side().file("engine/Log.mt", "a").folder("DOCS");
        Side engine = new Side().file("engine/Log.mt", "a").folder("docs");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(List.of("docs", "engine"), plan.folders());
        assertFalse(plan.upToDate(), "the folder is renamed");
    }

    @Test
    void everyTargetsFolderIsSpelledAsInTheFolderList() {
        Side project = new Side().file("ENGINE/OOP/behaviour.mt", "a").file("ENGINE/OOP/Mine.mt", "m")
                .other("ENGINE/OOP/Mine.mt.vfmeta").other("ENGINE/OOP/orphan.vfmeta").file("Own/x.mt", "o");
        Side engine = new Side().file("engine/oop/Behaviour.mt", "b").file("engine/Log.mt", "l");

        LibraryPlan plan = plan(project, engine, legacy(Map.of("engine/oop/Behaviour.mt", List.of(h("a")))), false);

        assertParentsListed(plan);
        assertTrue(byPath(plan).containsKey("engine/oop/Mine.mt"));
        assertTrue(byPath(plan).containsKey("engine/oop/Mine.mt.vfmeta"));
        assertTrue(byPath(plan).containsKey("engine/oop/orphan.vfmeta"));
        assertTrue(byPath(plan).containsKey("Own/x.mt"));
    }

    /** Every target's folder is in the plan's folders exactly as spelled there (the build and its check rely on it). */
    private static void assertParentsListed(LibraryPlan plan) {
        Set<String> folders = new HashSet<>(plan.folders());
        Set<String> keys = new HashSet<>();
        for (TargetFile file : plan.files()) {
            assertTrue(keys.add(LibraryManifest.pathKey(file.path())), "two targets share a key: " + file.path());
            int slash = file.path().lastIndexOf('/');
            if (slash > 0) {
                assertTrue(folders.contains(file.path().substring(0, slash)),
                        file.path() + " is not under a listed folder " + plan.folders());
            }
        }
        for (String folder : plan.folders()) {
            int slash = folder.lastIndexOf('/');
            if (slash > 0) {
                assertTrue(folders.contains(folder.substring(0, slash)), folder + "'s parent is not listed");
            }
            assertFalse(keys.contains(LibraryManifest.pathKey(folder)), folder + " is also a file");
        }
    }

    @Test
    void aFileOfOneLibraryWhereTheOtherHasAFolderIsAClash() {
        Side project = new Side().file("engine/Log.mt", "a").file("engine/oop", "a file without an extension")
                .file("docs/mine.mt", "m");
        Side engine = new Side().file("engine/Log.mt", "a").file("engine/oop/Behaviour.mt", "b").file("DOCS", "d");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(List.of("DOCS", "engine/oop"), plan.clashes());
        assertFalse(plan.upToDate());
    }

    @Test
    void aPlanWithoutClashesSaysSo() {
        EveryVerdict fixture = new EveryVerdict();

        assertEquals(List.of(), plan(fixture.project, fixture.engine, fixture.baseline, false).clashes());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Up to date
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anUnchangedRecordedProjectIsUpToDate() {
        Side engine = new Side().file("core/Array.mt", "a").file("engine/Log.mt", "l");
        Side project = new Side().file("core/Array.mt", "a").file("engine/Log.mt", "l").other(".vflib.json");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertTrue(plan.upToDate());
        assertEquals(2, plan.counts().get(Verdict.NOOP));
    }

    @Test
    void aFileOnlyTheUserChangedLeavesTheProjectUpToDate() {
        Side engine = new Side().file("engine/Log.mt", "l");
        Side project = new Side().file("engine/Log.mt", "mine").file("own.mt", "o");

        LibraryPlan plan = plan(project, engine, recordedAs(engine), false);

        assertEquals(Verdict.KEEP_EDIT, verdictOf(plan, "engine/Log.mt"));
        assertTrue(plan.upToDate());
    }

    @Test
    void aLegacyBaselineIsNeverUpToDate() {
        Side engine = new Side().file("engine/Log.mt", "l");
        Side project = new Side().file("engine/Log.mt", "l");

        LibraryPlan plan = plan(project, engine, legacy(Map.of()), false);

        assertEquals(Verdict.NOOP, verdictOf(plan, "engine/Log.mt"));
        assertFalse(plan.upToDate(), "the update at least records a baseline");
    }

    @Test
    void aRecordOfOtherHashesIsNotUpToDateEvenWhenEveryFileMatches() {
        Side engine = new Side().file("engine/Log.mt", "l");
        Side project = new Side().file("engine/Log.mt", "l");

        assertFalse(plan(project, engine, recorded("1.0.0", Map.of("engine/Log.mt", h("old"))), false).upToDate());
        assertFalse(plan(project, engine, recorded("1.0.0", Map.of("engine/Log.mt", h("l"), "x.mt", h("x"))), false)
                .upToDate());
        assertFalse(plan(project, engine, recorded("1.0.0", Map.of("ENGINE/log.mt", h("l"))), false).upToDate(),
                "the record is rewritten in the engine's spelling");
    }

    /**
     * Current behaviour, pinned: the recorded engine VERSION alone never makes a library outdated (the plan, like the
     * row's tag, compares the files). Building would only rewrite {@code engineVersion} in {@code .vflib.json}.
     */
    @Test
    void aRecordOfAnotherEngineVersionWithTheSameFilesIsUpToDate() {
        Side engine = new Side().file("engine/Log.mt", "l");
        Side project = new Side().file("engine/Log.mt", "l");

        assertTrue(plan(project, engine, recorded("0.9.0", Map.of("engine/Log.mt", h("l"))), false).upToDate());
    }

    @Test
    void anyChangeMakesThePlanOutdated() {
        Side engine = new Side().file("engine/Log.mt", "l").file("engine/New.mt", "n");
        Side project = new Side().file("engine/Log.mt", "l");
        assertFalse(plan(project, engine, recordedAs(engine), false).upToDate(), "a restore");

        Side moreEngine = new Side().file("engine/Log.mt", "l").folder("empty");
        assertFalse(plan(new Side().file("engine/Log.mt", "l"), moreEngine, recordedAs(moreEngine), false).upToDate(),
                "a new empty engine folder");

        Side conflicted = new Side().file("engine/Log.mt", "mine");
        LibraryPlan conflict = plan(conflicted, new Side().file("engine/Log.mt", "l"),
                recorded("1.0.0", Map.of("engine/Log.mt", h("base"))), true);
        assertEquals(Verdict.CONFLICT, verdictOf(conflict, "engine/Log.mt"));
        assertFalse(conflict.upToDate(), "keeping mine still records the engine's hash");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Fingerprint
    // -----------------------------------------------------------------------------------------------------------------

    private static LibraryPlan.Inputs fingerprinted(String engineContent, String recordedVersion,
                                                    List<String> extraProjectFiles, List<String> projectFolders,
                                                    List<String> engineFolders, LibraryBaseline baseline) {
        Side project = new Side().file("engine/Log.mt", "l").file("own.mt", "o");
        Side engine = new Side().file("engine/Log.mt", engineContent);
        List<String> files = new ArrayList<>(project.files());
        files.addAll(extraProjectFiles);
        LibraryBaseline base = baseline != null ? baseline : recorded(recordedVersion, Map.of("engine/Log.mt", h("l")));
        return new LibraryPlan.Inputs(project.snapshot(), files, projectFolders, engine.snapshot(), engineFolders,
                base, ENGINE_VERSION);
    }

    private static String fingerprint(LibraryPlan.Inputs inputs) {
        return LibraryPlan.fingerprint(inputs);
    }

    @Test
    void equalInputsHaveEqualFingerprintsWhateverTheListOrder() {
        LibraryPlan.Inputs first = fingerprinted("l2", "1.0.0", List.of("a.vfmeta", "b.vfmeta"),
                List.of("engine", "x"), List.of("engine", "y"), null);
        LibraryPlan.Inputs second = fingerprinted("l2", "1.0.0", List.of("b.vfmeta", "a.vfmeta"),
                List.of("x", "engine"), List.of("y", "engine"), null);

        assertEquals(fingerprint(first), fingerprint(second));
        assertEquals(64, fingerprint(first).length());
    }

    @Test
    void everyInputChangesTheFingerprint() {
        String base = fingerprint(fingerprinted("l2", "1.0.0", List.of(), List.of("engine"), List.of("engine"),
                null));
        Set<String> fingerprints = new HashSet<>();
        fingerprints.add(base);

        // The engine's content.
        fingerprints.add(fingerprint(fingerprinted("l3", "1.0.0", List.of(), List.of("engine"), List.of("engine"),
                null)));
        // A project file that is not a library file (a sidecar).
        fingerprints.add(fingerprint(fingerprinted("l2", "1.0.0", List.of("own.mt.vfmeta"), List.of("engine"),
                List.of("engine"), null)));
        // A project folder.
        fingerprints.add(fingerprint(fingerprinted("l2", "1.0.0", List.of(), List.of("engine", "empty"),
                List.of("engine"), null)));
        // An engine folder.
        fingerprints.add(fingerprint(fingerprinted("l2", "1.0.0", List.of(), List.of("engine"),
                List.of("engine", "empty"), null)));
        // The recorded version.
        fingerprints.add(fingerprint(fingerprinted("l2", "1.0.1", List.of(), List.of("engine"), List.of("engine"),
                null)));
        // A recorded hash.
        fingerprints.add(fingerprint(fingerprinted("l2", "1.0.0", List.of(), List.of("engine"), List.of("engine"),
                recorded("1.0.0", Map.of("engine/Log.mt", h("other"))))));
        // No record at all.
        fingerprints.add(fingerprint(fingerprinted("l2", "1.0.0", List.of(), List.of("engine"), List.of("engine"),
                legacy(Map.of()))));

        assertEquals(8, fingerprints.size(), "each input gives its own fingerprint");
    }

    @Test
    void theProjectsContentTheEngineVersionAndTheSpellingChangeTheFingerprint() {
        Side project = new Side().file("engine/Log.mt", "l");
        Side engine = new Side().file("engine/Log.mt", "l");
        LibraryPlan.Inputs inputs = inputs(project, engine, recordedAs(engine));
        String base = fingerprint(inputs);

        Side edited = new Side().file("engine/Log.mt", "edited");
        assertNotEquals(base, fingerprint(inputs(edited, engine, recordedAs(engine))));

        Side respelled = new Side().file("Engine/Log.mt", "l");
        assertNotEquals(base, fingerprint(inputs(respelled, engine, recordedAs(engine))));

        LibraryPlan.Inputs otherVersion = new LibraryPlan.Inputs(inputs.project(), inputs.projectFiles(),
                inputs.projectFolders(), inputs.engine(), inputs.engineFolders(), inputs.baseline(), "2.0.1");
        assertNotEquals(base, fingerprint(otherVersion));

        LegacyLibraryHashes a = LegacyLibraryHashesTest.hashes(Map.of());
        LibraryPlan.Inputs legacyA = inputs(project, engine, new LibraryBaseline.Legacy(a, "one note"));
        LibraryPlan.Inputs legacyB = inputs(project, engine, new LibraryBaseline.Legacy(a, "another note"));
        assertEquals(fingerprint(legacyA), fingerprint(legacyB), "the note is only for the user");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Building the plan once is enough (property)
    // -----------------------------------------------------------------------------------------------------------------

    private static final String[] FOLDERS = {"", "core/", "engine/", "engine/oop/"};
    private static final String[] NAMES = {"A.mt", "b.mt", "Log.mt"};
    private static final String[] CONTENTS = {"v1", "v2", "v3", "v4"};

    /** One randomly made case: both libraries, the baseline and the choice. */
    private record Case(Side project, Side engine, LibraryBaseline baseline, boolean keepMine) {
        @Override
        public String toString() {
            return "project " + project.library + " " + project.others + " " + project.emptyFolders + "\nengine "
                    + engine.library + " " + engine.emptyFolders + "\nbaseline " + describe(baseline) + "\nkeepMine "
                    + keepMine;
        }

        private static String describe(LibraryBaseline baseline) {
            return switch (baseline) {
                case LibraryBaseline.Recorded recorded -> "recorded " + recorded.files().hashesByPath();
                case LibraryBaseline.Legacy legacy -> "legacy " + legacy.hashes().files();
            };
        }
    }

    /**
     * {@code path} with each folder spelled one of two ways, the same way for every path of that side (a folder has
     * one name on disk), and the file's own name spelled either way.
     */
    private static String spell(String path, Map<String, String> folderSpelling, Random random, double upper) {
        String[] parts = path.split("/");
        StringBuilder spelled = new StringBuilder();
        StringBuilder prefix = new StringBuilder();
        for (int i = 0; i < parts.length; i++) {
            String part = parts[i];
            if (i > 0) {
                prefix.append('/');
                spelled.append('/');
            }
            prefix.append(part);
            String name;
            if (i == parts.length - 1) {
                name = random.nextDouble() < upper ? part.toUpperCase(Locale.ROOT) : part;
            } else {
                name = folderSpelling.computeIfAbsent(prefix.toString(),
                        key -> random.nextDouble() < upper ? part.toUpperCase(Locale.ROOT) : part);
            }
            spelled.append(name);
        }
        return spelled.toString();
    }

    private static Case randomCase(Random random) {
        Map<String, String> projectSpelling = new HashMap<>();
        Map<String, String> engineSpelling = new HashMap<>();
        double projectUpper = random.nextInt(3) == 0 ? 0.3 : 0;
        boolean hadLibrary = random.nextInt(8) != 0;
        boolean legacyBaseline = random.nextInt(3) == 0;

        Side project = new Side();
        Side engine = new Side();
        Map<String, String> baseline = new TreeMap<>();
        Map<String, List<String>> known = new TreeMap<>();
        for (String folder : FOLDERS) {
            for (String name : NAMES) {
                String path = folder + name;
                if (hadLibrary && random.nextInt(3) != 0) {
                    String spelled = spell(path, projectSpelling, random, projectUpper);
                    project.file(spelled, CONTENTS[random.nextInt(CONTENTS.length)]);
                    if (random.nextInt(4) == 0) {
                        // Its sidecar, in the same folder, its name spelled like the file's or not.
                        int slash = spelled.lastIndexOf('/');
                        String fileName = spelled.substring(slash + 1);
                        project.other(spelled.substring(0, slash + 1)
                                + (random.nextBoolean() ? fileName : fileName.toUpperCase(Locale.ROOT))
                                + (random.nextBoolean() ? ".vfmeta" : ".VFMETA"));
                    }
                }
                if (random.nextInt(3) != 0) {
                    engine.file(spell(path, engineSpelling, random, 0), CONTENTS[random.nextInt(CONTENTS.length)]);
                }
                if (random.nextInt(2) == 0) {
                    baseline.put(path, h(CONTENTS[random.nextInt(CONTENTS.length)]));
                }
                List<String> versions = new ArrayList<>();
                for (String content : CONTENTS) {
                    if (random.nextInt(3) == 0) {
                        versions.add(h(content));
                    }
                }
                if (!versions.isEmpty()) {
                    known.put(path, versions);
                }
            }
        }
        if (hadLibrary && random.nextInt(4) == 0) {
            project.other(spell("engine/orphan.mt", projectSpelling, random, projectUpper) + ".vfmeta");
        }
        if (hadLibrary && random.nextInt(4) == 0) {
            String scratch = spell("engine/scratch/x", projectSpelling, random, projectUpper);
            project.folder(scratch.substring(0, scratch.lastIndexOf('/')));
        }
        if (hadLibrary && random.nextInt(4) == 0) {
            project.folder("userEmpty");
        }
        if (random.nextInt(4) == 0) {
            engine.folder("assets/empty");
        }
        if (hadLibrary && random.nextInt(3) != 0) {
            project.other(LibraryManifest.FILE_NAME);
        }
        if (engine.library.isEmpty()) {
            engine.file("engine/Log.mt", "v1");
        }
        LibraryBaseline base = !hadLibrary || legacyBaseline ? legacy(known) : recorded("1.0.0", baseline);
        return new Case(project, engine, base, random.nextBoolean());
    }

    /** The project as it is after the plan was built: its targets, the new record and the plan's folders. */
    private static LibraryPlan.Inputs applied(LibraryPlan plan, LibraryPlan.Inputs before) throws Exception {
        Map<String, String> library = new TreeMap<>();
        List<String> files = new ArrayList<>();
        for (TargetFile file : plan.files()) {
            files.add(file.path());
            assertEquals(LibraryManifest.isSidecar(file.path()), file.expectedHash() == null, file.path());
            if (file.expectedHash() != null) {
                library.put(file.path(), file.expectedHash());
            }
        }
        files.add(LibraryManifest.FILE_NAME);
        LibraryBaseline.Recorded record = LibraryBaseline.Recorded.of(LibraryManifest.parse(plan.manifest()));
        return new LibraryPlan.Inputs(LibrarySnapshot.of(library), files, plan.folders(), before.engine(),
                before.engineFolders(), record, before.engineVersion());
    }

    @Test
    void aBuiltPlanLeavesNothingForTheNextUpdate() throws Exception {
        Random random = new Random(1691);
        int conflictsKept = 0;
        for (int iteration = 0; iteration < 8000; iteration++) {
            Case c = randomCase(random);
            LibraryPlan.Inputs before = inputs(c.project(), c.engine(), c.baseline());
            LibraryPlan plan = LibraryPlan.of(before, c.keepMine());
            try {
                assertParentsListed(plan);
                LibraryPlan.Inputs after = applied(plan, before);

                LibraryPlan again = LibraryPlan.of(after, false);

                if (!again.upToDate()) {
                    fail("not up to date after the update: " + again.decisions() + " renamed " + again.renamed()
                            + " dropped " + again.droppedSidecars() + " folders " + again.folders() + " vs "
                            + after.projectFolders());
                }
                for (FileDecision decision : again.decisions()) {
                    assertTrue(UNCHANGED_VERDICTS.contains(decision.verdict()), decision.toString());
                }
                assertEquals(LibraryPlan.fingerprint(after), LibraryPlan.fingerprint(applied(plan, before)),
                        "the fingerprint depends only on the inputs");

                // A file the user kept in a conflict: once the engine changes it again, it is a conflict again.
                if (c.keepMine()) {
                    for (FileDecision conflict : plan.decisions(Verdict.CONFLICT)) {
                        conflictsKept++;
                        Side changedEngine = new Side();
                        c.engine().library.forEach(changedEngine::hashed);
                        changedEngine.emptyFolders.addAll(c.engine().emptyFolders);
                        changedEngine.hashed(conflict.enginePath(), h("fresh " + iteration));
                        LibraryPlan.Inputs changed = new LibraryPlan.Inputs(after.project(), after.projectFiles(),
                                after.projectFolders(), changedEngine.snapshot(), changedEngine.folders(),
                                after.baseline(), ENGINE_VERSION);
                        LibraryPlan next = LibraryPlan.of(changed, false);
                        assertEquals(Verdict.CONFLICT, next.decisions().stream()
                                .filter(decision -> decision.key().equals(conflict.key())).findFirst().orElseThrow()
                                .verdict(), conflict.toString());
                    }
                }
            } catch (AssertionError | Exception e) {
                throw new AssertionError("iteration " + iteration + " (seed 1691):\n" + c, e);
            }
        }
        assertTrue(conflictsKept > 100, "the cases exercise kept conflicts (" + conflictsKept + ")");
    }

    /** The verdicts a library just built can have: nothing left to add, restore, update or delete. */
    private static final Set<Verdict> UNCHANGED_VERDICTS = EnumSet.of(Verdict.NOOP, Verdict.KEEP_EDIT,
            Verdict.USER_FILE);

    static LibraryBaseline.Legacy legacy(Map<String, List<String>> known) {
        return new LibraryBaseline.Legacy(LegacyLibraryHashesTest.hashes(known), "no record");
    }

    @Test
    void aPlanForAProjectWithoutALibraryAddsEverything() {
        Side engine = new Side().file("core/Array.mt", "a").file("engine/Log.mt", "l").folder("empty");

        LibraryPlan plan = LibraryPlan.of(new LibraryPlan.Inputs(LibrarySnapshot.EMPTY, List.of(), List.of(),
                engine.snapshot(), engine.folders(), legacy(Map.of()), ENGINE_VERSION), false);

        assertEquals(2, plan.counts().get(Verdict.ADD));
        assertEquals(List.of("core", "empty", "engine"), plan.folders());
        assertNull(plan.files().stream().filter(file -> file.source() == Source.PROJECT).findFirst().orElse(null));
        assertFalse(plan.upToDate());
    }
}
