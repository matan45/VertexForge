package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** GUID text to memory bytes, as COM and the shell read them (VK-1695). */
class GuidTest {

    private static byte[] bytes(int... values) {
        byte[] bytes = new byte[values.length];
        for (int i = 0; i < values.length; i++) {
            bytes[i] = (byte) values[i];
        }
        return bytes;
    }

    @Test
    void iUnknownKeepsItsLastTwoGroupsAsWritten() {
        assertArrayEquals(bytes(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46),
                Guid.bytes("00000000-0000-0000-C000-000000000046"));
    }

    /** Data1, Data2 and Data3 are little-endian on x64; Data4 is a byte array. */
    @Test
    void theFirstThreeGroupsAreLittleEndian() {
        assertArrayEquals(bytes(0x75, 0x55, 0xd0, 0x3a, 0x57, 0x88, 0x50, 0x48,
                        0x92, 0x77, 0x11, 0xb8, 0x5b, 0xdb, 0x8e, 0x09),
                Guid.bytes("3ad05575-8857-4850-9277-11b85bdb8e09"));
    }

    @Test
    void caseDoesNotMatter() {
        assertArrayEquals(Guid.bytes("3ad05575-8857-4850-9277-11b85bdb8e09"),
                Guid.bytes("3AD05575-8857-4850-9277-11B85BDB8E09"));
    }

    @Test
    void ofCopiesTheBytesIntoTheArena() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment guid = Guid.of("947aab5f-0a5c-4c13-b4d6-4bf7836fc9f8", arena);

            assertEquals(Guid.BYTES, guid.byteSize());
            assertEquals(0, guid.address() % Integer.BYTES, "aligned as the DWORD Data1");
            assertArrayEquals(bytes(0x5f, 0xab, 0x7a, 0x94, 0x5c, 0x0a, 0x13, 0x4c,
                    0xb4, 0xd6, 0x4b, 0xf7, 0x83, 0x6f, 0xc9, 0xf8), guid.toArray(ValueLayout.JAVA_BYTE));
        }
    }

    @Test
    void malformedTextIsRejected() {
        for (String text : Arrays.asList(
                null,
                "",
                "{3ad05575-8857-4850-9277-11b85bdb8e09}",
                "3ad05575-8857-4850-9277-11b85bdb8e0",
                "3ad05575-8857-4850-9277-11b85bdb8e099",
                "3ad055758-857-4850-9277-11b85bdb8e09",
                "3ad05575_8857_4850_9277_11b85bdb8e09",
                "3ad05575-8857-4850-9277-11b85bdb8e0g",
                "3ad05575-8857-4850-9277-11b85bdb8e-9",
                "3ad05575-8857-4850-9277-11b85bdb8e\u0660\u0669")) {
            assertThrows(IllegalArgumentException.class, () -> Guid.bytes(text), String.valueOf(text));
        }
        try (Arena arena = Arena.ofConfined()) {
            assertThrows(IllegalArgumentException.class, () -> Guid.of("not a guid", arena));
        }
    }
}
