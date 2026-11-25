package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.scriptlib.FileDecision.Verdict;
import com.vertexforge.launcher.template.LibraryManifest;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Both truth tables of {@link LibraryClassifier} (VK-1691), row by row, and what {@code classify} adds to them. */
class LibraryClassifierTest {

    private static final Optional<String> NONE = Optional.empty();
    private static final String P = hash("project's");
    private static final String E = hash("engine's");
    private static final String B = hash("baseline's");

    static String hash(String content) {
        return LibraryManifest.hash(content.getBytes(StandardCharsets.UTF_8));
    }

    private static Optional<String> of(String hash) {
        return Optional.of(hash);
    }

    private static LibrarySnapshot snapshot(Map<String, String> files) throws Exception {
        return LibrarySnapshot.of(files);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The recorded-baseline table
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void recordedAFileOnlyTheEngineHasIsAdded() {
        assertEquals(Verdict.ADD, LibraryClassifier.recorded(NONE, of(E), NONE));
    }

    @Test
    void recordedAFileTheUserDeletedIsRestoredWhateverTheEngineDidToIt() {
        assertEquals(Verdict.RESTORE, LibraryClassifier.recorded(NONE, of(E), of(B)), "the engine changed it");
        assertEquals(Verdict.RESTORE, LibraryClassifier.recorded(NONE, of(E), of(E)), "the engine kept it");
    }

    @Test
    void recordedAFileGoneOnBothSidesGetsNoDecision() throws Exception {
        LibraryBaseline.Recorded baseline = new LibraryBaseline.Recorded("1.0.0",
                snapshot(Map.of("gone.mt", B, "kept.mt", E)));

        List<FileDecision> decisions = LibraryClassifier.classify(snapshot(Map.of("kept.mt", E)),
                snapshot(Map.of("kept.mt", E)), baseline);

        assertEquals(List.of(new FileDecision("KEPT.MT", Verdict.NOOP, "kept.mt", "kept.mt")), decisions);
    }

    @Test
    void recordedAFileOfTheUsersOwnIsKept() {
        assertEquals(Verdict.USER_FILE, LibraryClassifier.recorded(of(P), NONE, NONE));
    }

    @Test
    void recordedAFileTheEngineRemovedIsDeletedOnlyWhenTheUserNeverChangedIt() {
        assertEquals(Verdict.DELETE, LibraryClassifier.recorded(of(B), NONE, of(B)));
        assertEquals(Verdict.KEEP_REMOVED_EDITED, LibraryClassifier.recorded(of(P), NONE, of(B)));
    }

    @Test
    void recordedAProjectCopyEqualToTheEnginesIsLeftAloneWhateverTheBaseline() {
        assertEquals(Verdict.NOOP, LibraryClassifier.recorded(of(E), of(E), NONE));
        assertEquals(Verdict.NOOP, LibraryClassifier.recorded(of(E), of(E), of(E)));
        assertEquals(Verdict.NOOP, LibraryClassifier.recorded(of(E), of(E), of(B)), "both made the same change");
    }

    @Test
    void recordedAFileOnlyTheEngineChangedIsUpdated() {
        assertEquals(Verdict.UPDATE, LibraryClassifier.recorded(of(B), of(E), of(B)));
    }

    @Test
    void recordedAFileOnlyTheUserChangedIsKept() {
        assertEquals(Verdict.KEEP_EDIT, LibraryClassifier.recorded(of(P), of(B), of(B)));
    }

    @Test
    void recordedAFileBothChangedIsAConflict() {
        assertEquals(Verdict.CONFLICT, LibraryClassifier.recorded(of(P), of(E), of(B)));
    }

    @Test
    void recordedAFileBothAddedDifferentlyIsAConflict() {
        assertEquals(Verdict.CONFLICT, LibraryClassifier.recorded(of(P), of(E), NONE));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The table without a record
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void legacyAMissingFileIsAddedEvenWhenTheLauncherKnowsIt() {
        assertEquals(Verdict.ADD, LibraryClassifier.legacy(NONE, of(E), Set.of()));
        assertEquals(Verdict.ADD, LibraryClassifier.legacy(NONE, of(E), Set.of(B, E)));
    }

    @Test
    void legacyAProjectCopyEqualToTheEnginesIsLeftAloneEvenWhenUnknown() {
        assertEquals(Verdict.NOOP, LibraryClassifier.legacy(of(E), of(E), Set.of()));
        assertEquals(Verdict.NOOP, LibraryClassifier.legacy(of(E), of(E), Set.of(B)));
    }

    @Test
    void legacyAKnownOlderVersionIsUpdated() {
        assertEquals(Verdict.UPDATE, LibraryClassifier.legacy(of(B), of(E), Set.of(B)));
        assertEquals(Verdict.UPDATE, LibraryClassifier.legacy(of(B), of(E), Set.of(P, B, E)));
    }

    @Test
    void legacyAnUnknownVersionIsAConflict() {
        assertEquals(Verdict.CONFLICT, LibraryClassifier.legacy(of(P), of(E), Set.of()));
        assertEquals(Verdict.CONFLICT, LibraryClassifier.legacy(of(P), of(E), Set.of(B, E)),
                "knowing the engine's own version among others does not make the user's copy known");
    }

    @Test
    void legacyAPathWhoseOnlyKnownVersionIsTheEnginesWasChangedOnlyByTheUser() {
        assertEquals(Verdict.KEEP_EDIT, LibraryClassifier.legacy(of(P), of(E), Set.of(E)));
        assertEquals(Verdict.CONFLICT, LibraryClassifier.legacy(of(P), of(E), Set.of(E, B)),
                "an older version the user may have started from: the engine did change it");
        assertEquals(Verdict.CONFLICT, LibraryClassifier.legacy(of(P), of(E), Set.of(B)));
    }

    @Test
    void legacyAFileTheEngineRemovedIsDeletedOnlyWhenKnown() {
        assertEquals(Verdict.DELETE, LibraryClassifier.legacy(of(B), NONE, Set.of(B)));
        assertEquals(Verdict.USER_FILE, LibraryClassifier.legacy(of(P), NONE, Set.of(B)));
        assertEquals(Verdict.USER_FILE, LibraryClassifier.legacy(of(P), NONE, Set.of()));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // classify
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aDifferenceOnlyInLineEndingsIsNoEdit() throws Exception {
        String crlf = LibraryManifest.hash("public class Log {\r\n}\r\n".getBytes(StandardCharsets.UTF_8));
        String lf = LibraryManifest.hash("public class Log {\n}\n".getBytes(StandardCharsets.UTF_8));
        assertEquals(crlf, lf);

        List<FileDecision> recorded = LibraryClassifier.classify(snapshot(Map.of("engine/Log.mt", crlf)),
                snapshot(Map.of("engine/Log.mt", lf)), new LibraryBaseline.Recorded("1.0.0", LibrarySnapshot.EMPTY));
        List<FileDecision> legacy = LibraryClassifier.classify(snapshot(Map.of("engine/Log.mt", crlf)),
                snapshot(Map.of("engine/Log.mt", lf)), legacyBaseline(Map.of()));

        assertEquals(Verdict.NOOP, recorded.getFirst().verdict());
        assertEquals(Verdict.NOOP, legacy.getFirst().verdict());
    }

    @Test
    void pathsAreMatchedIgnoringCaseOnEverySide() throws Exception {
        LibraryBaseline.Recorded baseline = new LibraryBaseline.Recorded("1.0.0",
                snapshot(Map.of("ENGINE/log.MT", B)));

        List<FileDecision> decisions = LibraryClassifier.classify(snapshot(Map.of("Engine/LOG.mt", B)),
                snapshot(Map.of("engine/Log.mt", E)), baseline);

        assertEquals(1, decisions.size());
        FileDecision decision = decisions.getFirst();
        assertEquals("ENGINE/LOG.MT", decision.key());
        assertEquals(Verdict.UPDATE, decision.verdict(), "the baseline's spelling does not matter either");
        assertEquals("Engine/LOG.mt", decision.projectPath());
        assertEquals("engine/Log.mt", decision.enginePath());
        assertEquals("engine/Log.mt", decision.displayPath(), "shown as the engine spells it");
    }

    @Test
    void legacyKnownVersionsAreLookedUpIgnoringCase() throws Exception {
        LibraryBaseline.Legacy baseline = legacyBaseline(Map.of("Engine/Log.mt", List.of(B)));

        List<FileDecision> decisions = LibraryClassifier.classify(snapshot(Map.of("engine/LOG.mt", B)),
                snapshot(Map.of("ENGINE/log.mt", E)), baseline);

        assertEquals(Verdict.UPDATE, decisions.getFirst().verdict());
    }

    @Test
    void thereIsOneDecisionPerFileOfEitherSideSortedByKey() throws Exception {
        LibrarySnapshot project = snapshot(Map.of("b.mt", P, "_first.mt", P, "c/x.mt", E));
        LibrarySnapshot engine = snapshot(Map.of("a.mt", E, "B.mt", E, "c/x.mt", E));

        List<FileDecision> decisions = LibraryClassifier.classify(project, engine,
                new LibraryBaseline.Recorded("1.0.0", LibrarySnapshot.EMPTY));

        // Sorted by key (upper case): 'A' (0x41) comes before '_' (0x5F), which would not hold for "a".
        assertEquals(List.of("A.MT", "B.MT", "C/X.MT", "_FIRST.MT"),
                decisions.stream().map(FileDecision::key).toList());
        assertEquals(List.of(Verdict.ADD, Verdict.CONFLICT, Verdict.NOOP, Verdict.USER_FILE),
                decisions.stream().map(FileDecision::verdict).toList());
        assertNull(decisions.getFirst().projectPath());
        assertNull(decisions.get(3).enginePath());
        assertEquals("_first.mt", decisions.get(3).displayPath(), "the project's spelling when the engine has none");
    }

    @Test
    void anEmptyProjectGetsEveryEngineFile() throws Exception {
        List<FileDecision> decisions = LibraryClassifier.classify(LibrarySnapshot.EMPTY,
                snapshot(Map.of("a.mt", E, "b/c.mt", E)), legacyBaseline(Map.of()));

        assertEquals(List.of(Verdict.ADD, Verdict.ADD), decisions.stream().map(FileDecision::verdict).toList());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The verdicts, snapshots and decisions themselves
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void exactlyTheVerdictsThatKeepSomethingOfTheUsersSaySo() {
        for (Verdict verdict : Verdict.values()) {
            boolean expected = switch (verdict) {
                case CONFLICT, KEEP_EDIT, KEEP_REMOVED_EDITED, USER_FILE -> true;
                case NOOP, ADD, RESTORE, UPDATE, DELETE -> false;
            };
            assertEquals(expected, verdict.userChanged(), verdict.name());
        }
    }

    @Test
    void aDecisionIsAboutAFileThatExistsSomewhere() {
        assertThrows(IllegalArgumentException.class, () -> new FileDecision("A", Verdict.ADD, null, null));
        assertEquals("p", new FileDecision("A", Verdict.USER_FILE, "p", null).displayPath());
        assertEquals("e", new FileDecision("A", Verdict.CONFLICT, "p", "e").displayPath());
    }

    @Test
    void aSnapshotRefusesTwoPathsWindowsSeesAsOne() {
        LibrarySnapshot.DuplicatePathException e = assertThrows(LibrarySnapshot.DuplicatePathException.class,
                () -> LibrarySnapshot.of(Map.of("engine/Log.mt", P, "Engine/log.mt", E)));

        assertTrue(e.getMessage().contains("differ only in case"), e.getMessage());
    }

    @Test
    void aSnapshotTakesOnlyLibraryFiles() {
        assertThrows(IllegalArgumentException.class, () -> LibrarySnapshot.of(Map.of("a.mt.vfmeta", P)));
        assertThrows(IllegalArgumentException.class, () -> LibrarySnapshot.of(Map.of(".vflib.json", P)));
    }

    @Test
    void aSnapshotListsItsFilesByExactPathSorted() throws Exception {
        LibrarySnapshot snapshot = snapshot(Map.of("b.mt", P, "A/x.mt", E));

        assertEquals(List.of("A/x.mt", "b.mt"), List.copyOf(snapshot.hashesByPath().keySet()));
        assertEquals(Optional.of(new LibrarySnapshot.Entry("A/x.mt", E)), snapshot.get("A/X.MT"));
        assertEquals(Optional.empty(), snapshot.get("A/x.mt"), "looked up by key only");
        assertEquals(2, snapshot.size());
        assertFalse(snapshot.isEmpty());
        assertTrue(LibrarySnapshot.EMPTY.isEmpty());
    }

    static LibraryBaseline.Legacy legacyBaseline(Map<String, List<String>> known) {
        return new LibraryBaseline.Legacy(LegacyLibraryHashesTest.hashes(known), "test");
    }
}
