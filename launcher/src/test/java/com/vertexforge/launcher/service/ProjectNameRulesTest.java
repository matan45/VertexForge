package com.vertexforge.launcher.service;

import com.vertexforge.launcher.service.ProjectNameRules.Problem;
import com.vertexforge.launcher.service.ProjectNameRules.Rule;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.nio.file.Path;
import java.util.List;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectNameRulesTest {

    private static Rule ruleOf(String name) {
        Optional<Problem> problem = ProjectNameRules.checkName(name);
        assertTrue(problem.isPresent(), () -> "\"" + name + "\" was accepted");
        return problem.get().rule();
    }

    @Test
    void theTicketsFourNamesAreEachRejectedWithTheirOwnMessage() {
        Problem con = ProjectNameRules.checkName("CON").orElseThrow();
        Problem aux = ProjectNameRules.checkName("aux.txt").orElseThrow();
        Problem dot = ProjectNameRules.checkName("Game.").orElseThrow();
        Problem space = ProjectNameRules.checkName("Game ").orElseThrow();

        assertEquals(Rule.RESERVED_NAME, con.rule());
        assertEquals("\"CON\" is reserved by Windows for a device and can't be used as a project name.",
                con.message());
        assertEquals(Rule.RESERVED_NAME_WITH_EXTENSION, aux.rule());
        assertEquals("\"aux.txt\" can't be used: Windows reserves the device name \"AUX\" even when an extension "
                + "follows.", aux.message());
        assertEquals(Rule.TRAILING_DOT, dot.rule());
        assertEquals("The project name can't end with a dot.", dot.message());
        assertEquals(Rule.TRAILING_SPACE, space.rule());
        assertEquals("The project name can't end with a space.", space.message());
        assertEquals(4, List.of(con.message(), aux.message(), dot.message(), space.message())
                .stream().distinct().count());
    }

    @ParameterizedTest
    @ValueSource(strings = {"CON", "con", "Prn", "AUX", "NUL", "COM1", "com9", "LPT1", "LPT9", "COM0", "LPT0",
            "COM¹", "LPT³", "CONIN$", "conout$"})
    void deviceNamesAreReserved(String name) {
        assertEquals(Rule.RESERVED_NAME, ruleOf(name));
    }

    @ParameterizedTest
    @ValueSource(strings = {"aux.txt", "NUL.tar.gz", "nul .txt", "Con.Game", "COM1.project"})
    void deviceNamesAreReservedWithAnExtensionToo(String name) {
        assertEquals(Rule.RESERVED_NAME_WITH_EXTENSION, ruleOf(name));
    }

    @ParameterizedTest
    @ValueSource(strings = {"Game", "Tom & Jerry's Game", "Console", "COM10", "LPT", "CONTROL", ".hidden",
            "My.Game", "Game #2 (final); 100%", "Café", "פרויקט", "Con Game"})
    void ordinaryNamesPass(String name) {
        assertEquals(Optional.empty(), ProjectNameRules.checkName(name));
    }

    @Test
    void emptyIsRejected() {
        assertEquals(Rule.EMPTY, ruleOf(""));
        assertEquals(Rule.EMPTY, ruleOf(null));
    }

    @Test
    void controlCharactersAreRejected() {
        assertEquals(Rule.CONTROL_CHARACTER, ruleOf("Ga\tme"));
        assertEquals(Rule.CONTROL_CHARACTER, ruleOf("Game\n"));
        assertEquals(Rule.CONTROL_CHARACTER, ruleOf("Ga\u0000me"));
        assertEquals(Rule.CONTROL_CHARACTER, ruleOf("Ga\u007fme"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"a:b", "a/b", "a\\b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b"})
    void windowsForbiddenCharactersAreRejected(String name) {
        Problem problem = ProjectNameRules.checkName(name).orElseThrow();

        assertEquals(Rule.FORBIDDEN_CHARACTER, problem.rule());
        assertTrue(problem.message().contains("\"" + name.charAt(1) + "\""), problem.message());
    }

    @Test
    void aLeadingSpaceIsRejectedRatherThanTrimmed() {
        assertEquals(Rule.LEADING_SPACE, ruleOf(" Game"));
        assertEquals(Rule.LEADING_SPACE, ruleOf("   "));
    }

    @Test
    void theTrailingRulesComeBeforeTheDeviceRule() {
        // "CON " and "CON." would also be devices; the trailing character is the thing to fix.
        assertEquals(Rule.TRAILING_SPACE, ruleOf("CON "));
        assertEquals(Rule.TRAILING_DOT, ruleOf("CON."));
    }

    @Test
    void thePathBudgetCountsTheDeepestTemplateFile() {
        Path location = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath().normalize();
        int allowed = ProjectNameRules.MAX_PATH_CHARS - ProjectNameRules.PATH_MARGIN;
        String name = "Game";
        // Pick a library file that makes the deepest path exactly the allowed length, then one character more.
        int base = location.toString().length() + 1 + name.length() + 1 + "scripts/lib/".length();
        ProjectTemplate atLimit = ProjectTemplate.of(List.of("d".repeat(allowed - base)));
        ProjectTemplate overLimit = ProjectTemplate.of(List.of("d".repeat(allowed - base + 1)));

        assertEquals(Optional.empty(), ProjectNameRules.checkPathLength(location, name, atLimit));
        Problem problem = ProjectNameRules.checkPathLength(location, name, overLimit).orElseThrow();
        assertEquals(Rule.PATH_TOO_LONG, problem.rule());
        assertTrue(problem.message().contains("1 more than the " + allowed + " allowed"), problem.message());
    }

    @Test
    void aLongNameMakesTheProjectFileTheDeepestFile() {
        Path location = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath().normalize();
        int allowed = ProjectNameRules.MAX_PATH_CHARS - ProjectNameRules.PATH_MARGIN;
        ProjectTemplate shallow = ProjectTemplate.of(List.of("a.mt"));
        // location \ name \ name.vfproj  ==  location + 1 + n + 1 + n + 7
        int fixed = location.toString().length() + 2 + ".vfproj".length();
        int longest = (allowed - fixed) / 2;

        assertEquals(Optional.empty(), ProjectNameRules.checkPathLength(location, "n".repeat(longest), shallow));
        assertEquals(Rule.PATH_TOO_LONG, ProjectNameRules.checkPathLength(location, "n".repeat(longest + 1), shallow)
                .orElseThrow().rule());
    }
}
