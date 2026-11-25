package com.vertexforge.launcher.env;

import java.util.Locale;

/**
 * The HRESULTs the shell's copy engine answers a recycle with, and how {@link WindowsRecycleBin} turns what one
 * operation saw ({@link Observation}) into an answer for the user (VK-1695). Pure: everything here can be tested
 * without Windows.
 * <p>
 * The codes are from the Windows SDK 10.0.26100 headers ({@code sherrors.h}, {@code winerror.h}). The rules never
 * claim more than Windows reported: a sink that saw Windows about to delete instead of recycle always wins, and only a
 * reported Recycle Bin item counts as recycled.
 */
final class ShellResults {

    static final int S_OK = 0;
    static final int S_FALSE = 1;
    static final int E_ABORT = 0x80004004;
    static final int E_NOINTERFACE = 0x80004002;
    static final int E_POINTER = 0x80004003;
    static final int E_ACCESSDENIED = 0x80070005;
    static final int RPC_E_CHANGED_MODE = 0x80010106;
    /** {@code HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)}. */
    static final int FILE_NOT_FOUND = 0x80070002;
    /** {@code HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)}: a file is open without delete sharing. */
    static final int SHARING_VIOLATION = 0x80070020;
    /** {@code HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION)}: a part of a file is locked. */
    static final int LOCK_VIOLATION = 0x80070021;

    /**
     * The user cancelled the whole job. (On Windows 11 a sink's E_ABORT comes back from PerformOperations as E_ABORT
     * itself; either way the answer comes from what the sink saw.)
     */
    static final int COPYENGINE_E_USER_CANCELLED = 0x80270000;
    /** The engine cancelled the whole job itself. */
    static final int COPYENGINE_E_CANCELLED = 0x80270001;
    static final int COPYENGINE_E_REQUIRES_ELEVATION = 0x80270002;
    static final int COPYENGINE_E_ACCESS_DENIED_SRC = 0x80270021;
    /** Access denied on the destination side of the move into the Recycle Bin. */
    static final int COPYENGINE_E_ACCESS_DENIED_DEST = 0x80270022;
    static final int COPYENGINE_E_SHARING_VIOLATION_SRC = 0x80270027;
    /**
     * Sharing violation on the destination side: what recycling a folder answers (in PostDeleteItem's
     * {@code hrDelete} and from PerformOperations) when a file in it is open without delete sharing, because the
     * folder is renamed into the Recycle Bin. Seen on Windows 11 (VK-1695's real test), with every file left in place.
     */
    static final int COPYENGINE_E_SHARING_VIOLATION_DEST = 0x80270028;
    /** Recycling is not available on the drive, usually because it is turned off. */
    static final int COPYENGINE_E_RECYCLE_FORCE_NUKE = 0x80270036;
    static final int COPYENGINE_E_RECYCLE_SIZE_TOO_BIG = 0x80270037;
    static final int COPYENGINE_E_RECYCLE_PATH_TOO_LONG = 0x80270038;
    static final int COPYENGINE_E_RECYCLE_BIN_NOT_FOUND = 0x8027003A;

    /** {@code TSF_DELETE_RECYCLE_IF_POSSIBLE}: PreDeleteItem's flag for "this delete goes into the Recycle Bin". */
    static final int TSF_DELETE_RECYCLE_IF_POSSIBLE = 0x80;

