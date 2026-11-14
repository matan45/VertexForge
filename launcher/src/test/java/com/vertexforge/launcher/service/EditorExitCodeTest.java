package com.vertexforge.launcher.service;

import org.junit.jupiter.api.Test;

import java.util.HashSet;
import java.util.Optional;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class EditorExitCodeTest {

    @Test
    void theDocumentedCodesHaveTheContractValues() {
        assertEquals(0, EditorExitCode.OK.code());
        assertEquals(2, EditorExitCode.BAD_ARGUMENTS.code());
        assertEquals(3, EditorExitCode.PROJECT_LOAD_FAILED.code());
        assertEquals(4, EditorExitCode.PROJECT_SCHEMA_TOO_NEW.code());
        assertEquals(5, EditorExitCode.PROJECT_ALREADY_OPEN.code());
    }

    @Test
    void everyCodeIsUnique() {
        Set<Integer> seen = new HashSet<>();
        for (EditorExitCode value : EditorExitCode.values()) {
            assertTrue(seen.add(value.code()), "duplicate code " + value.code());
        }
    }

    @Test
    void fromCodeFindsEveryDocumentedCode() {
        for (EditorExitCode value : EditorExitCode.values()) {
            assertEquals(Optional.of(value), EditorExitCode.fromCode(value.code()));
        }
    }

    @Test
    void fromCodeIsEmptyForAnUndocumentedCode() {
        assertEquals(Optional.empty(), EditorExitCode.fromCode(1));
        assertEquals(Optional.empty(), EditorExitCode.fromCode(0xC0000005));
    }

    @Test
    void describeNamesADocumentedCode() {
        assertEquals("code 5: the project is already open in another editor", EditorExitCode.describe(5));
    }

    @Test
    void describeShowsAnExceptionCodeInHex() {
        assertEquals("code -1073741819 (0xC0000005)", EditorExitCode.describe(0xC0000005));
    }

    @Test
    void describeKeepsAnUnknownPositiveCodePlain() {
        assertEquals("code 1", EditorExitCode.describe(1));
    }
}
