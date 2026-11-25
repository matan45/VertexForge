package com.vertexforge.launcher.util;

import com.google.gson.JsonObject;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.InvalidManifestException;
import com.vertexforge.launcher.template.TemplateException;
import com.vertexforge.launcher.template.TemplateManifest;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The one version rule every MAJOR.MINOR.PATCH form follows (VK-1690). */
class VersionNumberTest {

    private static VersionNumber version(long major, long minor, long patch) {
        return new VersionNumber(BigInteger.valueOf(major), BigInteger.valueOf(minor), BigInteger.valueOf(patch));
    }

    private static VersionNumber parsed(String text) {
        return VersionNumber.parse(text).orElseThrow(() -> new AssertionError(text + " did not parse"));
    }

    static Stream<String> versions() {
        return Stream.of("0.0.0", "1.0.0", "1.10.0", "10.20.30", "4294967296.0.0",
                "123456789012345678901234567890.0.1");
    }

    static Stream<String> notVersions() {
        return Stream.of("", "1", "1.0", "1.0.0.0", "01.0.0", "1.00.0", "1.0.00", " 1.0.0", "1.0.0 ", "1.0.0\n",
                "1 .0.0", "1.0.0-rc1", "v1.0.0", "1..0", ".1.0", "1.0.", "+1.0.0", "-1.0.0", "1.0.-1", "1,0,0",
                // Digits of other scripts: Java's \d is ASCII only, as the engine's parsers are.
                "١.٠.٠", "１.０.０",
                "VertexForge 1.0.0");
    }

    @ParameterizedTest
    @MethodSource("versions")
    void aVersionParsesAndPrintsBackAsItWasWritten(String text) {
        assertEquals(text, parsed(text).toString());
    }

    @ParameterizedTest
    @MethodSource("notVersions")
    void anythingElseIsNotAVersion(String text) {
        assertEquals(Optional.empty(), VersionNumber.parse(text), text);
    }

    @Test
    void nullIsNotAVersion() {
        assertEquals(Optional.empty(), VersionNumber.parse(null));
        assertEquals(Optional.empty(), VersionNumber.parseProduct(null));
    }

    @Test
    void thePartsAreNumbers() {
        assertEquals(version(1, 10, 3), parsed("1.10.3"));
        assertEquals(new BigInteger("123456789012345678901234567890"),
                parsed("123456789012345678901234567890.0.1").major());
    }

    private static boolean engineManifestAccepts(String engineVersion) {
        JsonObject manifest = new JsonObject();
        manifest.addProperty("manifestVersion", 1);
        manifest.addProperty("engineVersion", engineVersion);
        manifest.addProperty("configuration", "Development");
        manifest.addProperty("editorExe", "Editor.exe");
        manifest.addProperty("workingDirectory", ".");
        try {
            EngineManifest.parse(manifest.toString());
            return true;
        } catch (InvalidManifestException e) {
            return false;
        }
    }

    private static boolean templateAccepts(String minimumEngineVersion) {
        JsonObject template = new JsonObject();
        template.addProperty("formatVersion", 1);
        template.addProperty("id", "empty");
        template.addProperty("name", "Empty");
        template.addProperty("minimumEngineVersion", minimumEngineVersion);
        template.addProperty("workingDirectory", "assets");
        template.addProperty("startupScene", "scenes/Main.vfScene");
        template.addProperty("scriptLibrary", "assets/scripts/lib");
        try {
            TemplateManifest.parse(template.toString());
            return true;
        } catch (TemplateException e) {
            return false;
        }
    }

    /** One rule for every reader: an engine.json's engineVersion and a template's minimumEngineVersion agree. */
    @Test
    void theEngineManifestAndTheTemplatesReadTheSameVersions() {
        List<String> texts = new ArrayList<>(versions().toList());
        texts.addAll(notVersions().toList());
        for (String text : texts) {
            boolean version = VersionNumber.parse(text).isPresent();
            assertEquals(version, engineManifestAccepts(text), "engine.json engineVersion " + text);
            assertEquals(version, templateAccepts(text), "template.json minimumEngineVersion " + text);
        }
    }

    @Test
    void theProductVersionIsWhatTheEditorWritesOrABareVersion() {
        assertEquals("VertexForge", EngineContent.PRODUCT_NAME, "the editor's Version::displayName prefix");
        assertEquals(Optional.of(version(1, 0, 0)), VersionNumber.parseProduct("VertexForge 1.0.0"));
        assertEquals(Optional.of(version(1, 0, 0)), VersionNumber.parseProduct("1.0.0"));
        assertEquals(Optional.of(version(1, 2, 3)), VersionNumber.parseProduct("  VertexForge 1.2.3 \t\r\n"));
        assertEquals(Optional.of(version(1, 2, 3)), VersionNumber.parseProduct(" 1.2.3 "));
    }

