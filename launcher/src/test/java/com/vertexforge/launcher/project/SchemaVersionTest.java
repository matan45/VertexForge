package com.vertexforge.launcher.project;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.NullSource;
import org.junit.jupiter.params.provider.ValueSource;

import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** {@link SchemaVersion#parse} must agree with the editor's {@code parseSchemaVersion} (VK-1686). */
class SchemaVersionTest {

    @Test
    void majorDotMinorIsRead() {
        assertEquals(Optional.of(new SchemaVersion(1, 0)), SchemaVersion.parse("1.0"));
        assertEquals(Optional.of(new SchemaVersion(2, 15)), SchemaVersion.parse("2.15"));
    }

    @Test
    void leadingZerosAreAllowedAndPrintedWithout() {
        SchemaVersion version = SchemaVersion.parse("01.002").orElseThrow();

        assertEquals(new SchemaVersion(1, 2), version);
        assertEquals("1.2", version.toString());
    }

    /** The editor's own list (VFEngine/tests/test_editor_launch.cpp, "schema versions must be MAJOR.MINOR"), and more. */
    @ParameterizedTest
    @ValueSource(strings = {"1", "-1.0", "1.0.0", " 1.0", "1.", ".0", "1.x", "a.b", "+1.0",
            "", ".", "1.0 ", "1 .0", "1,0", "١.٠", "１.0", "0x1.0"})
    void whatTheEditorRefusesIsRefused(String text) {
        assertEquals(Optional.empty(), SchemaVersion.parse(text), text);
    }

    @ParameterizedTest
    @NullSource
    void noTextIsRefused(String text) {
        assertEquals(Optional.empty(), SchemaVersion.parse(text));
    }

    @Test
    void aPartTooLargeFor32BitsSaturatesSoItStaysANewVersion() {
        assertEquals(Optional.of(new SchemaVersion(SchemaVersion.MAX_PART, 0)), SchemaVersion.parse("4294967296.0"));
        assertEquals(Optional.of(new SchemaVersion(SchemaVersion.MAX_PART, SchemaVersion.MAX_PART)),
                SchemaVersion.parse("99999999999999999999999999.99999999999999999999"));
        assertEquals(Optional.of(new SchemaVersion(SchemaVersion.MAX_PART, 1)), SchemaVersion.parse("4294967295.1"));
    }

    @Test
    void partsOutsideTheEditorsRangeCannotBeBuilt() {
        assertThrows(IllegalArgumentException.class, () -> new SchemaVersion(-1, 0));
        assertThrows(IllegalArgumentException.class, () -> new SchemaVersion(0, SchemaVersion.MAX_PART + 1));
    }
}
