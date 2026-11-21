package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The progress sink's safety rule (VK-1695), called as the shell would call it, without Windows. */
class ShellProgressSinkTest {

    private static final int RECYCLES = ShellResults.TSF_DELETE_RECYCLE_IF_POSSIBLE | 0x202;
    private static final int DELETES = 0x202;

    private static int preDelete(ShellProgressSink sink, int flags) {
        return sink.preDeleteItem(MemorySegment.NULL, flags, MemorySegment.NULL);
    }

    @Test
    void theItemSinkLetsOnlyTheFirstRecyclingDeleteThrough() {
        ShellProgressSink sink = new ShellProgressSink(ShellFileOperation.Mode.RECYCLE, () -> false);

        assertEquals(ShellResults.S_OK, preDelete(sink, RECYCLES));
        assertEquals(ShellResults.E_ABORT, preDelete(sink, RECYCLES), "a second item means item by item");
        assertEquals(ShellResults.E_ABORT, new ShellProgressSink(ShellFileOperation.Mode.RECYCLE, () -> false)
                .preDeleteItem(MemorySegment.NULL, DELETES, MemorySegment.NULL), "never a permanent delete");
    }

    /** Review L1: items below the folder reach only the operation's sink, which must refuse every permanent delete. */
    @Test
    void theOperationSinkRefusesEveryPermanentDelete() {
        ShellProgressSink sink = new ShellProgressSink(ShellFileOperation.Mode.RECYCLE, () -> false, true);

        assertEquals(ShellResults.S_OK, preDelete(sink, RECYCLES), "the folder's own, which the item sink hears too");
        assertEquals(ShellResults.S_OK, preDelete(sink, RECYCLES));
        assertEquals(ShellResults.E_ABORT, preDelete(sink, DELETES));
        assertTrue(sink.report().preDeleteWithoutRecycle());
    }

    @Test
    void aDryRunAlwaysStops() {
        for (boolean operationWide : new boolean[] {false, true}) {
            ShellProgressSink sink = new ShellProgressSink(ShellFileOperation.Mode.CHECK, () -> false, operationWide);

            assertEquals(ShellResults.E_ABORT, preDelete(sink, RECYCLES));
            assertEquals(RECYCLES, sink.report().firstPreDeleteFlags());
        }
    }

    @Test
    void aDryRunsCancelStopsItWhileItPreparesButARecycleIsNeverCancelled() {
        assertEquals(ShellResults.E_ABORT, new ShellProgressSink(ShellFileOperation.Mode.CHECK, () -> true, true)
                .startOperations(MemorySegment.NULL));
        assertEquals(ShellResults.S_OK, new ShellProgressSink(ShellFileOperation.Mode.RECYCLE, () -> true, true)
                .updateProgress(MemorySegment.NULL, 10, 1));
    }
}
