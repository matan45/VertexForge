package com.vertexforge.launcher.project;

import com.google.gson.JsonObject;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.project.VersionCompatibility.Compatible;
import com.vertexforge.launcher.project.VersionCompatibility.Newer;
import com.vertexforge.launcher.project.VersionCompatibility.Older;
import com.vertexforge.launcher.project.VersionCompatibility.Unknown;
import com.vertexforge.launcher.project.VersionCompatibility.Unsupported;
import com.vertexforge.launcher.project.VersionCompatibility.Verdict;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.util.VersionNumber;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Whether an engine can open a project, row by row (VK-1690). */
class VersionCompatibilityTest {

    private static final Path ROOT = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath();
    private static final UUID A = UUID.fromString("5b0c9d1e-7f0a-4d3b-9a51-2f6f1c0e8a42");
    private static final UUID B = UUID.fromString("c3a8e1f2-0b6d-4e5a-8f7c-1d2e3f4a5b6c");
    private static final UUID C = UUID.fromString("0f1e2d3c-4b5a-4968-8776-655443322110");

    /** A project the editor accepts; {@code null} leaves a version out. */
    private static ProjectFile project(String engineVersion, String schemaVersion, String assetFormatVersion) {
        return new ProjectFile("Game", schemaVersion, engineVersion, null, "assets", assetFormatVersion);
    }