    private static final String PERMANENT_PREFIX =
            "Windows would delete it permanently instead of moving it to the Recycle Bin: ";
    /** Windows was about to delete without the Recycle Bin, and did not say why. */
    static final String PERMANENT_DELETE = PERMANENT_PREFIX
            + "the drive has no Recycle Bin, it is turned off, or the folder is too large or too deep for it";
    static final String RECYCLING_TURNED_OFF = PERMANENT_PREFIX + "the Recycle Bin is turned off for this drive";
    static final String TOO_LARGE = PERMANENT_PREFIX + "the folder is too large for the Recycle Bin";
    static final String TOO_DEEP = PERMANENT_PREFIX + "the folder is too deep for the Recycle Bin";
    static final String NO_RECYCLE_BIN = PERMANENT_PREFIX + "this drive has no Recycle Bin, or it cannot be reached";
    static final String IN_USE = "a file in it is open in another program, or access is denied";
    static final String NEEDS_ADMIN = "it needs administrator rights";
    static final String NOT_INTO_RECYCLE_BIN = "Windows reported the folder deleted, but not into the Recycle Bin";
    static final String NOTHING_REPORTED = "Windows finished without reporting that it moved the folder to the "
            + "Recycle Bin";
    static final String STOPPED_WITHOUT_REASON = "Windows stopped moving the folder to the Recycle Bin without "
            + "saying why";
    static final String NO_ANSWER = "Windows did not say whether it can move the folder to the Recycle Bin";

    private ShellResults() {
        // Utility class - prevent instantiation
    }

    /**
     * What the progress sink saw during one operation. Counts are of calls the shell made; the HRESULTs are
     * {@link #S_OK} when the call never came.
     *
     * @param preDeleteCalls          how many times PreDeleteItem was called: 1 when Windows moves the folder as one
     *                                item, more when it goes through the folder item by item
     * @param firstPreDeleteFlags     the {@code dwFlags} of the first PreDeleteItem (0 when there was none)
     * @param preDeleteWithoutRecycle whether any PreDeleteItem came without {@link #TSF_DELETE_RECYCLE_IF_POSSIBLE}
     * @param postDeleteCalls         how many times PostDeleteItem was called
     * @param deleteResult            the first failing {@code hrDelete} of PostDeleteItem, else {@link #S_OK}
     * @param newlyCreated            whether a PostDeleteItem named the item it created in the Recycle Bin
     * @param finishResult            FinishOperations' {@code hrResult}
     * @param cancelObserved          whether the sink stopped the operation because the caller cancelled
     * @param startOperationsCalls    how many times StartOperations was called
     * @param updateProgressCalls     how many times UpdateProgress was called
     * @param otherCalls              calls about renames, moves, copies and new items, which a delete should not cause
     */
    record SinkReport(int preDeleteCalls, int firstPreDeleteFlags, boolean preDeleteWithoutRecycle,
                      int postDeleteCalls, int deleteResult, boolean newlyCreated, int finishResult,
                      boolean cancelObserved, int startOperationsCalls, int updateProgressCalls, int otherCalls) {

        /** A sink that was never called. */
        static final SinkReport NONE = new SinkReport(0, 0, false, 0, S_OK, false, S_OK, false, 0, 0, 0);

        /**
         * This report of the sink given to DeleteItem, joined with the report of a sink Advise()d for the whole
         * operation, which alone hears StartOperations, UpdateProgress and FinishOperations (and any item below the
         * folder). The deletes are this sink's; only when it heard none at all are the other sink's taken. A
         * PreDeleteItem without the recycle flag counts whichever sink heard it (VK-1695 review: an item-by-item
         * delete reaches only the operation's sink).
         */
        SinkReport withOperation(SinkReport operation) {
            boolean ownDeletes = preDeleteCalls > 0 || postDeleteCalls > 0;
            SinkReport deletes = ownDeletes ? this : operation;
            return new SinkReport(deletes.preDeleteCalls, deletes.firstPreDeleteFlags,
                    preDeleteWithoutRecycle || operation.preDeleteWithoutRecycle, deletes.postDeleteCalls,
                    deletes.deleteResult,
                    deletes.newlyCreated, failed(finishResult) ? finishResult : operation.finishResult,
                    cancelObserved || operation.cancelObserved,
                    startOperationsCalls + operation.startOperationsCalls,
                    updateProgressCalls + operation.updateProgressCalls,
                    otherCalls + operation.otherCalls);
        }
    }