    /**
     * Only spaces, tabs, CRs and LFs are trimmed, exactly as the editor's twin ({@code parseVersionTriple},
     * ProjectSerialization.cpp) trims them, so a text the launcher reads as a version is one the editor keeps when it
     * stamps the file, and the other way round (VK-1690).
     */
    @ParameterizedTest
    @ValueSource(strings = {"VertexForge 1.1.0\u000B", "\fVertexForge 1.1.0", "VertexForge 1.1.0 ",
            " VertexForge 1.1.0", "VertexForge 1.1.0\u001C"})
    void otherWhiteSpaceAroundTheProductVersionIsNotTrimmed(String text) {
        assertEquals(Optional.empty(), VersionNumber.parseProduct(text), text);
    }

    /** An asset format version must be exactly MAJOR.MINOR.PATCH: nothing is trimmed (the editor's rule too). */
    @Test
    void aPlainVersionIsNeverTrimmed() {
        assertEquals(Optional.empty(), VersionNumber.parse(" 1.1.0"));
        assertEquals(Optional.empty(), VersionNumber.parse("1.1.0\n"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "   ", "VertexForge", "VertexForge ", "VertexForge  1.0.0", "VertexForge\t1.0.0",
            "VertexForge1.0.0", "vertexforge 1.0.0", "VERTEXFORGE 1.0.0", "Unity 1.0.0", "VertexForge 1.0",
            "VertexForge 01.0.0", "VertexForge 1.0.0 beta", "VertexForge VertexForge 1.0.0", "a newer VertexForge"})
    void anyOtherProductTextIsNotAVersion(String text) {
        assertEquals(Optional.empty(), VersionNumber.parseProduct(text), text);
    }

    @Test
    void versionsCompareNumericallyPartByPart() {
        assertTrue(parsed("1.10.0").compareTo(parsed("1.9.0")) > 0);
        assertTrue(parsed("0.9.9").compareTo(parsed("1.0.0")) < 0);
        assertTrue(parsed("1.0.1").compareTo(parsed("1.0.0")) > 0);
        assertTrue(parsed("2.0.0").compareTo(parsed("1.99.99")) > 0);
        assertTrue(parsed("4294967296.0.0").compareTo(parsed("4294967295.9.9")) > 0);
        assertEquals(0, parsed("2.0.0").compareTo(parsed("2.0.0")));

        List<VersionNumber> sorted = Stream.of("1.10.0", "1.2.0", "0.0.1", "1.2.10", "1.2.9")
                .map(VersionNumberTest::parsed)
                .sorted()
                .toList();
        assertEquals(List.of("0.0.1", "1.2.0", "1.2.9", "1.2.10", "1.10.0"),
                sorted.stream().map(VersionNumber::toString).toList());
    }

    @Test
    void theOrderIsConsistentWithEquals() {
        List<VersionNumber> all = Stream.of("0.0.0", "1.0.0", "1.0.1", "1.1.0", "2.0.0").map(VersionNumberTest::parsed)
                .toList();
        for (VersionNumber a : all) {
            for (VersionNumber b : all) {
                assertEquals(a.equals(b), a.compareTo(b) == 0, a + " vs " + b);
                assertEquals(Integer.signum(a.compareTo(b)), -Integer.signum(b.compareTo(a)), a + " vs " + b);
            }
        }
        assertEquals(parsed("1.2.3"), version(1, 2, 3));
        assertEquals(parsed("1.2.3").hashCode(), version(1, 2, 3).hashCode());
    }

    @Test
    void majorAndMinorCompareWithoutThePatch() {
        assertEquals(0, parsed("1.1.3").compareMajorMinor(parsed("1.1.0")));
        assertEquals(0, parsed("1.1.0").compareMajorMinor(parsed("1.1.3")));
        assertTrue(parsed("1.2.0").compareMajorMinor(parsed("1.1.9")) > 0);
        assertTrue(parsed("1.1.9").compareMajorMinor(parsed("1.2.0")) < 0);
        assertTrue(parsed("0.9.0").compareMajorMinor(parsed("1.0.0")) < 0);
        assertTrue(parsed("2.0.0").compareMajorMinor(parsed("1.10.0")) > 0);
    }

    @Test
    void aPartIsNeverNegativeOrMissing() {
        assertThrows(IllegalArgumentException.class,
                () -> new VersionNumber(BigInteger.ONE, BigInteger.valueOf(-1), BigInteger.ZERO));
        assertThrows(NullPointerException.class, () -> new VersionNumber(BigInteger.ONE, null, BigInteger.ZERO));
        assertThrows(NullPointerException.class, () -> parsed("1.0.0").compareTo(null));
    }
}
