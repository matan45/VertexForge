package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;

import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_ACCESS_DENIED_DEST;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_ACCESS_DENIED_SRC;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_CANCELLED;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_RECYCLE_BIN_NOT_FOUND;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_RECYCLE_FORCE_NUKE;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_RECYCLE_PATH_TOO_LONG;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_RECYCLE_SIZE_TOO_BIG;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_REQUIRES_ELEVATION;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_SHARING_VIOLATION_DEST;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_SHARING_VIOLATION_SRC;
import static com.vertexforge.launcher.env.ShellResults.COPYENGINE_E_USER_CANCELLED;
import static com.vertexforge.launcher.env.ShellResults.E_ABORT;
import static com.vertexforge.launcher.env.ShellResults.E_ACCESSDENIED;
import static com.vertexforge.launcher.env.ShellResults.FILE_NOT_FOUND;
import static com.vertexforge.launcher.env.ShellResults.LOCK_VIOLATION;
import static com.vertexforge.launcher.env.ShellResults.S_OK;
import static com.vertexforge.launcher.env.ShellResults.SHARING_VIOLATION;
import static com.vertexforge.launcher.env.ShellResults.TSF_DELETE_RECYCLE_IF_POSSIBLE;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The HRESULTs of a recycle and the answers they give (VK-1695). Pure: no Windows needed. */
class ShellResultsTest {

    /** Every code that means a file is in use or access is denied. */
    private static final int[] IN_USE_CODES = {SHARING_VIOLATION, LOCK_VIOLATION, COPYENGINE_E_SHARING_VIOLATION_SRC,
        COPYENGINE_E_SHARING_VIOLATION_DEST, E_ACCESSDENIED, COPYENGINE_E_ACCESS_DENIED_SRC,
        COPYENGINE_E_ACCESS_DENIED_DEST};

    /** The sink after Windows moved the folder as one item into the Recycle Bin. */
    private static final ShellResults.SinkReport RECYCLED_SINK =
            new ShellResults.SinkReport(1, TSF_DELETE_RECYCLE_IF_POSSIBLE, false, 1, S_OK, true, S_OK, false, 0, 0, 0);

    private static ShellResults.SinkReport preDelete(int calls, int flags, boolean withoutRecycle) {
        return new ShellResults.SinkReport(calls, flags, withoutRecycle, 0, S_OK, false, S_OK, false, 0, 0, 0);
    }

    private static ShellResults.SinkReport postDelete(int hrDelete, boolean newlyCreated) {
        return new ShellResults.SinkReport(1, TSF_DELETE_RECYCLE_IF_POSSIBLE, false, 1, hrDelete, newlyCreated, S_OK,
                false, 0, 0, 0);
    }

    private static ShellResults.Observation observed(ShellResults.SinkReport sink) {
        return new ShellResults.Observation(null, S_OK, false, sink);
    }

    private static ShellResults.Observation failedAt(String step, int hr, ShellResults.SinkReport sink) {
        return new ShellResults.Observation(step, hr, false, sink);
    }

    @Test
    void hresultsAreWrittenAsWindowsWritesThem() {
        assertEquals("0x80270036", ShellResults.hex(COPYENGINE_E_RECYCLE_FORCE_NUKE));
        assertEquals("0x8027003A", ShellResults.hex(COPYENGINE_E_RECYCLE_BIN_NOT_FOUND));
        assertEquals("0x80004004", ShellResults.hex(E_ABORT));
        assertEquals("0x80070002", ShellResults.hex(FILE_NOT_FOUND));
        assertEquals("0x00000000", ShellResults.hex(S_OK));
        assertEquals("0x00000001", ShellResults.hex(ShellResults.S_FALSE));
        assertEquals("0xFFFFFFFF", ShellResults.hex(-1));
    }

