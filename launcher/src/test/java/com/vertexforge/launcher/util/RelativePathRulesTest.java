package com.vertexforge.launcher.util;

import com.vertexforge.launcher.util.RelativePathRules.Problem;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The relative-path form shared by template.json (VK-1689) and engine.json's projectCacheDirs (VK-1695). */
class RelativePathRulesTest {

    @ParameterizedTest
    @ValueSource(strings = {"a", "scripts/compiled", "assets/scripts/lib", "a b/c-d_e.f", "Ünïcödé/名前", ".hidden/x"})
    void goodPathsPass(String value) {
        assertEquals(Optional.empty(), RelativePathRules.problem(value), value);
        assertTrue(RelativePathRules.isValid(value), value);
    }

    @Test
    void eachProblemIsNamed() {
        assertEquals(Optional.of(Problem.EMPTY), RelativePathRules.problem(""));
        assertEquals(Optional.of(Problem.NOT_RELATIVE), RelativePathRules.problem("/abs"));
        for (String value : new String[] {"a//b", "a/", ".", "..", "a/../b", "./a", "a/."}) {
            assertEquals(Optional.of(Problem.BAD_PART), RelativePathRules.problem(value), value);
        }
        for (String value : new String[] {"C:/x", "a\\b", "a?b", "a*b", "a<b", "a>b", "a|b", "a\"b", "a\tb"}) {
            assertEquals(Optional.of(Problem.FORBIDDEN_CHARACTER), RelativePathRules.problem(value), value);
        }
        for (String value : new String[] {"a.", "a ", "x/a./y", "x/a /y"}) {
            assertEquals(Optional.of(Problem.BAD_ENDING), RelativePathRules.problem(value), value);
        }
        assertFalse(RelativePathRules.isValid("a/../b"));
    }

    /** The parts are checked left to right, so the first bad part decides, as TemplateManifest always did. */
    @Test
    void theFirstBadPartDecides() {
        assertEquals(Optional.of(Problem.FORBIDDEN_CHARACTER), RelativePathRules.problem("a?/.."));
        assertEquals(Optional.of(Problem.BAD_PART), RelativePathRules.problem("../a?"));
        assertEquals(Optional.of(Problem.BAD_ENDING), RelativePathRules.problem("a./b?"));
    }

    @Test
    void theEditorsOwnFolderIsRecognizedIgnoringCase() {
        assertTrue(RelativePathRules.isEditorState(".vertexforge"));
        assertTrue(RelativePathRules.isEditorState(".VertexForge/saves"));
        assertFalse(RelativePathRules.isEditorState("assets/.vertexforge"));
        assertFalse(RelativePathRules.isEditorState(".vertexforge2"));
    }
}
