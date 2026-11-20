package com.vertexforge.launcher.instance;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * {@code user32!AllowSetForegroundWindow(DWORD)} through the Foreign Function API (VK-1685), like
 * {@code WindowsDriveTypes}: the lookup happens on first use and needs native access (granted in pom.xml to the module
 * in the app image, to ALL-UNNAMED for the tests). Any failure, now or later, makes it do nothing; the running launcher
 * then only flashes in the taskbar instead of coming to the front.
 */
final class WindowsForegroundGrant implements ForegroundGrant {

    static final WindowsForegroundGrant INSTANCE = new WindowsForegroundGrant();

    /** The largest process id a {@code DWORD} holds. */
    private static final long MAX_DWORD = 0xFFFF_FFFFL;

    /** The function, or {@code null} when it could not be looked up (not Windows, or no native access). */
    private static final class Holder {
        static final MethodHandle ALLOW_SET_FOREGROUND_WINDOW = lookup();

        private static MethodHandle lookup() {
            try {
                MemorySegment address = SymbolLookup.libraryLookup("user32", Arena.global())
                        .find("AllowSetForegroundWindow")
                        .orElse(null);
                if (address == null) {
                    return null;
                }
                return Linker.nativeLinker().downcallHandle(address,
                        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            } catch (Throwable t) {
                return null;
            }
        }
    }

    private WindowsForegroundGrant() {
    }

    @Override
    public void allow(long pid) {
        if (pid < 1 || pid > MAX_DWORD) {
            return;
        }
        try {
            MethodHandle allow = Holder.ALLOW_SET_FOREGROUND_WINDOW;
            if (allow == null) {
                return;
            }
            // A DWORD is unsigned; the int carries the same 32 bits. The BOOL result is not needed: a refused grant
            // only means the window flashes instead of coming to the front.
            int ignored = (int) allow.invokeExact((int) pid);
        } catch (Throwable t) {
            // Nothing to do: see the class comment.
        }
    }
}
