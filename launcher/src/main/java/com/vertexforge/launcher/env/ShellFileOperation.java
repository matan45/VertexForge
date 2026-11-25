package com.vertexforge.launcher.env;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.function.BooleanSupplier;

/**
 * One delete through the shell's {@code IFileOperation}, called through the Foreign Function API (VK-1695), with a
 * {@link ShellProgressSink} that stops it unless Windows recycles the folder as one item. This is Electron's
 * {@code shell.trashItem} ({@code platform_util_win.cc}) without its elevation prompt.
 * <p>
 * {@link #run} must be called on a thread of its own: {@code IFileOperation} lives in a single-threaded COM
 * apartment, which {@code CoInitializeEx} makes of the calling thread, and a pooled or virtual thread must not be
 * left with one (or may already be in another). The functions are looked up on first use and need native access, as
 * {@link WindowsDriveTypes} does.
 */
final class ShellFileOperation {

    /** What the operation is for. */
    enum Mode {
        /** A dry run: the sink records what Windows would do and stops at the first PreDeleteItem. */
        CHECK(ShellFileOperation.FOF_NO_UI | ShellFileOperation.FOFX_RECYCLEONDELETE
                | ShellFileOperation.FOFX_EARLYFAILURE),
        /** The real recycle, which the user can undo from Explorer (FOFX_ADDUNDORECORD). */
        RECYCLE(ShellFileOperation.FOF_NO_UI | ShellFileOperation.FOFX_RECYCLEONDELETE
                | ShellFileOperation.FOFX_EARLYFAILURE | ShellFileOperation.FOFX_ADDUNDORECORD);

        final int operationFlags;

        Mode(int operationFlags) {
            this.operationFlags = operationFlags;
        }
    }

    /** {@code FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR} (shellapi.h): no UI at all. */
    static final int FOF_NO_UI = 0x0614;
    /** Recycle instead of deleting; Windows still deletes permanently when it cannot, which the sink stops. */
    static final int FOFX_RECYCLEONDELETE = 0x00080000;
    /** Stop at the first error instead of going on with other items (applies only with FOF_NOERRORUI). */
    static final int FOFX_EARLYFAILURE = 0x00100000;
    /** A user action: Explorer's Undo puts the folder back. */
    static final int FOFX_ADDUNDORECORD = 0x20000000;
    // FOFX_SHOWELEVATIONPROMPT (0x00040000) is left out on purpose: a folder that needs elevation is refused.

    static final int COINIT_APARTMENTTHREADED = 0x2;
    static final int COINIT_DISABLE_OLE1DDE = 0x4;
    /** {@code CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER}. */
    static final int CLSCTX_ALL = 0x17;

    static final String CLSID_FILE_OPERATION = "3ad05575-8857-4850-9277-11b85bdb8e09";
    static final String IID_IFILE_OPERATION = "947aab5f-0a5c-4c13-b4d6-4bf7836fc9f8";
    static final String IID_ISHELL_ITEM = "43826d1e-e718-42ee-bc55-a1e261c37bfe";

    /** IUnknown's Release, the same slot in every COM interface. */
    private static final int RELEASE = 2;
    /** IFileOperation's slots (ShObjIdl_core.h, SDK 10.0.26100). */
    private static final int ADVISE = 3;
    private static final int UNADVISE = 4;
    private static final int SET_OPERATION_FLAGS = 5;
    private static final int DELETE_ITEM = 18;
    private static final int PERFORM_OPERATIONS = 21;
    private static final int GET_ANY_OPERATIONS_ABORTED = 22;

    private ShellFileOperation() {
        // Utility class - prevent instantiation
    }

    /** The functions and the vtable call shapes, or {@code null} when any of them could not be looked up. */
    private record Natives(MethodHandle coInitializeEx, MethodHandle coUninitialize, MethodHandle coCreateInstance,
                           MethodHandle shCreateItemFromParsingName, MethodHandle callThis,
                           MethodHandle callThisInt, MethodHandle callThisPointer,
                           MethodHandle callThisPointerPointer) {
    }

    /** Initialized on first use. */
    private static final class Holder {
        static final Natives NATIVES = lookup();

