package com.vertexforge.launcher.env;

import java.nio.file.Path;
import java.util.Locale;
import java.util.function.BooleanSupplier;

/**
 * Moves a project folder to the Windows Recycle Bin (VK-1695), and never deletes it permanently.
 * <p>
 * {@code java.awt.Desktop.moveToTrash} is not used: OpenJDK passes {@code FOF_NOCONFIRMATION}, so when Windows cannot
 * recycle a folder (a network or removable drive, a Recycle Bin that is turned off or too small) it deletes the folder
 * permanently without asking. {@link #system()} instead drives the shell's {@code IFileOperation} with a progress sink
 * that stops the operation as soon as Windows says it would not recycle, the way Electron's {@code shell.trashItem}
 * does. A folder that cannot be recycled is left where it is, and the user is told why.
 * <p>
 * Both operations block for as long as Windows takes, and so must never run on the JavaFX application thread. Neither
 * ever throws: every failure is an answer.
 */
public interface RecycleBin {

    /** The answer of {@link #unavailable()} and of a machine that cannot recycle at all. */
    String UNAVAILABLE = "The Recycle Bin is not available on this system";

    /** Whether this machine can recycle at all (Windows, the shell's IFileOperation found). */
    boolean available();

    /**
     * Dry run: asks Windows whether it WOULD recycle {@code folder}, deleting nothing. Blocks; never throws.
     * <p>
     * Windows only decides once it is about to delete, so this starts a real recycle and stops it at that moment.
     * {@code cancelRequested} is polled when the operation starts and whenever Windows reports progress while it
     * prepares, from a thread of Windows' choosing, so it must be thread-safe (a volatile flag); once it answers
     * {@code true} the check ends as {@link RecycleCheck.Cancelled}, unless Windows had already answered.
     */
    RecycleCheck check(Path folder, BooleanSupplier cancelRequested);

    /**
     * Moves {@code folder} to the Recycle Bin, or refuses when Windows would delete it permanently. Blocks; never
     * throws. A {@link RecycleResult.Refused} or {@link RecycleResult.NotRecyclable} answer means nothing was
     * deleted; a {@link RecycleResult.Failed} one may follow a partly finished move, and its message says where to
     * look.
     */
    RecycleResult recycle(Path folder);

    /** Recycles nothing: {@link #available()} is false, a check is {@link RecycleCheck.Unknown}, a recycle fails. */
    static RecycleBin unavailable() {
        return new RecycleBin() {
            @Override
            public boolean available() {
                return false;
            }

            @Override
            public RecycleCheck check(Path folder, BooleanSupplier cancelRequested) {
                return new RecycleCheck.Unknown(UNAVAILABLE);
            }

            @Override
            public RecycleResult recycle(Path folder) {
                return new RecycleResult.Failed(UNAVAILABLE);
            }
        };
    }

    /** The shell's {@code IFileOperation} on Windows, looked up on first use; {@link #unavailable()} elsewhere. */
    static RecycleBin system() {
        boolean windows = System.getProperty("os.name", "").toLowerCase(Locale.ROOT).startsWith("windows");
        return windows ? new WindowsRecycleBin() : unavailable();
    }
}