    /**
     * What one {@code IFileOperation} run saw.
     *
     * @param failedStep   the first step whose HRESULT failed ({@code CoInitializeEx}, {@code CoCreateInstance},
     *                     {@code SetOperationFlags}, {@code SHCreateItemFromParsingName}, {@code DeleteItem},
     *                     {@code PerformOperations}, {@code GetAnyOperationsAborted}); {@code null} when none did
     * @param failedResult that step's HRESULT, {@link #S_OK} when none failed
     * @param anyAborted   what GetAnyOperationsAborted answered (false when it was not reached)
     * @param sink         what the progress sink saw
     */
    record Observation(String failedStep, int failedResult, boolean anyAborted, SinkReport sink) {

        /** One diagnostic line, for a log or a test failure. */
        String describe() {
            return "step=" + (failedStep == null ? "none" : failedStep + " " + hex(failedResult))
                    + " aborted=" + anyAborted
                    + " preDelete=" + sink.preDeleteCalls() + " flags=" + hex(sink.firstPreDeleteFlags())
                    + " withoutRecycle=" + sink.preDeleteWithoutRecycle()
                    + " postDelete=" + sink.postDeleteCalls() + " hrDelete=" + hex(sink.deleteResult())
                    + " newlyCreated=" + sink.newlyCreated() + " finish=" + hex(sink.finishResult())
                    + " cancelled=" + sink.cancelObserved() + " start=" + sink.startOperationsCalls()
                    + " progress=" + sink.updateProgressCalls() + " other=" + sink.otherCalls();
        }
    }

    /** {@code hr} as Windows writes it: "0x80270036". */
    static String hex(int hr) {
        return String.format(Locale.ROOT, "0x%08X", hr);
    }

    static boolean failed(int hr) {
        return hr < 0;
    }

    /**
     * The reason a code says Windows would delete permanently instead of recycling, {@code null} for any other code.
     */
    static String notRecyclableReason(int hr) {
        return switch (hr) {
            case COPYENGINE_E_RECYCLE_FORCE_NUKE -> RECYCLING_TURNED_OFF;
            case COPYENGINE_E_RECYCLE_SIZE_TOO_BIG -> TOO_LARGE;
            case COPYENGINE_E_RECYCLE_PATH_TOO_LONG -> TOO_DEEP;
            case COPYENGINE_E_RECYCLE_BIN_NOT_FOUND -> NO_RECYCLE_BIN;
            default -> null;
        };
    }

    /**
     * Whether a code says a file is in use or access is denied: closing a program and trying again can work. The
     * folder is renamed into the Recycle Bin as one item, so such a failure moved nothing. Both sides of the move
     * count: the copy engine reports an open file as a sharing violation on the destination.
     */
    static boolean inUseOrDenied(int hr) {
        return hr == SHARING_VIOLATION || hr == LOCK_VIOLATION
                || hr == COPYENGINE_E_SHARING_VIOLATION_SRC || hr == COPYENGINE_E_SHARING_VIOLATION_DEST
                || hr == E_ACCESSDENIED
                || hr == COPYENGINE_E_ACCESS_DENIED_SRC || hr == COPYENGINE_E_ACCESS_DENIED_DEST;
    }

    /** Whether a code says only administrator rights would let the move go ahead. */
    static boolean needsElevation(int hr) {
        return hr == COPYENGINE_E_REQUIRES_ELEVATION;
    }

