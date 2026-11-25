package com.vertexforge.launcher.env;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.util.Arrays;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BooleanSupplier;

/**
 * A COM {@code IFileOperationProgressSink} written in Java, handed to {@code IFileOperation::DeleteItem} by
 * {@link ShellFileOperation} (VK-1695). It is the part that makes the Recycle Bin safe: the shell tells it, item by
 * item, whether a delete would go into the Recycle Bin ({@code TSF_DELETE_RECYCLE_IF_POSSIBLE} in PreDeleteItem's
 * {@code dwFlags}), and the sink stops the operation with {@code E_ABORT} whenever it would not, before anything is
 * deleted. A dry run ({@link ShellFileOperation.Mode#CHECK}) records the flag and always stops.
 * <p>
 * The sink given to DeleteItem hears only that item's PreDeleteItem and PostDeleteItem (seen on Windows 11: one
 * PreDeleteItem with flags {@code 0x282} for a folder on a fixed drive). StartOperations and UpdateProgress, where a
 * dry run's cancel is polled, reach only a sink {@code Advise()}d for the whole operation, so a dry run adds a second
 * CHECK sink for them; a recycle adds one too, which stops any item's PreDeleteItem that would delete permanently,
 * should Windows ever go through the folder item by item.
 * <p>
 * The COM object is a segment whose first 8 bytes point to a 19-slot vtable of upcall stubs, in the vtable order of
 * {@code ShObjIdl_core.h} (SDK 10.0.26100); each stub is bound to this Java object. AddRef and Release do nothing:
 * the object, its vtable and its stubs live in the global arena, so a call the shell makes late, even after the
 * operation was released, never reaches freed memory.
 * <p>
 * The shell may call from another thread than the one that owns the arena, so no stub body touches an arena segment:
 * the state is in atomics, and only the segments the shell passes in are read or written. An exception escaping an
 * upcall stub ends the JVM, so every body catches {@link Throwable} and answers a safe HRESULT.
 */
final class ShellProgressSink {

    /** IUnknown's 3 methods plus the sink's 16. */
    static final int SLOTS = 19;

    private static final byte[] IID_IUNKNOWN = Guid.bytes("00000000-0000-0000-c000-000000000046");
    private static final byte[] IID_IFILE_OPERATION_PROGRESS_SINK =
            Guid.bytes("04b0f1a7-9490-44bc-96e1-4296a31252e2");

    private static final ValueLayout A = ValueLayout.ADDRESS;
    private static final ValueLayout I = ValueLayout.JAVA_INT;

    /** The Java method behind each vtable slot, in vtable order. */
    private static final String[] METHODS = {
        "queryInterface", "addRef", "release",
        "startOperations", "finishOperations",
        "preRenameItem", "postRenameItem",
        "preMoveItem", "postMoveItem",
        "preCopyItem", "postCopyItem",
        "preDeleteItem", "postDeleteItem",
        "preNewItem", "postNewItem",
        "updateProgress", "resetTimer", "pauseTimer", "resumeTimer"
    };

