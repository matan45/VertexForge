package com.vertexforge.launcher.env;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

/**
 * A Windows {@code GUID} (CLSID, IID, KNOWNFOLDERID) built from its registry text, for the COM and shell calls of
 * {@link WindowsRecycleBin} and {@link WindowsKnownFolders} (VK-1695).
 * <p>
 * The text {@code 3ad05575-8857-4850-9277-11b85bdb8e09} is not the byte order in memory: {@code GUID} is
 * {@code {DWORD Data1; WORD Data2; WORD Data3; BYTE Data4[8]}}, so on x64 the first three groups are stored
 * little-endian and only the last two groups keep the order they are written in.
 */
final class Guid {

    /** {@code GUID} is 16 bytes, aligned as its {@code DWORD Data1}. */
    static final int BYTES = 16;

    private Guid() {
        // Utility class - prevent instantiation
    }

    /**
     * The 16 bytes of the GUID {@code text} ({@code xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}, either case, no braces) as
     * they lie in memory.
     *
     * @throws IllegalArgumentException when {@code text} is not such a GUID
     */
    static byte[] bytes(String text) {
        if (text == null || text.length() != 36) {
            throw new IllegalArgumentException("not a GUID: " + text);
        }
        for (int dash : new int[] {8, 13, 18, 23}) {
            if (text.charAt(dash) != '-') {
                throw new IllegalArgumentException("not a GUID: " + text);
            }
        }
        String digits = text.substring(0, 8) + text.substring(9, 13) + text.substring(14, 18)
                + text.substring(19, 23) + text.substring(24);
        byte[] written = new byte[BYTES];
        for (int i = 0; i < BYTES; i++) {
            int high = hexValue(digits.charAt(2 * i));
            int low = hexValue(digits.charAt(2 * i + 1));
            if (high < 0 || low < 0) {
                throw new IllegalArgumentException("not a GUID: " + text);
            }
            written[i] = (byte) (high << 4 | low);
        }
        // Data1 (4 bytes), Data2 and Data3 (2 bytes each) are little-endian; Data4 stays as written.
        byte[] memory = written.clone();
        reverse(memory, 0, 4);
        reverse(memory, 4, 2);
        reverse(memory, 6, 2);
        return memory;
    }

    /** The GUID {@code text} in a new 16-byte segment of {@code arena}, ready to pass as a {@code REFIID}. */
    static MemorySegment of(String text, Arena arena) {
        byte[] memory = bytes(text);
        MemorySegment guid = arena.allocate(BYTES, Integer.BYTES);
        MemorySegment.copy(memory, 0, guid, ValueLayout.JAVA_BYTE, 0, BYTES);
        return guid;
    }

    /** An ASCII hex digit's value, -1 for anything else (Character.digit would also take other scripts' digits). */
    private static int hexValue(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    private static void reverse(byte[] bytes, int from, int length) {
        for (int i = 0; i < length / 2; i++) {
            byte swap = bytes[from + i];
            bytes[from + i] = bytes[from + length - 1 - i];
            bytes[from + length - 1 - i] = swap;
        }
    }
}
