package com.vertexforge.launcher.project;

import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.util.VersionNumber;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * Whether an engine can open a project, judged before its editor starts from the versions both name (VK-1690). Pure:
 * it only reads the {@link ProjectFile} and the {@code engine.json} it is given, so it runs on the FX thread and in
 * plain unit tests, like {@code EngineResolver}.
 * <p>
 * The project names its schema version {@code Ps} ({@code schemaVersion}), the product version that last saved or
 * opened it {@code Pv} ({@code engineVersion}, "VertexForge 1.0.0"; {@link VersionNumber#parseProduct}) and the asset
 * format its binary assets are in {@code Pf} ({@code assetFormatVersion}; {@link #LEGACY_ASSET_FORMAT} when absent).
 * The engine's {@code engine.json} names the same three: {@code Ev} ({@code engineVersion}), {@code Es}
 * ({@code projectSchemaVersion}) and {@code Ef} ({@code assetFormatVersion}). The first row that applies wins, and
 * every row that finds the project newer comes before every row that finds it older:
 * <ol start="0">
 *   <li>the engine has no {@code engine.json}: {@link Unknown};</li>
 *   <li>{@code Ps.major > Es.major}: {@link Newer} (the editor would exit with code 4);</li>
 *   <li>{@code Ps.major < Es.major}: {@link Unsupported}, in the editor's words (it would exit with code 3, and no
 *       migration exists);</li>
 *   <li>{@code Pf > Ef}: {@link Newer}, the assets are in a format this engine cannot read;</li>
 *   <li>{@code Pv} is newer than {@code Ev} in its major or minor part: {@link Newer};</li>
 *   <li>{@code Pf < Ef}: {@link Older}, every asset needs a re-import ({@code AssetFormatVersion::matches} wants the
 *       exact format);</li>
 *   <li>{@code Pv} is older than {@code Ev} in its major or minor part: {@link Older}, with no asset format difference
 *       known;</li>
 *   <li>{@code Es} or {@code Ef} is not in the {@code engine.json}: {@link Unknown};</li>
 *   <li>{@code Pv} is missing or not a version: {@link Unknown};</li>
 *   <li>{@code Pf} is present but not a version: {@link Unknown};</li>
 *   <li>otherwise {@link Compatible}: the same versions, or a newer or older patch only (and a newer schema MINOR,
 *       which the editor accepts).</li>
 * </ol>
 * A row whose versions are not all known does not apply. Before the rows, a project whose schema version the editor
 * cannot read is {@link Unsupported} by any engine, in the editor's words: it would exit with code 3 whatever its
 * version. {@link ProjectFileCheck.Readable} rows never get here with one ({@code ProjectHealthService} reports it as
 * Invalid first), but the open flow's fresh read may.
 * <p>
 * A version text longer than {@link #MAX_VERSION_TEXT} characters is not read as a version: no engine writes one (it
 * keeps each part in a {@code uint32_t}, so "VertexForge 4294967295.4294967295.4294967295" is the longest), and turning
 * a megabyte of digits into a {@link BigInteger} would stall the FX thread for seconds.
 */
public final class VersionCompatibility {

    /**
     * The asset format of a {@code .vfproj} without {@code assetFormatVersion}: it was last opened by an engine from
     * before VK-1690, and all of them use 1.0.0. {@code AssetFormatVersion} was introduced at 1.0.0 (commit
     * {@code 069db3d5f}, VK-1680) and has not changed since; before it, binary assets carried the product
     * {@code Version}, which has been 1.0.0 since commit {@code 3c204e792} (2026-03-16). A project a 0.x engine saved
     * still names that version in {@code engineVersion}, so the product rule (row 6) finds it older and Open offers a
     * backup; as its assets may carry their 0.x version, row 6 then claims nothing about them.
     */
    public static final VersionNumber LEGACY_ASSET_FORMAT =
            new VersionNumber(BigInteger.ONE, BigInteger.ZERO, BigInteger.ZERO);

    /** What {@link #savedBy} says when the project does not name the version that saved it. */
    public static final String UNKNOWN_SAVER = "an unknown " + EngineContent.PRODUCT_NAME;

    /** What {@link Newer#required()} says when the project does not name a version newer than the engine's. */
    public static final String ANY_NEWER = "a newer " + EngineContent.PRODUCT_NAME;

    /** Longer version texts are not read as versions (see the class comment). */
    static final int MAX_VERSION_TEXT = 256;

    /** The editor's words for a schema MAJOR older than its own ({@code ProjectSerialization::inspectProject}). */
    static final String SCHEMA_TOO_OLD_START = "Project schema version ";
    static final String SCHEMA_TOO_OLD_MIDDLE = " is older than this engine supports (";

    private static final String PRODUCT = EngineContent.PRODUCT_NAME;

    /** What an engine says about a project. */
    public sealed interface Verdict permits Compatible, Older, Newer, Unsupported, Unknown {

        /** Full sentences for a dialog or a row tooltip; empty for {@link Compatible}. */
        String reason();

        /** Whether this engine must not open the project: its editor would refuse it, or damage what it cannot read. */
        default boolean blocksOpen() {
            return this instanceof Newer || this instanceof Unsupported;
        }
    }

    /** The same versions, or a difference the editor takes as it is (a patch, a newer schema MINOR): open silently. */
    public record Compatible() implements Verdict {
        @Override
        public String reason() {
            return "";
        }
    }

    /**
     * The project was saved by an older engine: opening it with this one upgrades it.
     *
     * @param savedBy  {@link #savedBy}: "VertexForge 1.0.0" or {@link #UNKNOWN_SAVER}
     * @param reimport whether its assets are in an older format, so that every asset needs a re-import
     * @param reason   what changes, for the dialog
     */
    public record Older(String savedBy, boolean reimport, String reason) implements Verdict {
        public Older {
            Objects.requireNonNull(savedBy, "savedBy");
            Objects.requireNonNull(reason, "reason");
        }
    }

    /**
     * The project needs a newer engine than this one.
     *
     * @param required "VertexForge 1.1.0" (the version that saved it), or {@link #ANY_NEWER} when the project does not
     *                 name a version newer than this engine's
     * @param reason   what the project needs and what this engine reads
     */
    public record Newer(String required, String reason) implements Verdict {
        public Newer {
            Objects.requireNonNull(required, "required");
            Objects.requireNonNull(reason, "reason");
        }
    }

    /** This engine's editor refuses the project, and no migration exists: {@code reason} is the editor's message. */
    public record Unsupported(String reason) implements Verdict {
        public Unsupported {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /** One side does not say enough to judge; Open still works (warning once, VK-1690's open flow). */
    public record Unknown(String reason) implements Verdict {
        public Unknown {
            Objects.requireNonNull(reason, "reason");
        }
    }

    private VersionCompatibility() {
        // Utility class - prevent instantiation
    }

    /**
     * What {@code engine} says about {@code project}, from its {@code engine.json} as last checked. The messages name
     * the engine by its version ({@link EngineOption#versionText()}), or by its path when it has no
     * {@code engine.json}.
     */
    public static Verdict check(ProjectFile project, EngineOption engine) {
        Objects.requireNonNull(project, "project");
        Objects.requireNonNull(engine, "engine");
        EngineManifest manifest = engine.health().manifest();
        return check(project, manifest, manifest == null ? engine.pathText() : engine.versionText());
    }

    /**
     * What an engine with this {@code engine.json} says about {@code project}.
     *
     * @param manifest   the engine's {@code engine.json}, or {@code null} when it has none (row 0)
     * @param engineText how the messages name the engine, such as "1.0.0 Development"
     */
    public static Verdict check(ProjectFile project, EngineManifest manifest, String engineText) {
        Objects.requireNonNull(project, "project");
        Objects.requireNonNull(engineText, "engineText");

        Optional<SchemaVersion> projectSchema = project.schema();
        if (projectSchema.isEmpty()) {
            // The editor reports the first member it refuses, and only then the schema version's format.
            return new Unsupported(project.structureProblem() != null
                    ? project.structureProblem()
                    : EditorProjectRules.schemaFormatProblem(project.schemaVersion()));
        }
        if (manifest == null) {
            return new Unknown("The engine (" + engineText + ") has no " + EngineManifest.FILE_NAME
                    + ", so the launcher cannot tell whether it reads this project.");
        }

        SchemaVersion ps = projectSchema.get();
        Optional<VersionNumber> pv = productVersion(project);
        Optional<VersionNumber> pf = assetFormat(project);
        Optional<VersionNumber> ev = version(manifest.engineVersion());
        Optional<SchemaVersion> es = SchemaVersion.parse(manifest.projectSchemaVersion());
        Optional<VersionNumber> ef = version(manifest.assetFormatVersion());
        String required = pv.filter(saved -> ev.isEmpty() || saved.compareTo(ev.get()) > 0)
                .map(saved -> PRODUCT + " " + saved)
                .orElse(ANY_NEWER);

        // Newer first: whatever else differs, an engine that cannot read the project must not open it.
        if (es.isPresent() && ps.major() > es.get().major()) {
            return new Newer(required, newer(project, engineText, "reads project format " + es.get().major()
                    + ".x, so it cannot open it", required));
        }
        if (es.isPresent() && ps.major() < es.get().major()) {
            return new Unsupported(schemaTooOld(ps, es.get()));
        }
        if (pf.isPresent() && ef.isPresent() && pf.get().compareTo(ef.get()) > 0) {
            return new Newer(required, newer(project, engineText, "reads asset format " + ef.get()
                    + ", which is older than the project's, so it cannot read its assets", required));
        }
        if (pv.isPresent() && ev.isPresent() && pv.get().compareMajorMinor(ev.get()) > 0) {
            return new Newer(required, newer(project, engineText, "is older than the " + PRODUCT + " "
                    + majorMinor(pv.get()) + " that saved it", required));
        }

        // Then older: the engine upgrades the project.
        if (pf.isPresent() && ef.isPresent() && pf.get().compareTo(ef.get()) < 0) {
            return new Older(savedBy(project), true, describe(project) + " This engine (" + engineText
                    + ") reads asset format " + ef.get() + ", so its assets need a re-import." + marks(ev));
        }
        if (pv.isPresent() && ev.isPresent() && pv.get().compareMajorMinor(ev.get()) < 0) {
            // Both formats known means both equal here: rows 3 and 5 compared them. Only a format the project names
            // is known: the legacy one is assumed, and a 0.x project's assets carried its 0.x version instead.
            String assets = project.assetFormatVersion() != null && pf.isPresent() && ef.isPresent()
                    ? " and reads the same asset format, so its assets need no re-import"
                    : "";
            return new Older(savedBy(project), false, describe(project) + " This engine (" + engineText
                    + ") is newer" + assets + "." + marks(ev));
        }

        // Nothing differs that both sides name; judge only when they name enough.
        if (es.isEmpty() || ef.isEmpty() || ev.isEmpty()) {
            return new Unknown("The engine (" + engineText + ") does not say in its " + EngineManifest.FILE_NAME
                    + " which project and asset formats it reads, so the launcher cannot tell whether it reads this"
                    + " project.");
        }
        if (pv.isEmpty()) {
            String engineVersion = project.engineVersion();
            String says = engineVersion == null || engineVersion.isBlank()
                    ? "The project file does not say which " + PRODUCT + " saved it"
                    : "The project file's engineVersion is not a " + PRODUCT + " version";
            return new Unknown(says + ", so the launcher cannot tell whether this engine (" + engineText
                    + ") reads it.");
        }
        if (pf.isEmpty()) {
            return new Unknown("The project file's assetFormatVersion is not a MAJOR.MINOR.PATCH version, so the"
                    + " launcher cannot tell whether this engine (" + engineText + ") reads its assets.");
        }
        return new Compatible();
    }

    /**
     * The engines a project can be opened with: every registered engine that works ({@link EngineOptions#usable()}),
     * then a working {@code VERTEXFORGE_EDITOR_PATH} override. {@link EngineOptions} keeps the override apart from the
     * registered engines (it is never bound), so it is never one of them already.
     */
    public static List<EngineOption> targets(EngineOptions options) {
        Objects.requireNonNull(options, "options");
        List<EngineOption> targets = new ArrayList<>(options.usable());
        options.activeOverride().filter(override -> !targets.contains(override)).ifPresent(targets::add);
        return List.copyOf(targets);
    }

    /**
     * The {@link #targets} whose verdict on {@code project} does not block Open. An {@link Unknown} verdict counts as
     * able to open it: an engine that does not say what it reads is never refused for it.
     */
    public static List<EngineOption> openable(ProjectFile project, EngineOptions options) {
        Objects.requireNonNull(project, "project");
        return targets(options).stream().filter(engine -> !check(project, engine).blocksOpen()).toList();
    }

    /** "VertexForge 1.0.0" (normalised from the project's {@code engineVersion}), or {@link #UNKNOWN_SAVER}. */
    public static String savedBy(ProjectFile project) {
        return productVersion(project).map(version -> PRODUCT + " " + version).orElse(UNKNOWN_SAVER);
    }

    /** The project's product version as a bare {@code MAJOR.MINOR.PATCH}, if it has one (backup file names use it). */
    public static Optional<String> savedVersion(ProjectFile project) {
        return productVersion(project).map(VersionNumber::toString);
    }

    /**
     * "Saved by VertexForge 1.0.0 (project format 1.0, asset format 1.0.0).": the project's side of every message. A
     * project without {@code assetFormatVersion} shows {@link #LEGACY_ASSET_FORMAT}; a version it does not name in a
     * form the launcher reads shows as "unknown".
     */
    public static String describe(ProjectFile project) {
        Objects.requireNonNull(project, "project");
        String schema = project.schema().map(SchemaVersion::toString).orElse("unknown");
        String assets = assetFormat(project).map(VersionNumber::toString).orElse("unknown");
        return "Saved by " + savedBy(project) + " (project format " + schema + ", asset format " + assets + ").";
    }

    /**
     * What an engine says it reads: "reads project format 1.0 and asset format 1.0.0", naming only what its
     * {@code engine.json} names ("does not say which formats it reads" when it names neither, "has no engine.json"
     * without one). Completes a sentence that starts with the engine.
     */
    public static String reads(EngineOption engine) {
        Objects.requireNonNull(engine, "engine");
        EngineManifest manifest = engine.health().manifest();
        if (manifest == null) {
            return "has no " + EngineManifest.FILE_NAME;
        }
        List<String> parts = new ArrayList<>();
        SchemaVersion.parse(manifest.projectSchemaVersion()).ifPresent(schema -> parts.add("project format " + schema));
        version(manifest.assetFormatVersion()).ifPresent(format -> parts.add("asset format " + format));
        return parts.isEmpty() ? "does not say which formats it reads" : "reads " + String.join(" and ", parts);
    }

    /** "Project schema version 0.5 is older than this engine supports (1.0)": the editor's message (exit code 3). */
    static String schemaTooOld(SchemaVersion project, SchemaVersion engine) {
        return SCHEMA_TOO_OLD_START + project + SCHEMA_TOO_OLD_MIDDLE + engine + ")";
    }

    /** "Saved by …. This engine (1.0.0 Development) reads …. It needs VertexForge 1.1.0 or later." */
    private static String newer(ProjectFile project, String engineText, String what, String required) {
        return describe(project) + " This engine (" + engineText + ") " + what + ". "
                + (required.equals(ANY_NEWER) ? "It needs " + ANY_NEWER + "." : "It needs " + required + " or later.");
    }

    /** What opening it does to the project: the editor stamps its own versions into the file (VK-1690). */
    private static String marks(Optional<VersionNumber> engineVersion) {
        return engineVersion.map(version -> " Opening it marks the project as " + PRODUCT + " " + version
                + ", which engines older than " + majorMinor(version) + " cannot open.").orElse("");
    }

    private static String majorMinor(VersionNumber version) {
        return version.major() + "." + version.minor();
    }

    /** {@code Pv}: the project's {@code engineVersion} as a version, if it is one. */
    private static Optional<VersionNumber> productVersion(ProjectFile project) {
        Objects.requireNonNull(project, "project");
        String text = project.engineVersion();
        return text == null || text.length() > MAX_VERSION_TEXT ? Optional.empty() : VersionNumber.parseProduct(text);
    }

    /** {@code Pf}: {@link #LEGACY_ASSET_FORMAT} when the member is absent; empty when it is there but malformed. */
    private static Optional<VersionNumber> assetFormat(ProjectFile project) {
        String text = project.assetFormatVersion();
        return text == null ? Optional.of(LEGACY_ASSET_FORMAT) : version(text);
    }

    private static Optional<VersionNumber> version(String text) {
        return text == null || text.length() > MAX_VERSION_TEXT ? Optional.empty() : VersionNumber.parse(text);
    }
}