    /**
     * Each slot's C signature: the first parameter is always {@code this}; HRESULT, ULONG, DWORD and UINT are
     * {@code JAVA_INT}; pointers ({@code REFIID}, {@code void**}, {@code IShellItem*}, {@code LPCWSTR}) are
     * {@code ADDRESS}.
     */
    private static final FunctionDescriptor[] SIGNATURES = {
        FunctionDescriptor.of(I, A, A, A),          // QueryInterface(REFIID riid, void **ppv)
        FunctionDescriptor.of(I, A),                // ULONG AddRef()
        FunctionDescriptor.of(I, A),                // ULONG Release()
        FunctionDescriptor.of(I, A),                // StartOperations()
        FunctionDescriptor.of(I, A, I),             // FinishOperations(HRESULT hrResult)
        FunctionDescriptor.of(I, A, I, A, A),       // PreRenameItem(DWORD, IShellItem*, LPCWSTR pszNewName)
        FunctionDescriptor.of(I, A, I, A, A, I, A), // PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*)
        FunctionDescriptor.of(I, A, I, A, A, A),    // PreMoveItem(DWORD, IShellItem*, IShellItem* dest, LPCWSTR)
        FunctionDescriptor.of(I, A, I, A, A, A, I, A), // PostMoveItem(DWORD, item, dest, LPCWSTR, HRESULT, item)
        FunctionDescriptor.of(I, A, I, A, A, A),    // PreCopyItem(DWORD, IShellItem*, IShellItem* dest, LPCWSTR)
        FunctionDescriptor.of(I, A, I, A, A, A, I, A), // PostCopyItem(DWORD, item, dest, LPCWSTR, HRESULT, item)
        FunctionDescriptor.of(I, A, I, A),          // PreDeleteItem(DWORD dwFlags, IShellItem *psiItem)
        FunctionDescriptor.of(I, A, I, A, I, A),    // PostDeleteItem(DWORD, IShellItem*, HRESULT hrDelete, IShellItem*)
        FunctionDescriptor.of(I, A, I, A, A),       // PreNewItem(DWORD, IShellItem* dest, LPCWSTR pszNewName)
        FunctionDescriptor.of(I, A, I, A, A, A, I, I, A), // PostNewItem(DWORD, dest, LPCWSTR, LPCWSTR, DWORD, HRESULT,
                                                          //             IShellItem*)
        FunctionDescriptor.of(I, A, I, I),          // UpdateProgress(UINT iWorkTotal, UINT iWorkSoFar)
        FunctionDescriptor.of(I, A),                // ResetTimer()
        FunctionDescriptor.of(I, A),                // PauseTimer()
        FunctionDescriptor.of(I, A)                 // ResumeTimer()
    };

    /** The unbound method handles, looked up once; {@code null} when they could not be. */
    private static final class Holder {
        static final MethodHandle[] TARGETS = lookup();

        private static MethodHandle[] lookup() {
            try {
                MethodHandles.Lookup lookup = MethodHandles.lookup();
                MethodHandle[] targets = new MethodHandle[SLOTS];
                for (int slot = 0; slot < SLOTS; slot++) {
                    targets[slot] = lookup.findVirtual(ShellProgressSink.class, METHODS[slot],
                            SIGNATURES[slot].toMethodType());
                }
                return targets;
            } catch (Throwable t) {
                return null;
            }
        }
    }

    private final ShellFileOperation.Mode mode;
    private final BooleanSupplier cancelRequested;
    /** Whether this sink is {@code Advise()}d for the whole operation rather than given to DeleteItem. */
    private final boolean operationWide;

    private final AtomicInteger preDeleteCalls = new AtomicInteger();
    private final AtomicInteger firstPreDeleteFlags = new AtomicInteger();
    private final AtomicBoolean preDeleteWithoutRecycle = new AtomicBoolean();
    private final AtomicInteger postDeleteCalls = new AtomicInteger();
    private final AtomicInteger deleteResult = new AtomicInteger(ShellResults.S_OK);
    private final AtomicBoolean newlyCreated = new AtomicBoolean();
    private final AtomicInteger finishResult = new AtomicInteger(ShellResults.S_OK);
    private final AtomicBoolean cancelObserved = new AtomicBoolean();
    private final AtomicInteger startOperationsCalls = new AtomicInteger();
    private final AtomicInteger updateProgressCalls = new AtomicInteger();
    private final AtomicInteger otherCalls = new AtomicInteger();

    /**
     * The sink given to DeleteItem.
     *
     * @param mode            a dry run always stops at the first PreDeleteItem; a recycle lets exactly one through, and
     *                        only with the recycle flag
     * @param cancelRequested polled in StartOperations and UpdateProgress of a dry run; a recycle is never cancelled
     */
    ShellProgressSink(ShellFileOperation.Mode mode, BooleanSupplier cancelRequested) {
        this(mode, cancelRequested, false);
    }

    /**
     * @param operationWide whether the sink is {@code Advise()}d for the whole operation: it then hears every item's
     *                      PreDeleteItem, should Windows go through the folder item by item, and in a recycle stops any
     *                      of them that comes without the recycle flag (it does not count calls, as it may also hear the
     *                      folder's own)
     */
    ShellProgressSink(ShellFileOperation.Mode mode, BooleanSupplier cancelRequested, boolean operationWide) {
        this.mode = Objects.requireNonNull(mode, "mode");
        this.cancelRequested = Objects.requireNonNull(cancelRequested, "cancelRequested");
        this.operationWide = operationWide;
    }