    /** The values the SDK headers give (sherrors.h, winerror.h, ShObjIdl_core.h). */
    @Test
    void theCodesAreTheSdkValues() {
        assertEquals(0x80004004, E_ABORT);
        assertEquals(0x80004002, ShellResults.E_NOINTERFACE);
        assertEquals(0x80070005, E_ACCESSDENIED);
        assertEquals(0x80070020, SHARING_VIOLATION);
        assertEquals(0x80070021, LOCK_VIOLATION);
        assertEquals(0x80010106, ShellResults.RPC_E_CHANGED_MODE);
        assertEquals(0x80270000, COPYENGINE_E_USER_CANCELLED);
        assertEquals(0x80270001, COPYENGINE_E_CANCELLED);
        assertEquals(0x80270002, COPYENGINE_E_REQUIRES_ELEVATION);
        assertEquals(0x80270021, COPYENGINE_E_ACCESS_DENIED_SRC);
        assertEquals(0x80270022, COPYENGINE_E_ACCESS_DENIED_DEST);
        assertEquals(0x80270027, COPYENGINE_E_SHARING_VIOLATION_SRC);
        assertEquals(0x80270028, COPYENGINE_E_SHARING_VIOLATION_DEST);
        assertEquals(0x80270036, COPYENGINE_E_RECYCLE_FORCE_NUKE);
        assertEquals(0x80270037, COPYENGINE_E_RECYCLE_SIZE_TOO_BIG);
        assertEquals(0x80270038, COPYENGINE_E_RECYCLE_PATH_TOO_LONG);
        assertEquals(0x8027003A, COPYENGINE_E_RECYCLE_BIN_NOT_FOUND);
        assertEquals(0x80, TSF_DELETE_RECYCLE_IF_POSSIBLE);
        assertEquals(0x0614, ShellFileOperation.FOF_NO_UI);
        assertEquals(0x00080000, ShellFileOperation.FOFX_RECYCLEONDELETE);
        assertEquals(0x00100000, ShellFileOperation.FOFX_EARLYFAILURE);
        assertEquals(0x20000000, ShellFileOperation.FOFX_ADDUNDORECORD);
    }

    @Test
    void theRecycleAddsAnUndoRecordAndTheDryRunDoesNot() {
        assertEquals(0x0614 | 0x00080000 | 0x00100000 | 0x20000000, ShellFileOperation.Mode.RECYCLE.operationFlags);
        assertEquals(0x0614 | 0x00080000 | 0x00100000, ShellFileOperation.Mode.CHECK.operationFlags);
        // No FOFX_SHOWELEVATIONPROMPT: a folder that needs administrator rights is refused, never elevated.
        assertEquals(0, ShellFileOperation.Mode.RECYCLE.operationFlags & 0x00040000);
        assertEquals(0, ShellFileOperation.Mode.CHECK.operationFlags & 0x00040000);
    }

    @Test
    void eachCodeIsClassified() {
        assertEquals(ShellResults.RECYCLING_TURNED_OFF,
                ShellResults.notRecyclableReason(COPYENGINE_E_RECYCLE_FORCE_NUKE));
        assertEquals(ShellResults.TOO_LARGE, ShellResults.notRecyclableReason(COPYENGINE_E_RECYCLE_SIZE_TOO_BIG));
        assertEquals(ShellResults.TOO_DEEP, ShellResults.notRecyclableReason(COPYENGINE_E_RECYCLE_PATH_TOO_LONG));
        assertEquals(ShellResults.NO_RECYCLE_BIN, ShellResults.notRecyclableReason(COPYENGINE_E_RECYCLE_BIN_NOT_FOUND));
        assertNull(ShellResults.notRecyclableReason(SHARING_VIOLATION));
        assertNull(ShellResults.notRecyclableReason(S_OK));

        for (int code : IN_USE_CODES) {
            assertTrue(ShellResults.inUseOrDenied(code), ShellResults.hex(code));
            assertFalse(ShellResults.needsElevation(code), ShellResults.hex(code));
        }
        assertFalse(ShellResults.inUseOrDenied(COPYENGINE_E_REQUIRES_ELEVATION));
        assertTrue(ShellResults.needsElevation(COPYENGINE_E_REQUIRES_ELEVATION));

        assertTrue(ShellResults.failed(E_ABORT));
        assertFalse(ShellResults.failed(S_OK));
        assertFalse(ShellResults.failed(ShellResults.S_FALSE));
    }

    // ---- A real recycle -------------------------------------------------------------------------------------------

    @Test
    void onlyAReportedRecycleBinItemIsRecycled() {
        assertEquals(new RecycleResult.Recycled(), ShellResults.classifyRecycle(observed(RECYCLED_SINK)));
        assertEquals(new RecycleResult.Failed(ShellResults.NOT_INTO_RECYCLE_BIN),
                ShellResults.classifyRecycle(observed(postDelete(S_OK, false))));
        assertEquals(new RecycleResult.Failed(ShellResults.NOTHING_REPORTED),
                ShellResults.classifyRecycle(observed(preDelete(1, TSF_DELETE_RECYCLE_IF_POSSIBLE, false))));
    }