        private static Natives lookup() {
            try {
                Linker linker = Linker.nativeLinker();
                SymbolLookup ole32 = SymbolLookup.libraryLookup("ole32", Arena.global());
                SymbolLookup shell32 = SymbolLookup.libraryLookup("shell32", Arena.global());
                ValueLayout a = ValueLayout.ADDRESS;
                ValueLayout i = ValueLayout.JAVA_INT;
                return new Natives(
                        // HRESULT CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit)
                        linker.downcallHandle(ole32.find("CoInitializeEx").orElseThrow(),
                                FunctionDescriptor.of(i, a, i)),
                        // void CoUninitialize(void)
                        linker.downcallHandle(ole32.find("CoUninitialize").orElseThrow(),
                                FunctionDescriptor.ofVoid()),
                        // HRESULT CoCreateInstance(REFCLSID, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID, LPVOID*)
                        linker.downcallHandle(ole32.find("CoCreateInstance").orElseThrow(),
                                FunctionDescriptor.of(i, a, a, i, a, a)),
                        // HRESULT SHCreateItemFromParsingName(PCWSTR pszPath, IBindCtx *pbc, REFIID riid, void **ppv)
                        linker.downcallHandle(shell32.find("SHCreateItemFromParsingName").orElseThrow(),
                                FunctionDescriptor.of(i, a, a, a, a)),
                        // Vtable calls: the function address comes first, then `this`.
                        linker.downcallHandle(FunctionDescriptor.of(i, a)),
                        linker.downcallHandle(FunctionDescriptor.of(i, a, i)),
                        linker.downcallHandle(FunctionDescriptor.of(i, a, a)),
                        linker.downcallHandle(FunctionDescriptor.of(i, a, a, a)));
            } catch (Throwable t) {
                return null;
            }
        }
    }

