package com.vertexforge.launcher.scriptlib;

import java.util.Objects;

/**
 * Works out how to finish or undo a script-library update that was interrupted (VK-1691), from its
 * {@linkplain UpdateJournal journal} and what is on disk now. Pure; one step at a time: the caller does the step,
 * looks at the disk again and asks again, until the answer is {@link Step.Done} or {@link Step.NeedsAttention}.
 * <p>
 * The update writes the journal, builds the staging folder (its {@code .vflib.json} last), renames the library to the
 * previous folder, renames the staging folder to the library, deletes the previous folder, then the journal. So:
 * <pre>
 *  library  staging     previous  step
 *  yes      yes         -         delete the staging folder (the old library stays)
 *  -        complete    yes       finish the swap: staging becomes the library
 *  -        incomplete  yes       roll back: previous becomes the library again
 *  -        -           yes       roll back
 *  yes      -           yes       delete the previous folder (only while the backup is there)
 *  yes      -           -         delete the journal
 * </pre>
 * A project that had no library before the update has no previous folder; a staging folder still there then means the
 * single rename never happened, and the update is undone.
 * Only the folders the journal names are ever touched, never the library itself, and anything that does not fit is
 * left for the user ({@link Step.NeedsAttention}).
 */
public final class RecoveryPlanner {

    /** What a name is on disk. */
    public enum Kind {
        MISSING,
        /** A real folder: not a link, nor a junction to another folder. */
        FOLDER,
        /** Anything else: a file, a link. */
        OTHER
    }

    /** The journal as found. */
    public sealed interface JournalState {
        /** No journal: no update was interrupted. */
        record Absent() implements JournalState {
        }

        record Valid(UpdateJournal journal) implements JournalState {
            public Valid {
                Objects.requireNonNull(journal, "journal");
            }
        }

        /** A journal that cannot be read; {@code newerFormat} when a newer launcher wrote it. */
        record Invalid(String reason, boolean newerFormat) implements JournalState {
            public Invalid {
                Objects.requireNonNull(reason, "reason");
            }
        }
    }

    /**
     * The disk as it is now.
     *
     * @param library          the library folder
     * @param staging          the journal's staging folder ({@link Kind#MISSING} without a valid journal)
     * @param stagingComplete  whether the staging folder's {@code .vflib.json} has the journal's hash
     * @param previous         the journal's previous folder ({@link Kind#MISSING} without a valid journal)
     * @param backupPresent    whether the journal's backup zip is there ({@code false} when it names none)
     * @param strayUpdateFolders whether there are folders that look like update folders beside the library
     */
    public record Disk(Kind library, Kind staging, boolean stagingComplete, Kind previous, boolean backupPresent,
                       boolean strayUpdateFolders) {
        public Disk {
            Objects.requireNonNull(library, "library");
            Objects.requireNonNull(staging, "staging");
            Objects.requireNonNull(previous, "previous");
        }
    }

    /** The next thing to do. */
    public sealed interface Step {
        /** Nothing was interrupted, or everything is finished. */
        record Done() implements Step {
        }

        /** Delete the staging folder: the old library stays. */
        record DeleteStaging() implements Step {
        }

        /** Rename the staging folder to the library's name. */
        record FinishSwap() implements Step {
        }

        /** Rename the previous folder back to the library's name. */
        record RollBack() implements Step {
        }

        /** Delete the previous folder (the old library, which is in the backup). */
        record DeletePrevious() implements Step {
        }

        /** Delete the journal: the library is in order. */
        record DeleteJournal() implements Step {
        }

        /** Nothing is touched: the user must look. {@code reason} says what is wrong. */
        record NeedsAttention(String reason) implements Step {
            public NeedsAttention {
                Objects.requireNonNull(reason, "reason");
            }
        }
    }

    private RecoveryPlanner() {
        // Utility class - prevent instantiation
    }

    /** The next step. */
    public static Step next(JournalState journal, Disk disk) {
        Objects.requireNonNull(journal, "journal");
        Objects.requireNonNull(disk, "disk");
        return switch (journal) {
            case JournalState.Absent ignored -> new Step.Done();
            case JournalState.Invalid invalid -> {
                if (invalid.newerFormat()) {
                    yield new Step.NeedsAttention("The note of an interrupted update was written by a newer launcher ("
                            + invalid.reason() + "). Finish the update with that launcher.");
                }
                // A journal is written before any folder of the update exists; with none around, nothing was begun.
                yield disk.strayUpdateFolders()
                        ? new Step.NeedsAttention("The note of an interrupted update cannot be read ("
                        + invalid.reason() + "), and folders of an update are beside the library.")
                        : new Step.DeleteJournal();
            }
            case JournalState.Valid valid -> next(valid.journal(), disk);
        };
    }

    private static Step next(UpdateJournal journal, Disk disk) {
        if (disk.library() == Kind.OTHER || disk.staging() == Kind.OTHER || disk.previous() == Kind.OTHER) {
            return new Step.NeedsAttention("The library, " + journal.staging() + " or " + journal.previous()
                    + " is not a plain folder.");
        }
        boolean library = disk.library() == Kind.FOLDER;
        boolean staging = disk.staging() == Kind.FOLDER;
        boolean previous = disk.previous() == Kind.FOLDER;

        if (library) {
            if (staging && previous) {
                return new Step.NeedsAttention("The library, " + journal.staging() + " and " + journal.previous()
                        + " are all there.");
            }
            if (staging) {
                return new Step.DeleteStaging();
            }
            if (previous) {
                if (!journal.hadLibrary()) {
                    return new Step.NeedsAttention(journal.previous() + " is there, although the project had no "
                            + "library before the update.");
                }
                return disk.backupPresent()
                        ? new Step.DeletePrevious()
                        : new Step.NeedsAttention(journal.previous() + " holds the library as it was before the "
                        + "update, and its backup is gone. Keep or delete it yourself.");
            }
            return new Step.DeleteJournal();
        }

        if (previous) {
            if (!journal.hadLibrary()) {
                return new Step.NeedsAttention(journal.previous() + " is there, although the project had no library "
                        + "before the update.");
            }
            return staging && disk.stagingComplete() ? new Step.FinishSwap() : new Step.RollBack();
        }
        if (staging) {
            if (!journal.hadLibrary()) {
                // Without an old library there is no previous folder: the swap is one rename, so a staging folder
                // still there means it never happened. The update is undone (the project had no library before).
                return new Step.DeleteStaging();
            }
            return disk.stagingComplete()
                    ? new Step.FinishSwap()
                    : new Step.NeedsAttention("The library is gone and the new one in " + journal.staging()
                    + " is incomplete. Restore the library from its backup.");
        }
        return journal.hadLibrary()
                ? new Step.NeedsAttention("The library folder is gone. Restore it from its backup.")
                : new Step.DeleteJournal();
    }
}