    /** The sink stopped a permanent delete or an item-by-item walk: that is the answer, whatever the codes say. */
    @Test
    void aPermanentDeleteTheSinkStoppedIsNotRecyclable() {
        RecycleResult permanent = new RecycleResult.NotRecyclable(ShellResults.PERMANENT_DELETE);

        assertEquals(permanent, ShellResults.classifyRecycle(failedAt("PerformOperations",
                COPYENGINE_E_USER_CANCELLED, preDelete(1, 0, true))));
        assertEquals(permanent, ShellResults.classifyRecycle(new ShellResults.Observation(null, S_OK, true,
                preDelete(1, 0, true))));
        assertEquals(permanent, ShellResults.classifyRecycle(failedAt("PerformOperations", E_ABORT,
                preDelete(2, TSF_DELETE_RECYCLE_IF_POSSIBLE, false))));
        // Even a sharing violation does not hide that Windows was going through the folder item by item.
        assertEquals(permanent, ShellResults.classifyRecycle(failedAt("PerformOperations", SHARING_VIOLATION,
                preDelete(3, TSF_DELETE_RECYCLE_IF_POSSIBLE, false))));
    }

    @Test
    void recycleBinCodesNameTheirReason() {
        assertEquals(new RecycleResult.NotRecyclable(ShellResults.RECYCLING_TURNED_OFF),
                ShellResults.classifyRecycle(observed(postDelete(COPYENGINE_E_RECYCLE_FORCE_NUKE, false))));
        assertEquals(new RecycleResult.NotRecyclable(ShellResults.TOO_LARGE),
                ShellResults.classifyRecycle(failedAt("PerformOperations", COPYENGINE_E_RECYCLE_SIZE_TOO_BIG,
                        ShellResults.SinkReport.NONE)));
        assertEquals(new RecycleResult.NotRecyclable(ShellResults.TOO_DEEP),
                ShellResults.classifyRecycle(failedAt("PerformOperations", COPYENGINE_E_RECYCLE_PATH_TOO_LONG,
                        ShellResults.SinkReport.NONE)));
        assertEquals(new RecycleResult.NotRecyclable(ShellResults.NO_RECYCLE_BIN),
                ShellResults.classifyRecycle(failedAt("PerformOperations", COPYENGINE_E_CANCELLED,
                        postDelete(COPYENGINE_E_RECYCLE_BIN_NOT_FOUND, false))));
    }

    @Test
    void filesInUseAndDeniedAccessAreRefusedAndWorthRetrying() {
        for (int code : IN_USE_CODES) {
            RecycleResult refused = new RecycleResult.Refused(ShellResults.IN_USE, true);

            assertEquals(refused, ShellResults.classifyRecycle(observed(postDelete(code, false))),
                    ShellResults.hex(code));
            assertEquals(refused, ShellResults.classifyRecycle(failedAt("PerformOperations", code,
                    ShellResults.SinkReport.NONE)), ShellResults.hex(code));
            assertEquals(refused, ShellResults.classifyRecycle(failedAt("PerformOperations", COPYENGINE_E_CANCELLED,
                    postDelete(code, false))), ShellResults.hex(code));
        }
    }

    /** What Windows 11 answered in WindowsRecycleBinRealTest for a folder with a file open without delete sharing. */
    @Test
    void theAnswerToAFileOpenWithoutDeleteSharingIsRefused() {
        assertEquals(new RecycleResult.Refused(ShellResults.IN_USE, true),
                ShellResults.classifyRecycle(failedAt("PerformOperations", COPYENGINE_E_SHARING_VIOLATION_DEST,
                        postDelete(COPYENGINE_E_SHARING_VIOLATION_DEST, false))));
    }

    @Test
    void elevationIsRefusedAndNotWorthRetrying() {
        assertEquals(new RecycleResult.Refused(ShellResults.NEEDS_ADMIN, false),
                ShellResults.classifyRecycle(observed(postDelete(COPYENGINE_E_REQUIRES_ELEVATION, false))));
        assertEquals(new RecycleResult.Refused(ShellResults.NEEDS_ADMIN, false),
                ShellResults.classifyRecycle(failedAt("PerformOperations", COPYENGINE_E_REQUIRES_ELEVATION,
                        ShellResults.SinkReport.NONE)));
    }

    @Test
    void anyOtherFailureIsNamedWithItsStepAndCode() {
        assertEquals(new RecycleResult.Failed("Windows could not move the folder to the Recycle Bin: "
                        + "SHCreateItemFromParsingName failed (0x80070002)"),
                ShellResults.classifyRecycle(failedAt("SHCreateItemFromParsingName", FILE_NOT_FOUND,
                        ShellResults.SinkReport.NONE)));
        assertEquals(new RecycleResult.Failed("Windows could not move the folder to the Recycle Bin: "
                        + "CoInitializeEx failed (0x80010106)"),
                ShellResults.classifyRecycle(failedAt("CoInitializeEx", ShellResults.RPC_E_CHANGED_MODE,
                        ShellResults.SinkReport.NONE)));
        // The delete's own code says more than the operation's "cancelled".
        assertEquals(new RecycleResult.Failed("Windows could not move the folder to the Recycle Bin (0x80070070)"),
                ShellResults.classifyRecycle(failedAt("PerformOperations", COPYENGINE_E_CANCELLED,
                        postDelete(0x80070070, false))));
        // An abort no rule explains is still a failure, never a success.
        assertEquals(new RecycleResult.Failed("Windows could not move the folder to the Recycle Bin: "
                        + "PerformOperations failed (0x80004004)"),
                ShellResults.classifyRecycle(failedAt("PerformOperations", E_ABORT, RECYCLED_SINK)));
        assertEquals(new RecycleResult.Failed(ShellResults.STOPPED_WITHOUT_REASON),
                ShellResults.classifyRecycle(new ShellResults.Observation(null, S_OK, true, RECYCLED_SINK)));
    }