    /** An engine.json naming these versions; {@code null} leaves a member out. */
    private static EngineManifest manifest(String engineVersion, String projectSchemaVersion,
                                           String assetFormatVersion) {
        JsonObject manifest = new JsonObject();
        manifest.addProperty("manifestVersion", 1);
        manifest.addProperty("engineVersion", engineVersion);
        if (assetFormatVersion != null) {
            manifest.addProperty("assetFormatVersion", assetFormatVersion);
        }
        if (projectSchemaVersion != null) {
            manifest.addProperty("projectSchemaVersion", projectSchemaVersion);
        }
        manifest.addProperty("configuration", "Development");
        manifest.addProperty("editorExe", "Editor.exe");
        manifest.addProperty("workingDirectory", ".");
        try {
            return EngineManifest.parse(manifest.toString());
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    private static Verdict check(ProjectFile project, EngineManifest manifest) {
        return VersionCompatibility.check(project, manifest, manifest.describe());
    }

    private static EngineOption engine(UUID id, EngineManifest manifest) {
        Path exe = ROOT.resolve(id.toString()).resolve("Editor.exe");
        return new EngineOption(Optional.of(id), exe, exe.toString(), EngineHealth.ok(manifest, ROOT), false, false,
                false);
    }

    private static EngineOption override(EngineManifest manifest) {
        Path exe = ROOT.resolve("override").resolve("Editor.exe");
        return new EngineOption(Optional.empty(), exe, exe.toString(), EngineHealth.ok(manifest, ROOT), false, false,
                true);
    }

    private static EngineOptions options(EngineOption override, EngineOption... engines) {
        EngineRegistryView registry = new EngineRegistryView(List.of(), Optional.empty(), StoreStatus.OK,
                Optional.empty(), "", 0);
        return new EngineOptions(registry, Optional.ofNullable(override), Arrays.asList(engines));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The acceptance criteria
    // ---------------------------------------------------------------------------------------------------------------

    /** AC1: a project 1.0.0 with no asset format, opened with an engine 1.1.0 that reads asset format 1.1.0. */
    @Test
    void anOlderProjectWithoutAnAssetFormatNeedsAReimport() {
        Verdict verdict = check(project("VertexForge 1.0.0", "1.0", null), manifest("1.1.0", "1.0", "1.1.0"));

        Older older = assertInstanceOf(Older.class, verdict);
        assertTrue(older.reimport());
        assertEquals("VertexForge 1.0.0", older.savedBy());
        assertEquals("Saved by VertexForge 1.0.0 (project format 1.0, asset format 1.0.0). This engine (1.1.0"
                + " Development) reads asset format 1.1.0, so its assets need a re-import. Opening it marks the project"
                + " as VertexForge 1.1.0, which engines older than 1.1 cannot open.", older.reason());
        assertFalse(older.blocksOpen());
    }

    /** AC2: a project 1.1.0 / asset format 1.1.0 against an engine 1.0.0 / 1.0.0. */
    @Test
    void aProjectInANewerAssetFormatNeedsTheVersionThatSavedIt() {
        Verdict verdict = check(project("VertexForge 1.1.0", "1.0", "1.1.0"), manifest("1.0.0", "1.0", "1.0.0"));

        Newer newer = assertInstanceOf(Newer.class, verdict);
        assertEquals("VertexForge 1.1.0", newer.required());
        assertEquals("Saved by VertexForge 1.1.0 (project format 1.0, asset format 1.1.0). This engine (1.0.0"
                + " Development) reads asset format 1.0.0, which is older than the project's, so it cannot read its"
                + " assets. It needs VertexForge 1.1.0 or later.", newer.reason());
        assertTrue(newer.blocksOpen());
    }

    /** AC4: only the patch differs, either way round. */
    @Test
    void aPatchOnlyDifferenceOpensSilently() {
        Verdict newerEngine = check(project("VertexForge 1.1.0", "1.0", "1.1.0"), manifest("1.1.3", "1.0", "1.1.0"));
        Verdict olderEngine = check(project("VertexForge 1.1.3", "1.0", "1.1.0"), manifest("1.1.0", "1.0", "1.1.0"));

        assertEquals(new Compatible(), newerEngine);
        assertEquals(new Compatible(), olderEngine);
        assertEquals("", newerEngine.reason());
        assertFalse(newerEngine.blocksOpen());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Row by row
    // ---------------------------------------------------------------------------------------------------------------

    /** R0. */
    @Test
    void anEngineWithoutAnEngineJsonCannotTell() {
        ProjectFile project = project("VertexForge 1.0.0", "1.0", null);

        Unknown unknown = assertInstanceOf(Unknown.class,
                VersionCompatibility.check(project, null, "C:\\VF\\Editor.exe"));
        assertEquals("The engine (C:\\VF\\Editor.exe) has no engine.json, so the launcher cannot tell whether it reads"
                + " this project.", unknown.reason());

        // An engine option names itself by its path then, as it has no version to show.
        Path exe = ROOT.resolve("old").resolve("Editor.exe");
        EngineOption withoutManifest = new EngineOption(Optional.of(A), exe, exe.toString(),
                EngineHealth.ok(null, ROOT), false, false, false);
        Unknown fromOption = assertInstanceOf(Unknown.class, VersionCompatibility.check(project, withoutManifest));
        assertTrue(fromOption.reason().startsWith("The engine (" + exe + ") has no engine.json"), fromOption.reason());
        assertFalse(fromOption.blocksOpen());
    }

    /** R1, before every other row. */
    @Test
    void aNewerSchemaMajorNeedsANewerEngine() {
        Verdict verdict = check(project("VertexForge 2.1.0", "2.0", "1.0.0"), manifest("1.5.0", "1.3", "1.1.0"));

        Newer newer = assertInstanceOf(Newer.class, verdict);
        assertEquals("VertexForge 2.1.0", newer.required());
        assertEquals("Saved by VertexForge 2.1.0 (project format 2.0, asset format 1.0.0). This engine (1.5.0"
                + " Development) reads project format 1.x, so it cannot open it. It needs VertexForge 2.1.0 or later.",
                newer.reason());
    }

    /** R2: the editor's words; it comes before the asset format and the product version. */
    @Test
    void anOlderSchemaMajorIsRefusedInTheEditorsWords() {
        Verdict verdict = check(project("VertexForge 3.0.0", "0.5", "2.0.0"), manifest("1.0.0", "1.0", "1.0.0"));

        Unsupported unsupported = assertInstanceOf(Unsupported.class, verdict);
        assertEquals("Project schema version 0.5 is older than this engine supports (1.0)", unsupported.reason());
        assertTrue(unsupported.blocksOpen());
    }

    /** R3 before R4 and R5, whatever the product version says. */
    @Test
    void aNewerAssetFormatBlocksEvenWhenTheProductIsNotNewer() {
        Verdict verdict = check(project("VertexForge 1.0.0", "1.0", "1.0.1"), manifest("1.0.0", "1.0", "1.0.0"));

        Newer newer = assertInstanceOf(Newer.class, verdict);
        // The project names no version newer than this engine's, so it cannot say which one it needs.
        assertEquals(VersionCompatibility.ANY_NEWER, newer.required());
        assertTrue(newer.reason().endsWith(" It needs a newer VertexForge."), newer.reason());
    }

    /** R4: a newer major or minor blocks, even with the same formats. */
    @Test
    void aNewerProductMajorOrMinorNeedsANewerEngine() {
        Verdict verdict = check(project("VertexForge 1.1.0", "1.0", "1.0.0"), manifest("1.0.7", "1.0", "1.0.0"));

        Newer newer = assertInstanceOf(Newer.class, verdict);
        assertEquals("VertexForge 1.1.0", newer.required());
        assertEquals("Saved by VertexForge 1.1.0 (project format 1.0, asset format 1.0.0). This engine (1.0.7"
                + " Development) is older than the VertexForge 1.1 that saved it. It needs VertexForge 1.1.0 or later.",
                newer.reason());
    }

    /** Every newer row comes before every older row: a newer product with older assets is still newer. */
    @Test
    void newerWinsOverOlder() {
        assertInstanceOf(Newer.class,
                check(project("VertexForge 2.0.0", "1.0", "1.0.0"), manifest("1.5.0", "1.0", "1.1.0")));
        assertInstanceOf(Newer.class,
                check(project("VertexForge 1.0.0", "2.0", "1.0.0"), manifest("1.5.0", "1.0", "1.1.0")));
    }

    /** R5 with a format the project names. */
    @Test
    void anOlderAssetFormatNeedsAReimport() {
        Older older = assertInstanceOf(Older.class,
                check(project("VertexForge 1.1.0", "1.0", "1.0.0"), manifest("1.1.0", "1.0", "1.1.0")));

        assertTrue(older.reimport());
        assertEquals("VertexForge 1.1.0", older.savedBy());
    }

    /** R6: the product is older, the asset format the same. */
    @Test
    void anOlderProductWithTheSameAssetFormatUpgradesWithoutAReimport() {
        Older older = assertInstanceOf(Older.class,
                check(project("VertexForge 1.0.0", "1.0", "1.0.0"), manifest("1.1.0", "1.0", "1.0.0")));

        assertFalse(older.reimport());
        assertEquals("Saved by VertexForge 1.0.0 (project format 1.0, asset format 1.0.0). This engine (1.1.0"
                + " Development) is newer and reads the same asset format, so its assets need no re-import. Opening it"
                + " marks the project as VertexForge 1.1.0, which engines older than 1.1 cannot open.", older.reason());
    }

    /** R6 for a project without an asset format: the legacy format is assumed, so nothing is said about the assets. */
    @Test
    void anOlderLegacyProjectClaimsNothingAboutItsAssets() {
        Older older = assertInstanceOf(Older.class,
                check(project("VertexForge 0.0.3", "1.0", null), manifest("1.1.0", "1.0", "1.0.0")));

        assertFalse(older.reimport());
        assertEquals("Saved by VertexForge 0.0.3 (project format 1.0, asset format 1.0.0). This engine (1.1.0"
                + " Development) is newer. Opening it marks the project as VertexForge 1.1.0, which engines older than"
                + " 1.1 cannot open.", older.reason());
    }

    /** R7: whichever format the engine.json leaves out. */
    @Test
    void anEngineThatDoesNotNameItsFormatsCannotTell() {
        String expected = "The engine (1.0.0 Development) does not say in its engine.json which project and asset"
                + " formats it reads, so the launcher cannot tell whether it reads this project.";
        ProjectFile project = project("VertexForge 1.0.0", "1.0", "1.0.0");

        assertEquals(new Unknown(expected), check(project, manifest("1.0.0", null, "1.0.0")));
        assertEquals(new Unknown(expected), check(project, manifest("1.0.0", "1.0", null)));
        assertEquals(new Unknown(expected), check(project, manifest("1.0.0", "garbled", "1.0.0")));
        assertEquals(new Unknown(expected), check(project, manifest("1.0.0", "1.0", "1.0")));
        // A difference it can see is still judged.
        assertInstanceOf(Newer.class, check(project("VertexForge 1.1.0", "1.0", null), manifest("1.0.0", null, null)));
    }

    /** R8. */
    @Test
    void aProjectThatDoesNotNameItsVersionCannotTell() {
        EngineManifest engine = manifest("1.0.0", "1.0", "1.0.0");
        Unknown noVersion = new Unknown("The project file does not say which VertexForge saved it, so the launcher"
                + " cannot tell whether this engine (1.0.0 Development) reads it.");

        assertEquals(noVersion, check(project(null, "1.0", null), engine));
        assertEquals(noVersion, check(project("  ", "1.0", null), engine));
        assertEquals(new Unknown("The project file's engineVersion is not a VertexForge version, so the launcher"
                        + " cannot tell whether this engine (1.0.0 Development) reads it."),
                check(project("VertexForge two", "1.0", null), engine));
    }

    /** R9: present but not a version (also a member of the wrong type, which the reader keeps as JSON text). */
    @Test
    void aMalformedAssetFormatCannotTell() throws InvalidProjectFileException {
        EngineManifest engine = manifest("1.0.0", "1.0", "1.0.0");
        String expected = "The project file's assetFormatVersion is not a MAJOR.MINOR.PATCH version, so the launcher"
                + " cannot tell whether this engine (1.0.0 Development) reads its assets.";

        assertEquals(new Unknown(expected), check(project("VertexForge 1.0.0", "1.0", "1.0"), engine));
        assertEquals(new Unknown(expected), check(project("VertexForge 1.0.0", "1.0", ""), engine));
        ProjectFile wrongType = ProjectFileReader.parse("{\"projectName\": \"Game\", \"schemaVersion\": \"1.0\","
                + " \"engineVersion\": \"VertexForge 1.0.0\", \"assetFormatVersion\": 1}");
        assertEquals(new Unknown(expected), check(wrongType, engine));
    }

    /** R10: the same versions, a patch either way, or a newer schema MINOR (the editor accepts it). */
    @Test
    void whatTheEditorTakesAsItIsIsCompatible() {
        EngineManifest engine = manifest("1.0.0", "1.0", "1.0.0");

        assertEquals(new Compatible(), check(project("VertexForge 1.0.0", "1.0", "1.0.0"), engine));
        assertEquals(new Compatible(), check(project("VertexForge 1.0.0", "1.0", null), engine));
        assertEquals(new Compatible(), check(project("1.0.0", "1.0", "1.0.0"), engine));
        assertEquals(new Compatible(), check(project("VertexForge 1.0.5", "1.0", "1.0.0"), engine));
        assertEquals(new Compatible(), check(project("VertexForge 1.0.0", "1.9", "1.0.0"), engine));
        assertEquals(new Compatible(), check(project("VertexForge 1.0.0", "01.0", "1.0.0"), engine));
    }

    /** Before the rows: a schema version the editor cannot read is refused by any engine, in its words. */
    @Test
    void aSchemaVersionTheEditorCannotReadIsUnsupportedByAnyEngine() {
        assertEquals(new Unsupported("Invalid schema version format: '1.0.0' (expected MAJOR.MINOR)"),
                check(project("VertexForge 1.0.0", "1.0.0", null), manifest("1.0.0", "1.0", "1.0.0")));
        assertEquals(new Unsupported("Invalid schema version format: '1.0.0' (expected MAJOR.MINOR)"),
                VersionCompatibility.check(project("VertexForge 1.0.0", "1.0.0", null), null, "C:\\VF\\Editor.exe"));
        // The editor reports a member it refuses first.
        ProjectFile structure = new ProjectFile("Game", null, "VertexForge 1.0.0",
                "Invalid project file: missing or invalid 'schemaVersion' field", ".", null);
        assertEquals(new Unsupported("Invalid project file: missing or invalid 'schemaVersion' field"),
                check(structure, manifest("1.0.0", "1.0", "1.0.0")));
    }

    /** A version longer than any engine writes is not read (a megabyte of digits would stall the FX thread). */
    @Test
    void aHugeVersionIsNotReadAndCostsNothing() {
        String huge = "VertexForge 1.0." + "9".repeat(1_000_000);
        ProjectFile project = project(huge, "1.0", "1.0." + "9".repeat(1_000_000));
        EngineManifest engine = manifest("1.0.0", "1.0", "1.0.0");

        Verdict verdict = assertTimeoutPreemptively(Duration.ofSeconds(2), () -> check(project, engine));

        assertInstanceOf(Unknown.class, verdict);
        assertEquals(VersionCompatibility.UNKNOWN_SAVER, VersionCompatibility.savedBy(project));
        // The limit leaves room for any version an engine can write.
        String longest = "VertexForge 4294967295.4294967295.4294967295";
        assertTrue(longest.length() <= VersionCompatibility.MAX_VERSION_TEXT);
        assertEquals(Optional.of("4294967295.4294967295.4294967295"),
                VersionCompatibility.savedVersion(project(longest, "1.0", null)));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The project's side
    // ---------------------------------------------------------------------------------------------------------------

    @Test
    void savedByNormalisesTheProjectsVersion() {
        assertEquals("VertexForge 1.0.0", VersionCompatibility.savedBy(project("VertexForge 1.0.0", "1.0", null)));
        assertEquals("VertexForge 1.2.3", VersionCompatibility.savedBy(project(" 1.2.3 ", "1.0", null)));
        assertEquals("an unknown VertexForge", VersionCompatibility.savedBy(project(null, "1.0", null)));
        assertEquals("an unknown VertexForge", VersionCompatibility.savedBy(project("VertexForge 01.0.0", "1.0",
                null)));

        assertEquals(Optional.of("1.0.0"),
                VersionCompatibility.savedVersion(project("VertexForge 1.0.0", "1.0", null)));
        assertEquals(Optional.of("1.2.3"), VersionCompatibility.savedVersion(project("1.2.3", "1.0", null)));
        assertEquals(Optional.empty(), VersionCompatibility.savedVersion(project("VertexForge two", "1.0", null)));
    }

    @Test
    void theProjectIsDescribedWithTheFormatsItIsIn() {
        assertEquals("Saved by VertexForge 1.0.0 (project format 1.0, asset format 1.0.0).",
                VersionCompatibility.describe(project("VertexForge 1.0.0", "1.0", null)));
        assertEquals("Saved by an unknown VertexForge (project format 1.0, asset format unknown).",
                VersionCompatibility.describe(project(null, "01.0", "garbled")));
        assertEquals("Saved by VertexForge 1.0.0 (project format unknown, asset format 1.2.0).",
                VersionCompatibility.describe(project("VertexForge 1.0.0", "1", "1.2.0")));
    }

    @Test
    void theLegacyAssetFormatIsOnePointZero() {
        assertEquals(VersionNumber.parse("1.0.0").orElseThrow(), VersionCompatibility.LEGACY_ASSET_FORMAT);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The engines Open could offer
    // ---------------------------------------------------------------------------------------------------------------

    @Test
    void theTargetsAreTheWorkingEnginesThenAWorkingOverride() {
        EngineOption a = engine(A, manifest("1.0.0", "1.0", "1.0.0"));
        Path exe = ROOT.resolve("broken").resolve("Editor.exe");
        EngineOption broken = new EngineOption(Optional.of(B), exe, exe.toString(),
                EngineHealth.broken("gone", manifest("2.0.0", "1.0", "1.0.0")), false, false, false);
        EngineOption c = engine(C, manifest("1.1.0", "1.0", "1.1.0"));
        EngineOption override = override(manifest("3.0.0", "1.0", "1.0.0"));
        EngineOption brokenOverride = new EngineOption(Optional.empty(), null, "relative\\Editor.exe",
                EngineHealth.broken("not absolute"), false, false, true);

        assertEquals(List.of(a, c), VersionCompatibility.targets(options(null, a, broken, c)));
        assertEquals(List.of(a, c, override), VersionCompatibility.targets(options(override, a, broken, c)));
        assertEquals(List.of(a), VersionCompatibility.targets(options(brokenOverride, a)));
        assertEquals(List.of(), VersionCompatibility.targets(options(null)));
    }

    @Test
    void openableLeavesOutWhatBlocksAndKeepsWhatCannotTell() {
        ProjectFile project = project("VertexForge 1.1.0", "1.0", "1.1.0");
        EngineOption old = engine(A, manifest("1.0.0", "1.0", "1.0.0"));
        EngineOption current = engine(B, manifest("1.1.2", "1.0", "1.1.0"));
        Path exe = ROOT.resolve("nomanifest").resolve("Editor.exe");
        EngineOption withoutManifest = new EngineOption(Optional.of(C), exe, exe.toString(),
                EngineHealth.ok(null, ROOT), false, false, false);

        assertEquals(List.of(current, withoutManifest),
                VersionCompatibility.openable(project, options(null, old, current, withoutManifest)));
        assertEquals(List.of(), VersionCompatibility.openable(project, options(null, old)));
    }

    @Test
    void theOverrideIsJudgedLikeAnyOtherEngine() {
        ProjectFile project = project("VertexForge 1.1.0", "1.0", "1.1.0");
        EngineOption current = engine(A, manifest("1.1.0", "1.0", "1.1.0"));
        EngineOption oldOverride = override(manifest("1.0.0", "1.0", "1.0.0"));
        EngineOption currentOverride = override(manifest("1.1.0", "1.0", "1.1.0"));

        assertEquals(List.of(current), VersionCompatibility.openable(project, options(oldOverride, current)));
        assertEquals(List.of(currentOverride), VersionCompatibility.openable(project, options(currentOverride)));
    }

    @Test
    void anEngineSaysWhatItReads() {
        assertEquals("reads project format 1.0 and asset format 1.0.0",
                VersionCompatibility.reads(engine(A, manifest("1.0.0", "1.0", "1.0.0"))));
        assertEquals("reads project format 1.0", VersionCompatibility.reads(engine(A, manifest("1.0.0", "1.0", null))));
        assertEquals("reads asset format 1.0.0",
                VersionCompatibility.reads(engine(A, manifest("1.0.0", null, "1.0.0"))));
        assertEquals("does not say which formats it reads",
                VersionCompatibility.reads(engine(A, manifest("1.0.0", null, null))));
        Path exe = ROOT.resolve("nomanifest").resolve("Editor.exe");
        assertEquals("has no engine.json", VersionCompatibility.reads(new EngineOption(Optional.of(A), exe,
                exe.toString(), EngineHealth.ok(null, ROOT), false, false, false)));
    }
}
