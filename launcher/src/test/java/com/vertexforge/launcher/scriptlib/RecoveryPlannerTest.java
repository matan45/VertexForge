package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.scriptlib.RecoveryPlanner.Disk;
import com.vertexforge.launcher.scriptlib.RecoveryPlanner.JournalState;
import com.vertexforge.launcher.scriptlib.RecoveryPlanner.Kind;
import com.vertexforge.launcher.scriptlib.RecoveryPlanner.Step;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Every row of {@link RecoveryPlanner}'s table (VK-1691), and what it does with journals it cannot use. */
class RecoveryPlannerTest {

    private static final Kind M = Kind.MISSING;
    private static final Kind F = Kind.FOLDER;
    private static final Kind O = Kind.OTHER;

    static UpdateJournal journal(boolean hadLibrary) {
        return new UpdateJournal(UpdateJournal.FORMAT_VERSION, "1a2b3c4d", "lib", "lib.new-1a2b3c4d",
                "lib.old-1a2b3c4d", hadLibrary, "0".repeat(64),
                hadLibrary ? ".vertexforge/backups/scripts-lib-1.0.0-20260102-030405Z.zip" : null, "1.1.0",
                "2026-01-02T03:04:05Z");
    }

    private static Step next(boolean hadLibrary, Kind library, Kind staging, boolean complete, Kind previous,
                             boolean backup) {
        return RecoveryPlanner.next(new JournalState.Valid(journal(hadLibrary)),
                new Disk(library, staging, complete, previous, backup, false));
    }

    private static Step next(Kind library, Kind staging, boolean complete, Kind previous) {
        return next(true, library, staging, complete, previous, true);
    }

    private static void assertAttention(Step step, String fragment) {
        Step.NeedsAttention attention = assertInstanceOf(Step.NeedsAttention.class, step, step::toString);
        assertTrue(attention.reason().contains(fragment), attention.reason());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The table (the project had a library)
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aStagingFolderBesideTheOldLibraryIsDeleted() {
        assertEquals(new Step.DeleteStaging(), next(F, F, true, M));
        assertEquals(new Step.DeleteStaging(), next(F, F, false, M), "complete or not, the old library stays");
    }

    @Test
    void aCompleteStagingFolderWithTheOldLibraryAsideFinishesTheSwap() {
        assertEquals(new Step.FinishSwap(), next(M, F, true, F));
    }

    @Test
    void anIncompleteStagingFolderWithTheOldLibraryAsideRollsBack() {
        assertEquals(new Step.RollBack(), next(M, F, false, F));
    }

    @Test
    void onlyThePreviousFolderRollsBack() {
        assertEquals(new Step.RollBack(), next(M, M, false, F));
    }

    @Test
    void thePreviousFolderBesideTheNewLibraryIsDeletedOnlyWhileTheBackupIsThere() {
        assertEquals(new Step.DeletePrevious(), next(true, F, M, false, F, true));
        assertAttention(next(true, F, M, false, F, false), "its backup is gone");
    }

    @Test
    void aLibraryAloneMeansOnlyTheJournalIsLeft() {
        assertEquals(new Step.DeleteJournal(), next(F, M, false, M));
    }

    @Test
    void allThreeFoldersNeedTheUser() {
        assertAttention(next(F, F, true, F), "are all there");
    }

    @Test
    void aCompleteStagingFolderAloneBecomesTheLibrary() {
        assertEquals(new Step.FinishSwap(), next(M, F, true, M));
    }

    @Test
    void anIncompleteStagingFolderAloneNeedsTheBackup() {
        assertAttention(next(M, F, false, M), "is incomplete. Restore the library from its backup");
    }

    @Test
    void noFolderAtAllNeedsTheBackup() {
        assertAttention(next(M, M, false, M), "The library folder is gone");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // A project that had no library
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void withoutAnOldLibraryAStagingFolderIsAnUndoneSwap() {
        assertEquals(new Step.DeleteStaging(), next(false, M, F, true, M, false));
        assertEquals(new Step.DeleteStaging(), next(false, M, F, false, M, false));
        assertEquals(new Step.DeleteStaging(), next(false, F, F, true, M, false));
    }

    @Test
    void withoutAnOldLibraryAPreviousFolderIsNeverOurs() {
        assertAttention(next(false, F, M, false, F, true), "although the project had no library");
        assertAttention(next(false, M, M, false, F, true), "although the project had no library");
        assertAttention(next(false, M, F, true, F, true), "although the project had no library");
    }

    @Test
    void withoutAnOldLibraryTheJournalAloneIsDeleted() {
        assertEquals(new Step.DeleteJournal(), next(false, M, M, false, M, false));
        assertEquals(new Step.DeleteJournal(), next(false, F, M, false, M, false), "the swap happened");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // What is not a plain folder
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anythingThatIsNotAPlainFolderNeedsTheUser() {
        for (boolean hadLibrary : new boolean[]{true, false}) {
            assertAttention(next(hadLibrary, O, M, false, M, true), "is not a plain folder");
            assertAttention(next(hadLibrary, F, O, false, M, true), "is not a plain folder");
            assertAttention(next(hadLibrary, F, M, false, O, true), "is not a plain folder");
            assertAttention(next(hadLibrary, M, O, true, F, true), "is not a plain folder");
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // The journal
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void noJournalMeansNothingToDoWhateverIsOnDisk() {
        assertEquals(new Step.Done(), RecoveryPlanner.next(new JournalState.Absent(),
                new Disk(M, M, false, M, false, true)));
        assertEquals(new Step.Done(), RecoveryPlanner.next(new JournalState.Absent(),
                new Disk(O, F, true, F, true, true)));
    }

    @Test
    void anUnreadableJournalWithoutUpdateFoldersIsDeleted() {
        assertEquals(new Step.DeleteJournal(), RecoveryPlanner.next(new JournalState.Invalid("broken", false),
                new Disk(F, M, false, M, false, false)));
        assertEquals(new Step.DeleteJournal(), RecoveryPlanner.next(new JournalState.Invalid("broken", false),
                new Disk(M, M, false, M, false, false)), "it was written before any folder of the update existed");
    }

    @Test
    void anUnreadableJournalBesideUpdateFoldersNeedsTheUser() {
        assertAttention(RecoveryPlanner.next(new JournalState.Invalid("broken", false),
                new Disk(F, M, false, M, false, true)), "cannot be read (broken), and folders of an update");
    }

    @Test
    void aNewerJournalIsLeftForTheNewerLauncher() {
        for (boolean stray : new boolean[]{true, false}) {
            assertAttention(RecoveryPlanner.next(new JournalState.Invalid("format 2", true),
                    new Disk(F, M, false, M, false, stray)), "written by a newer launcher (format 2)");
        }
    }

    @Test
    void aValidJournalIgnoresTheStrayFlag() {
        assertEquals(new Step.DeleteJournal(), RecoveryPlanner.next(new JournalState.Valid(journal(true)),
                new Disk(F, M, false, M, false, true)));
    }

    @Test
    void theMessagesNameTheJournalsFolders() {
        assertAttention(next(F, F, true, F), "lib.new-1a2b3c4d and lib.old-1a2b3c4d");
        assertAttention(next(true, F, M, false, F, false), "lib.old-1a2b3c4d holds the library");
    }
}