    /** Whether every function this needs was found (Windows, native access granted). */
    static boolean available() {
        try {
            return Holder.NATIVES != null;
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * Deletes {@code folder} through {@code IFileOperation} as {@code mode} says, on the calling thread, and reports
     * what happened. The arguments live in one confined arena of this thread, closed only after the operation and the
     * shell item have been released and COM has been uninitialized. The sinks live in the global arena: COM's
     * reference counting is not mirrored, so a call the shell makes on a sink after that must still find it.
     *
     * @param folder          an absolute folder path the caller has already checked (length, form)
     * @param cancelRequested polled by a dry run's sink
     * @throws Throwable when a native call cannot be made at all (no native access, a function missing); every
     *                   HRESULT, including failures, is reported in the observation instead
     */
    static ShellResults.Observation run(Path folder, Mode mode, BooleanSupplier cancelRequested) throws Throwable {
        Natives natives = Holder.NATIVES;
        if (natives == null) {
            throw new IllegalStateException("the shell's IFileOperation functions could not be looked up");
        }
        ShellProgressSink sink = new ShellProgressSink(mode, cancelRequested);
        // Only a sink Advise()d for the whole operation hears StartOperations and UpdateProgress, and the items below
        // the folder should Windows go through them one by one (the sink given to DeleteItem hears only that item's
        // Pre/PostDeleteItem). A dry run adds one to let a cancel stop Windows while it prepares; it is a CHECK sink
        // too, so its PreDeleteItem stops the dry run as well. A recycle adds one that stops any item's PreDeleteItem
        // without the recycle flag: never a permanent delete, whichever way Windows goes.
        ShellProgressSink progress = new ShellProgressSink(mode, cancelRequested, true);
        try (Arena arena = Arena.ofConfined()) {
            int initialized = (int) natives.coInitializeEx().invokeExact(MemorySegment.NULL,
                    COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            // S_FALSE: COM was already initialized on this thread, which still needs a matching CoUninitialize.
            if (initialized != ShellResults.S_OK && initialized != ShellResults.S_FALSE) {
                return failed("CoInitializeEx", initialized, sink, progress);
            }
            try {
                return perform(natives, arena, folder, mode, sink, progress);
            } finally {
                natives.coUninitialize().invokeExact();
            }
        }
    }

    private static ShellResults.Observation perform(Natives natives, Arena arena, Path folder, Mode mode,
                                                    ShellProgressSink sink, ShellProgressSink progress)
            throws Throwable {
        MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
        out.set(ValueLayout.ADDRESS, 0, MemorySegment.NULL);
        int hr = (int) natives.coCreateInstance().invokeExact(Guid.of(CLSID_FILE_OPERATION, arena),
                MemorySegment.NULL, CLSCTX_ALL, Guid.of(IID_IFILE_OPERATION, arena), out);
        MemorySegment operation = out.get(ValueLayout.ADDRESS, 0);
        if (ShellResults.failed(hr) || operation.address() == 0) {
            return failed("CoCreateInstance", ShellResults.failed(hr) ? hr : ShellResults.E_POINTER, sink, progress);
        }
        try {
            hr = (int) natives.callThisInt().invokeExact(method(operation, SET_OPERATION_FLAGS), operation,
                    mode.operationFlags);
            if (ShellResults.failed(hr)) {
                return failed("SetOperationFlags", hr, sink, progress);
            }
            // A failed Advise costs a dry run its cancel (it still stops at its PreDeleteItem) and a recycle its guard
            // of items below the folder: a recycle without its guard does not go ahead.
            MemorySegment cookieOut = arena.allocate(ValueLayout.JAVA_INT);
            int adviseResult = (int) natives.callThisPointerPointer().invokeExact(method(operation, ADVISE),
                    operation, progress.allocate(Arena.global()), cookieOut);
            boolean advised = !ShellResults.failed(adviseResult);
            int cookie = cookieOut.get(ValueLayout.JAVA_INT, 0);
            if (!advised && mode == Mode.RECYCLE) {
                return failed("Advise", adviseResult, sink, progress);
            }
            try {
                return deleteOneItem(natives, arena, operation, folder, sink, progress);
            } finally {
                if (advised) {
                    int ignored = (int) natives.callThisInt().invokeExact(method(operation, UNADVISE), operation,
                            cookie);
                }
            }
        } finally {
            release(natives, operation);
        }
    }

    /** SHCreateItemFromParsingName, DeleteItem with the deciding sink, PerformOperations, GetAnyOperationsAborted. */
    private static ShellResults.Observation deleteOneItem(Natives natives, Arena arena, MemorySegment operation,
                                                          Path folder, ShellProgressSink sink,
                                                          ShellProgressSink progress) throws Throwable {
        MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
        out.set(ValueLayout.ADDRESS, 0, MemorySegment.NULL);
        MemorySegment path = arena.allocateFrom(folder.toString(), StandardCharsets.UTF_16LE);
        int hr = (int) natives.shCreateItemFromParsingName().invokeExact(path, MemorySegment.NULL,
                Guid.of(IID_ISHELL_ITEM, arena), out);
        MemorySegment item = out.get(ValueLayout.ADDRESS, 0);
        if (ShellResults.failed(hr) || item.address() == 0) {
            return failed("SHCreateItemFromParsingName", ShellResults.failed(hr) ? hr : ShellResults.E_POINTER,
                    sink, progress);
        }
        try {
            hr = (int) natives.callThisPointerPointer().invokeExact(method(operation, DELETE_ITEM), operation,
                    item, sink.allocate(Arena.global()));
            if (ShellResults.failed(hr)) {
                return failed("DeleteItem", hr, sink, progress);
            }
            int performed = (int) natives.callThis().invokeExact(method(operation, PERFORM_OPERATIONS), operation);
            MemorySegment abortedOut = arena.allocate(ValueLayout.JAVA_INT);
            abortedOut.set(ValueLayout.JAVA_INT, 0, 0);
            int queried = (int) natives.callThisPointer().invokeExact(method(operation, GET_ANY_OPERATIONS_ABORTED),
                    operation, abortedOut);
            boolean aborted = !ShellResults.failed(queried) && abortedOut.get(ValueLayout.JAVA_INT, 0) != 0;
            if (ShellResults.failed(performed)) {
                return new ShellResults.Observation("PerformOperations", performed, aborted, report(sink, progress));
            }
            if (ShellResults.failed(queried)) {
                return new ShellResults.Observation("GetAnyOperationsAborted", queried, false,
                        report(sink, progress));
            }
            return new ShellResults.Observation(null, ShellResults.S_OK, aborted, report(sink, progress));
        } finally {
            release(natives, item);
        }
    }

    private static ShellResults.Observation failed(String step, int hr, ShellProgressSink sink,
                                                   ShellProgressSink progress) {
        return new ShellResults.Observation(step, hr, false, report(sink, progress));
    }

    /** The deciding sink's report, joined with what the operation-level sink saw. */
    private static ShellResults.SinkReport report(ShellProgressSink sink, ShellProgressSink progress) {
        return sink.report().withOperation(progress.report());
    }

    /** The function in vtable slot {@code slot} of the COM object {@code object}. */
    private static MemorySegment method(MemorySegment object, int slot) {
        long pointer = ValueLayout.ADDRESS.byteSize();
        MemorySegment vtable = object.reinterpret(pointer).get(ValueLayout.ADDRESS, 0)
                .reinterpret(pointer * (slot + 1L));
        MemorySegment function = vtable.getAtIndex(ValueLayout.ADDRESS, slot);
        if (function.address() == 0) {
            throw new IllegalStateException("COM vtable slot " + slot + " is empty");
        }
        return function;
    }

    private static void release(Natives natives, MemorySegment object) throws Throwable {
        int ignored = (int) natives.callThis().invokeExact(method(object, RELEASE), object);
    }
}