    /**
     * The COM object, allocated with its vtable and stubs in {@code arena}. The arena must outlive every call the shell
     * may still make on the object, Release included; {@link ShellFileOperation} uses the global arena, which is never
     * freed (a few KB per operation), because COM's reference counting is not mirrored here.
     *
     * @throws IllegalStateException when the method handles could not be looked up
     */
    MemorySegment allocate(Arena arena) {
        MethodHandle[] targets = Holder.TARGETS;
        if (targets == null) {
            throw new IllegalStateException("the progress sink's methods could not be looked up");
        }
        Linker linker = Linker.nativeLinker();
        MemorySegment vtable = arena.allocate(ValueLayout.ADDRESS, SLOTS);
        for (int slot = 0; slot < SLOTS; slot++) {
            MemorySegment stub = linker.upcallStub(targets[slot].bindTo(this), SIGNATURES[slot], arena);
            vtable.setAtIndex(ValueLayout.ADDRESS, slot, stub);
        }
        MemorySegment object = arena.allocate(ValueLayout.ADDRESS);
        object.set(ValueLayout.ADDRESS, 0, vtable);
        return object;
    }

    /** What the sink has seen so far. */
    ShellResults.SinkReport report() {
        return new ShellResults.SinkReport(preDeleteCalls.get(), firstPreDeleteFlags.get(),
                preDeleteWithoutRecycle.get(), postDeleteCalls.get(), deleteResult.get(), newlyCreated.get(),
                finishResult.get(), cancelObserved.get(), startOperationsCalls.get(), updateProgressCalls.get(),
                otherCalls.get());
    }

    // ---- IUnknown -------------------------------------------------------------------------------------------------

    /** Answers IUnknown and IFileOperationProgressSink with this object, anything else with E_NOINTERFACE. */
    int queryInterface(MemorySegment self, MemorySegment riid, MemorySegment ppv) {
        try {
            if (ppv.address() == 0) {
                return ShellResults.E_POINTER;
            }
            MemorySegment out = ppv.reinterpret(ValueLayout.ADDRESS.byteSize());
            if (riid.address() != 0) {
                byte[] iid = riid.reinterpret(Guid.BYTES).toArray(ValueLayout.JAVA_BYTE);
                if (Arrays.equals(iid, IID_IUNKNOWN) || Arrays.equals(iid, IID_IFILE_OPERATION_PROGRESS_SINK)) {
                    out.set(ValueLayout.ADDRESS, 0, self);
                    return ShellResults.S_OK;
                }
            }
            out.set(ValueLayout.ADDRESS, 0, MemorySegment.NULL);
            return ShellResults.E_NOINTERFACE;
        } catch (Throwable t) {
            return ShellResults.E_NOINTERFACE;
        }
    }

    /** Lifetime is the arena's, so the count is a constant. */
    int addRef(MemorySegment self) {
        return 1;
    }

    /** Lifetime is the arena's, so the count is a constant. */
    int release(MemorySegment self) {
        return 1;
    }

    // ---- The operation as a whole ---------------------------------------------------------------------------------

    int startOperations(MemorySegment self) {
        try {
            startOperationsCalls.incrementAndGet();
            return continueUnlessCancelled();
        } catch (Throwable t) {
            return ShellResults.S_OK;
        }
    }

    int finishOperations(MemorySegment self, int hrResult) {
        try {
            finishResult.set(hrResult);
        } catch (Throwable t) {
            // Nothing to record.
        }
        return ShellResults.S_OK;
    }

    int updateProgress(MemorySegment self, int workTotal, int workSoFar) {
        try {
            updateProgressCalls.incrementAndGet();
            return continueUnlessCancelled();
        } catch (Throwable t) {
            return ShellResults.S_OK;
        }
    }

    int resetTimer(MemorySegment self) {
        return ShellResults.S_OK;
    }

    int pauseTimer(MemorySegment self) {
        return ShellResults.S_OK;
    }

    int resumeTimer(MemorySegment self) {
        return ShellResults.S_OK;
    }

