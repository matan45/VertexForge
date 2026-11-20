package com.vertexforge.launcher.env;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * The system's ANSI code page as Windows gives it to a process that does not ask for another one (VK-1693):
 * {@code HKLM\SYSTEM\CurrentControlSet\Control\Nls\CodePage}, value {@code ACP}, read with
 * {@code advapi32!RegGetValueW} through the Foreign Function API.
 * <p>
 * The packaged launcher's exe asks for UTF-8 (its activeCodePage manifest), so its own {@code GetACP()} and
 * {@code native.encoding} say UTF-8; an editor older than VK-1684 still runs in this code page. Native access is
 * granted as for {@link WindowsDriveTypes}; any failure gives an empty result.
 */
final class WindowsCodePage {

    /** {@code HKEY_LOCAL_MACHINE}: {@code (HKEY)(ULONG_PTR)((LONG)0x80000002)}, sign-extended on 64-bit Windows. */
    private static final long HKEY_LOCAL_MACHINE = 0xFFFFFFFF80000002L;
    private static final int RRF_RT_REG_SZ = 0x00000002;
    private static final int ERROR_SUCCESS = 0;
    /** Bytes for the value: "65001" and its terminator need 12, so this leaves room for anything sane. */
    private static final int BUFFER_BYTES = 64;

    static final String KEY = "SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage";
    static final String VALUE = "ACP";

    private WindowsCodePage() {
        // Utility class - prevent instantiation
    }

    /** The function, or {@code null} when it could not be looked up. Initialized on first use. */
    private static final class Holder {
        static final MethodHandle REG_GET_VALUE = lookup();

        private static MethodHandle lookup() {
            try {
                MemorySegment address = SymbolLookup.libraryLookup("advapi32", Arena.global())
                        .find("RegGetValueW")
                        .orElse(null);
                if (address == null) {
                    return null;
                }
                return Linker.nativeLinker().downcallHandle(address, FunctionDescriptor.of(ValueLayout.JAVA_INT,
                        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
            } catch (Throwable t) {
                return null;
            }
        }
    }

    /** The system's ANSI code page number; empty when the registry value cannot be read or is not a number. */
    static OptionalInt systemAnsiCodePage() {
        try {
            MethodHandle regGetValue = Holder.REG_GET_VALUE;
            if (regGetValue == null) {
                return OptionalInt.empty();
            }
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment key = arena.allocateFrom(KEY, StandardCharsets.UTF_16LE);
                MemorySegment value = arena.allocateFrom(VALUE, StandardCharsets.UTF_16LE);
                MemorySegment data = arena.allocate(BUFFER_BYTES);
                MemorySegment size = arena.allocate(ValueLayout.JAVA_INT);
                size.set(ValueLayout.JAVA_INT, 0, BUFFER_BYTES);
                int status = (int) regGetValue.invokeExact(MemorySegment.ofAddress(HKEY_LOCAL_MACHINE), key, value,
                        RRF_RT_REG_SZ, MemorySegment.NULL, data, size);
                if (status != ERROR_SUCCESS) {
                    return OptionalInt.empty();
                }
                return parse(data.getString(0, StandardCharsets.UTF_16LE));
            }
        } catch (Throwable t) {
            return OptionalInt.empty();
        }
    }

    /** "1252" -> 1252; empty for anything but a positive decimal number. */
    static OptionalInt parse(String text) {
        String digits = text == null ? "" : text.trim();
        if (digits.isEmpty() || digits.length() > 9 || !digits.chars().allMatch(c -> c >= '0' && c <= '9')) {
            return OptionalInt.empty();
        }
        int codePage = Integer.parseInt(digits);
        return codePage > 0 ? OptionalInt.of(codePage) : OptionalInt.empty();
    }

    /**
     * The charset Java names a Windows code page with, as the JDK itself does for {@code native.encoding}
     * ({@code java_props_md.c}): UTF-8 for 65001, MS* for the East Asian and Thai double-byte pages, GBK, GB18030,
     * and Cp* for the rest. Empty when this Java has no such charset.
     */
    static Optional<Charset> charsetFor(int codePage) {
        String name = switch (codePage) {
            case 65001 -> "UTF-8";
            case 874, 932, 949, 950, 1361 -> "MS" + codePage;
            case 936 -> "GBK";
            case 54936 -> "GB18030";
            default -> "Cp" + codePage;
        };
        try {
            return Optional.of(Charset.forName(name));
        } catch (IllegalArgumentException e) {
            return Optional.empty();
        }
    }
}
