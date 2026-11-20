package com.vertexforge.launcher.util;

import com.vertexforge.launcher.engine.EngineContent;

import java.math.BigInteger;
import java.util.Objects;
import java.util.Optional;
import java.util.regex.Pattern;

/**
 * A {@code MAJOR.MINOR.PATCH} version (VK-1690): the engine's {@code VERSION}, an {@code engine.json}'s
 * {@code engineVersion} and {@code assetFormatVersion}, a template's {@code minimumEngineVersion}, and the version a
 * {@code .vfproj} names in its {@code engineVersion}.
 * <p>
 * One rule reads every form: decimal ASCII digits without leading zeros ({@code 0} itself is fine) and no spaces, as
 * {@code tools/engine_version.lua} writes the version and as {@code EngineManifest} and {@code TemplateManifest} have
 * always required it. So {@code "01.0.0"}, {@code "1.0"}, {@code "1.0.0.0"}, {@code " 1.0.0"} and {@code "1.0.0-rc1"}
 * are not versions. The parts have no upper bound (a {@link BigInteger} each), so a part larger than any engine writes
 * still compares as larger.
 *
 * @param major the major part, at least 0
 * @param minor the minor part, at least 0
 * @param patch the patch part, at least 0
 */
public record VersionNumber(BigInteger major, BigInteger minor, BigInteger patch) implements Comparable<VersionNumber> {

    /**
     * The same pattern as {@code EngineManifest} and {@code TemplateManifest}'s: Java's {@code \d} is ASCII
     * {@code [0-9]} only (no {@code UNICODE_CHARACTER_CLASS}), so digits of other scripts never match.
     */
    private static final Pattern PATTERN = Pattern.compile("(0|[1-9]\\d*)(\\.(0|[1-9]\\d*)){2}");

    /** {@code "VertexForge "}: what the editor writes before its version ({@code Version::displayName}). */
    private static final String PRODUCT_PREFIX = EngineContent.PRODUCT_NAME + " ";

    public VersionNumber {
        Objects.requireNonNull(major, "major");
        Objects.requireNonNull(minor, "minor");
        Objects.requireNonNull(patch, "patch");
        if (major.signum() < 0 || minor.signum() < 0 || patch.signum() < 0) {
            throw new IllegalArgumentException("A version part must not be negative");
        }
    }

    /**
     * The version {@code text} holds, exactly as written: {@code MAJOR.MINOR.PATCH} by the rule above, with nothing
     * around it. Empty for {@code null} and for any other text.
     */
    public static Optional<VersionNumber> parse(String text) {
        if (text == null || !PATTERN.matcher(text).matches()) {
            return Optional.empty();
        }
        // The pattern guarantees exactly two dots and a non-empty run of digits around each.
        int first = text.indexOf('.');
        int second = text.indexOf('.', first + 1);
        return Optional.of(new VersionNumber(new BigInteger(text.substring(0, first)),
                new BigInteger(text.substring(first + 1, second)), new BigInteger(text.substring(second + 1))));
    }

    /**
     * The product version a {@code .vfproj}'s {@code engineVersion} names: {@code "VertexForge 1.0.0"} as the editor
     * writes it, or a bare {@code "1.0.0"}. Spaces, tabs, CRs and LFs around it are ignored, and nothing else: the
     * editor's twin ({@code parseVersionTriple} in {@code ProjectSerialization.cpp}, which keeps a newer record when it
     * stamps the file) trims exactly these, so both read the same texts as the same versions (VK-1690). The product
     * name is {@link EngineContent#PRODUCT_NAME} with its exact letter case, followed by exactly one space, and the
     * version itself follows {@link #parse}'s rule. Empty for {@code null} and for any other text.
     */
    public static Optional<VersionNumber> parseProduct(String text) {
        if (text == null) {
            return Optional.empty();
        }
        String version = trimBlanks(text);
        if (version.startsWith(PRODUCT_PREFIX)) {
            version = version.substring(PRODUCT_PREFIX.length());
        }
        return parse(version);
    }

    /** {@code text} without the spaces, tabs, CRs and LFs at either end. */
    private static String trimBlanks(String text) {
        int start = 0;
        int end = text.length();
        while (start < end && isBlank(text.charAt(start))) {
            start++;
        }
        while (end > start && isBlank(text.charAt(end - 1))) {
            end--;
        }
        return text.substring(start, end);
    }

    private static boolean isBlank(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    /** Compares the major and minor parts only: negative, zero or positive as this one is older, equal or newer. */
    public int compareMajorMinor(VersionNumber other) {
        Objects.requireNonNull(other, "other");
        int order = major.compareTo(other.major);
        return order != 0 ? order : minor.compareTo(other.minor);
    }

    /** Compares numerically, part by part: {@code 1.10.0} is newer than {@code 1.9.0}. Consistent with equals. */
    @Override
    public int compareTo(VersionNumber other) {
        int order = compareMajorMinor(other);
        return order != 0 ? order : patch.compareTo(other.patch);
    }

    /** {@code 1.2.3}: the form {@link #parse} reads. */
    @Override
    public String toString() {
        return major + "." + minor + "." + patch;
    }
}
