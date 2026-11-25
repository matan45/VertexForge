package com.vertexforge.launcher.project;

import java.util.Optional;

/**
 * A {@code .vfproj} {@code schemaVersion} as the editor reads it (VK-1686), so that a row says what Open will do.
 * {@link #parse} is a twin of {@code ProjectSerialization::parseSchemaVersion}
 * ({@code VFEngine/utilities/serialization/ProjectSerialization.cpp}): decimal digits on both sides of the first dot,
 * nothing else. A part too large for 32 bits saturates, so it still reads as a (very) new version.
 *
 * @param major the major part, 0 to {@link #MAX_PART}
 * @param minor the minor part, 0 to {@link #MAX_PART}
 */
public record SchemaVersion(long major, long minor) {

    /** The editor keeps each part in a {@code uint32_t}. */
    public static final long MAX_PART = 0xFFFF_FFFFL;

    public SchemaVersion {
        if (major < 0 || major > MAX_PART || minor < 0 || minor > MAX_PART) {
            throw new IllegalArgumentException("A schema version part must be 0 to " + MAX_PART);
        }
    }

    /**
     * The version {@code text} holds, or empty when the editor would refuse it: {@code null}, no dot, or a part that is
     * empty or holds anything but the ASCII digits {@code 0}-{@code 9} (so "1", "1.0.0", " 1.0", "+1.0", "1." are
     * refused). Leading zeros are allowed.
     */
    public static Optional<SchemaVersion> parse(String text) {
        if (text == null) {
            return Optional.empty();
        }
        int dot = text.indexOf('.');
        if (dot < 0) {
            return Optional.empty();
        }
        long major = part(text, 0, dot);
        long minor = part(text, dot + 1, text.length());
        if (major < 0 || minor < 0) {
            return Optional.empty();
        }
        return Optional.of(new SchemaVersion(major, minor));
    }

    /** The digits of {@code text[from, to)} as a number, saturated at {@link #MAX_PART}; -1 when they are not digits. */
    private static long part(String text, int from, int to) {
        if (from >= to) {
            return -1;
        }
        long value = 0;
        for (int i = from; i < to; i++) {
            char c = text.charAt(i);
            // Never Character.isDigit: it accepts digits of other scripts, which the editor refuses.
            if (c < '0' || c > '9') {
                return -1;
            }
            if (value <= MAX_PART) {
                value = value * 10 + (c - '0');
            }
        }
        return Math.min(value, MAX_PART);
    }

    /** {@code MAJOR.MINOR} without leading zeros, as the editor prints it. */
    @Override
    public String toString() {
        return major + "." + minor;
    }
}