    /**
     * The answer to a real recycle. First match wins:
     * <ol>
     *   <li>the sink saw a PreDeleteItem without the recycle flag, or a second one: Windows was about to delete
     *       permanently, or to go through the folder item by item, and the sink stopped it;</li>
     *   <li>a code that names the reason recycling is impossible;</li>
     *   <li>a file in use or access denied: refused, worth retrying;</li>
     *   <li>administrator rights needed: refused, not worth retrying (the launcher never asks for them);</li>
     *   <li>any other failure, named with its step and code;</li>
     *   <li>an operation Windows aborted without a failure code;</li>
     *   <li>success, with PostDeleteItem naming the new Recycle Bin item: recycled;</li>
     *   <li>success without it: not into the Recycle Bin, as far as anyone can tell.</li>
     * </ol>
     */
    static RecycleResult classifyRecycle(Observation observation) {
        SinkReport sink = observation.sink();
        if (sink.preDeleteWithoutRecycle() || sink.preDeleteCalls() > 1) {
            return new RecycleResult.NotRecyclable(PERMANENT_DELETE);
        }
        int[] codes = {sink.deleteResult(), sink.finishResult(), observation.failedResult()};
        for (int code : codes) {
            String reason = notRecyclableReason(code);
            if (reason != null) {
                return new RecycleResult.NotRecyclable(reason);
            }
        }
        for (int code : codes) {
            if (inUseOrDenied(code)) {
                return new RecycleResult.Refused(IN_USE, true);
            }
        }
        for (int code : codes) {
            if (needsElevation(code)) {
                return new RecycleResult.Refused(NEEDS_ADMIN, false);
            }
        }
        if (failed(sink.deleteResult())) {
            return new RecycleResult.Failed("Windows could not move the folder to the Recycle Bin ("
                    + hex(sink.deleteResult()) + ")");
        }
        if (observation.failedStep() != null && failed(observation.failedResult())) {
            return new RecycleResult.Failed("Windows could not move the folder to the Recycle Bin: "
                    + observation.failedStep() + " failed (" + hex(observation.failedResult()) + ")");
        }
        if (failed(sink.finishResult())) {
            return new RecycleResult.Failed("Windows could not move the folder to the Recycle Bin: the operation "
                    + "ended with " + hex(sink.finishResult()));
        }
        if (observation.anyAborted()) {
            return new RecycleResult.Failed(STOPPED_WITHOUT_REASON);
        }
        if (sink.newlyCreated()) {
            return new RecycleResult.Recycled();
        }
        return new RecycleResult.Failed(sink.postDeleteCalls() == 0 ? NOTHING_REPORTED : NOT_INTO_RECYCLE_BIN);
    }

    /**
     * The answer to a dry run, in which the sink stops the operation at the first PreDeleteItem. The E_ABORT and
     * cancelled codes that stop causes are expected, never failures. First match wins:
     * <ol>
     *   <li>a PreDeleteItem with the recycle flag: recyclable; without it: not;</li>
     *   <li>the caller cancelled before Windows got that far;</li>
     *   <li>a code that names the reason recycling is impossible;</li>
     *   <li>otherwise Windows did not say, named with the first failing step and its code (a folder that does not
     *       exist fails in {@code SHCreateItemFromParsingName} with {@link #FILE_NOT_FOUND}).</li>
     * </ol>
     *
     * @param cancelRequested whether the caller had asked to cancel by the time the operation ended
     */
    static RecycleCheck classifyCheck(Observation observation, boolean cancelRequested) {
        SinkReport sink = observation.sink();
        if (sink.preDeleteCalls() > 0) {
            return (sink.firstPreDeleteFlags() & TSF_DELETE_RECYCLE_IF_POSSIBLE) != 0
                    ? new RecycleCheck.Recyclable()
                    : new RecycleCheck.NotRecyclable(PERMANENT_DELETE);
        }
        if (sink.cancelObserved() || cancelRequested) {
            return new RecycleCheck.Cancelled();
        }
        int[] codes = {sink.deleteResult(), sink.finishResult(), observation.failedResult()};
        for (int code : codes) {
            String reason = notRecyclableReason(code);
            if (reason != null) {
                return new RecycleCheck.NotRecyclable(reason);
            }
        }
        if (observation.failedStep() != null && failed(observation.failedResult())) {
            return new RecycleCheck.Unknown(NO_ANSWER + ": " + observation.failedStep() + " failed ("
                    + hex(observation.failedResult()) + ")");
        }
        if (failed(sink.deleteResult())) {
            return new RecycleCheck.Unknown(NO_ANSWER + " (" + hex(sink.deleteResult()) + ")");
        }
        // Nothing failed and nothing asked: Windows may have gone ahead without asking (the caller checks the folder).
        return new RecycleCheck.Unknown(DRY_RUN_WENT_AHEAD);
    }

    /** A dry run that ended without an error and without ever asking the sink (VK-1695 review M1). */
    static final String DRY_RUN_WENT_AHEAD = "Windows finished the check without asking first; look at the folder and "
            + "in the Recycle Bin";
}