    /**
     * E_ABORT once a dry run's caller has cancelled. A supplier that throws counts as not cancelled: the dry run then
     * still stops at its PreDeleteItem.
     */
    private int continueUnlessCancelled() {
        if (mode != ShellFileOperation.Mode.CHECK) {
            return ShellResults.S_OK;
        }
        boolean cancelled;
        try {
            cancelled = cancelRequested.getAsBoolean();
        } catch (Throwable t) {
            cancelled = false;
        }
        if (cancelled) {
            cancelObserved.set(true);
            return ShellResults.E_ABORT;
        }
        return ShellResults.S_OK;
    }

    // ---- Deletes: the only items this operation has ---------------------------------------------------------------

    /**
     * The safety rule. A dry run records the flags and always stops. A recycle lets the delete through only when it is
     * the first PreDeleteItem AND carries {@code TSF_DELETE_RECYCLE_IF_POSSIBLE}: without the flag Windows would
     * delete permanently, and a second PreDeleteItem means Windows is going through the folder item by item instead
     * of moving it as one item.
     */
    int preDeleteItem(MemorySegment self, int flags, MemorySegment item) {
        try {
            int call = preDeleteCalls.incrementAndGet();
            boolean recycles = (flags & ShellResults.TSF_DELETE_RECYCLE_IF_POSSIBLE) != 0;
            if (call == 1) {
                firstPreDeleteFlags.set(flags);
            }
            if (!recycles) {
                preDeleteWithoutRecycle.set(true);
            }
            if (mode == ShellFileOperation.Mode.CHECK) {
                return ShellResults.E_ABORT;
            }
            if (operationWide) {
                // Every item, the folder's own and any Windows goes through one by one: never a permanent delete.
                return recycles ? ShellResults.S_OK : ShellResults.E_ABORT;
            }
            return recycles && call == 1 ? ShellResults.S_OK : ShellResults.E_ABORT;
        } catch (Throwable t) {
            return ShellResults.E_ABORT;
        }
    }

    /**
     * Records how the delete went, and whether it created a Recycle Bin item: {@code psiNewlyCreated} is the item now
     * in the Recycle Bin, {@code NULL} after a permanent delete.
     */
    int postDeleteItem(MemorySegment self, int flags, MemorySegment item, int hrDelete,
                       MemorySegment newlyCreatedItem) {
        try {
            postDeleteCalls.incrementAndGet();
            if (ShellResults.failed(hrDelete)) {
                deleteResult.compareAndSet(ShellResults.S_OK, hrDelete);
            }
            if (newlyCreatedItem.address() != 0) {
                newlyCreated.set(true);
            }
        } catch (Throwable t) {
            // Nothing to record.
        }
        return ShellResults.S_OK;
    }

    // ---- Calls a delete does not cause: counted, and let through ------------------------------------------------

    int preRenameItem(MemorySegment self, int flags, MemorySegment item, MemorySegment newName) {
        return countOther();
    }

    int postRenameItem(MemorySegment self, int flags, MemorySegment item, MemorySegment newName, int hrRename,
                       MemorySegment newlyCreatedItem) {
        return countOther();
    }

    int preMoveItem(MemorySegment self, int flags, MemorySegment item, MemorySegment destination,
                    MemorySegment newName) {
        return countOther();
    }

    int postMoveItem(MemorySegment self, int flags, MemorySegment item, MemorySegment destination,
                     MemorySegment newName, int hrMove, MemorySegment newlyCreatedItem) {
        return countOther();
    }

    int preCopyItem(MemorySegment self, int flags, MemorySegment item, MemorySegment destination,
                    MemorySegment newName) {
        return countOther();
    }

    int postCopyItem(MemorySegment self, int flags, MemorySegment item, MemorySegment destination,
                     MemorySegment newName, int hrCopy, MemorySegment newlyCreatedItem) {
        return countOther();
    }

    int preNewItem(MemorySegment self, int flags, MemorySegment destination, MemorySegment newName) {
        return countOther();
    }

    int postNewItem(MemorySegment self, int flags, MemorySegment destination, MemorySegment newName,
                    MemorySegment templateName, int fileAttributes, int hrNew, MemorySegment newItem) {
        return countOther();
    }

    private int countOther() {
        try {
            otherCalls.incrementAndGet();
        } catch (Throwable t) {
            // Nothing to record.
        }
        return ShellResults.S_OK;
    }
}