    // ---- A dry run ------------------------------------------------------------------------------------------------

    /** The item sink decides; the operation-level sink adds what only it hears. */
    @Test
    void aDryRunJoinsTheItemSinkWithTheOperationSink() {
        ShellResults.SinkReport item = new ShellResults.SinkReport(1, 0x282, false, 1, E_ABORT, false, S_OK, false,
                0, 0, 0);
        ShellResults.SinkReport operation = new ShellResults.SinkReport(1, 0, true, 1, E_ABORT, false,
                COPYENGINE_E_USER_CANCELLED, true, 1, 5, 2);

        // A PreDeleteItem without the recycle flag counts whichever sink heard it (an item-by-item delete reaches
        // only the operation's sink).
        assertEquals(new ShellResults.SinkReport(1, 0x282, true, 1, E_ABORT, false, COPYENGINE_E_USER_CANCELLED,
                true, 1, 5, 2), item.withOperation(operation));
        // An item sink that heard nothing leaves the deletes to the other sink.
        assertEquals(new ShellResults.SinkReport(1, 0, true, 1, E_ABORT, false, COPYENGINE_E_USER_CANCELLED, true,
                1, 5, 2), ShellResults.SinkReport.NONE.withOperation(operation));
        assertEquals(item, item.withOperation(ShellResults.SinkReport.NONE));
    }

    @Test
    void theFirstPreDeleteItemsFlagAnswersTheDryRun() {
        // The sink's own abort makes PerformOperations fail; that is expected, not a failure.
        assertEquals(new RecycleCheck.Recyclable(), ShellResults.classifyCheck(failedAt("PerformOperations",
                COPYENGINE_E_USER_CANCELLED, preDelete(1, TSF_DELETE_RECYCLE_IF_POSSIBLE | 0x4, false)), false));
        assertEquals(new RecycleCheck.Recyclable(), ShellResults.classifyCheck(new ShellResults.Observation(null,
                S_OK, true, preDelete(1, TSF_DELETE_RECYCLE_IF_POSSIBLE, false)), true));
        assertEquals(new RecycleCheck.NotRecyclable(ShellResults.PERMANENT_DELETE),
                ShellResults.classifyCheck(failedAt("PerformOperations", E_ABORT, preDelete(1, 0, true)), false));
    }

    @Test
    void aCancelBeforeWindowsAnsweredIsCancelled() {
        ShellResults.SinkReport cancelledSink =
                new ShellResults.SinkReport(0, 0, false, 0, S_OK, false, S_OK, true, 1, 0, 0);

        assertEquals(new RecycleCheck.Cancelled(), ShellResults.classifyCheck(failedAt("PerformOperations",
                COPYENGINE_E_USER_CANCELLED, cancelledSink), false));
        assertEquals(new RecycleCheck.Cancelled(), ShellResults.classifyCheck(failedAt("SHCreateItemFromParsingName",
                FILE_NOT_FOUND, ShellResults.SinkReport.NONE), true));
    }

    @Test
    void aDryRunWithoutAPreDeleteItemDoesNotKnow() {
        assertEquals(new RecycleCheck.Unknown(ShellResults.NO_ANSWER
                        + ": SHCreateItemFromParsingName failed (0x80070002)"),
                ShellResults.classifyCheck(failedAt("SHCreateItemFromParsingName", FILE_NOT_FOUND,
                        ShellResults.SinkReport.NONE), false));
        // Nothing failed and nothing asked: Windows may have gone ahead, so the answer says to look.
        assertEquals(new RecycleCheck.Unknown(ShellResults.DRY_RUN_WENT_AHEAD),
                ShellResults.classifyCheck(observed(ShellResults.SinkReport.NONE), false));
        assertEquals(new RecycleCheck.NotRecyclable(ShellResults.NO_RECYCLE_BIN),
                ShellResults.classifyCheck(failedAt("PerformOperations", COPYENGINE_E_RECYCLE_BIN_NOT_FOUND,
                        ShellResults.SinkReport.NONE), false));
    }
}
